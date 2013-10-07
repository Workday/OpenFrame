// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/sync_notifier/synced_notification.h"

#include "base/basictypes.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/notifications/notification_ui_manager.h"
#include "chrome/browser/notifications/sync_notifier/chrome_notifier_delegate.h"
#include "content/public/browser/browser_thread.h"
#include "sync/protocol/sync.pb.h"
#include "sync/protocol/synced_notification_specifics.pb.h"
#include "ui/gfx/image/image.h"
#include "ui/message_center/message_center_util.h"
#include "ui/message_center/notification_types.h"

namespace {
const char kExtensionScheme[] = "synced-notification://";
const char kDefaultSyncedNotificationScheme[] = "https:";

// The name of our first synced notification service.
// TODO(petewil): remove this hardcoding once we have the synced notification
// signalling sync data type set up to provide this.
// crbug.com/248337
const char kFirstSyncedNotificationServiceId[] = "Google+";


// Today rich notifications only supports two buttons, make sure we don't
// try to supply them with more than this number of buttons.
const unsigned int kMaxNotificationButtonIndex = 2;

bool UseRichNotifications() {
  return message_center::IsRichNotificationEnabled();
}

// Schema-less specs default badly in windows.  If we find one, add the schema
// we expect instead of allowing windows specific GURL code to make it default
// to "file:".
GURL AddDefaultSchemaIfNeeded(std::string& url_spec) {
  if (StartsWithASCII(url_spec, std::string("//"), false))
    return GURL(std::string(kDefaultSyncedNotificationScheme) + url_spec);

  return GURL(url_spec);
}

}  // namespace

namespace notifier {

COMPILE_ASSERT(static_cast<sync_pb::CoalescedSyncedNotification_ReadState>(
                   SyncedNotification::kUnread) ==
               sync_pb::CoalescedSyncedNotification_ReadState_UNREAD,
               local_enum_must_match_protobuf_enum);
COMPILE_ASSERT(static_cast<sync_pb::CoalescedSyncedNotification_ReadState>(
                   SyncedNotification::kRead) ==
               sync_pb::CoalescedSyncedNotification_ReadState_READ,
               local_enum_must_match_protobuf_enum);
COMPILE_ASSERT(static_cast<sync_pb::CoalescedSyncedNotification_ReadState>(
                   SyncedNotification::kDismissed) ==
               sync_pb::CoalescedSyncedNotification_ReadState_DISMISSED,
               local_enum_must_match_protobuf_enum);

SyncedNotification::SyncedNotification(const syncer::SyncData& sync_data)
    : notification_manager_(NULL),
      notifier_service_(NULL),
      profile_(NULL),
      active_fetcher_count_(0) {
  Update(sync_data);
}

SyncedNotification::~SyncedNotification() {}

void SyncedNotification::Update(const syncer::SyncData& sync_data) {
  // TODO(petewil): Add checking that the notification looks valid.
  specifics_.CopyFrom(sync_data.GetSpecifics().synced_notification());
}

sync_pb::EntitySpecifics SyncedNotification::GetEntitySpecifics() const {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.mutable_synced_notification()->CopyFrom(specifics_);
  return entity_specifics;
}

void SyncedNotification::OnFetchComplete(const GURL url,
                                         const SkBitmap* bitmap) {
  // TODO(petewil): Add timeout mechanism in case bitmaps take too long.  Do we
  // already have one built into URLFetcher?
  // Make sure we are on the thread we expect.
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  // Match the incoming bitmaps to URLs.  In case this is a dup, make sure to
  // try all potentially matching urls.
  if (GetAppIconUrl() == url && bitmap != NULL) {
    app_icon_bitmap_ = gfx::Image::CreateFrom1xBitmap(*bitmap);
  }
  if (GetImageUrl() == url && bitmap != NULL) {
    image_bitmap_ = gfx::Image::CreateFrom1xBitmap(*bitmap);
  }
  if (GetProfilePictureUrl(0) == url && bitmap != NULL) {
    sender_bitmap_ = gfx::Image::CreateFrom1xBitmap(*bitmap);
  }

  // If this URL matches one or more button bitmaps, save them off.
  for (unsigned int i = 0; i < GetButtonCount(); ++i) {
    if (GetButtonIconUrl(i) == url && bitmap != NULL)
      button_bitmaps_[i] = gfx::Image::CreateFrom1xBitmap(*bitmap);
  }

  // Count off the bitmaps as they arrive.
  --active_fetcher_count_;
  DCHECK_GE(active_fetcher_count_, 0);
  // See if all bitmaps are accounted for, if so call Show.
  if (active_fetcher_count_ == 0) {
    Show(notification_manager_, notifier_service_, profile_);
  }
}

void SyncedNotification::QueueBitmapFetchJobs(
    NotificationUIManager* notification_manager,
    ChromeNotifierService* notifier_service,
    Profile* profile) {
  // If we are not using the MessageCenter, call show now, and the existing
  // code will handle the bitmap fetch for us.
  if (!UseRichNotifications()) {
    Show(notification_manager, notifier_service, profile);
    return;
  }

  // Save off the arguments for the call to Show.
  notification_manager_ = notification_manager;
  notifier_service_ = notifier_service;
  profile_ = profile;
  DCHECK_EQ(active_fetcher_count_, 0);

  // Ensure our bitmap vector has as many entries as there are buttons,
  // so that when the bitmaps arrive the vector has a slot for them.
  for (unsigned int i = 0; i < GetButtonCount(); ++i) {
    button_bitmaps_.push_back(gfx::Image());
    AddBitmapToFetchQueue(GetButtonIconUrl(i));
  }

  // If there is a profile image bitmap, fetch it
  if (GetProfilePictureCount() > 0) {
    // TODO(petewil): When we have the capacity to display more than one bitmap,
    // modify this code to fetch as many as we can display
    AddBitmapToFetchQueue(GetProfilePictureUrl(0));
  }

  // If the URL is non-empty, add it to our queue of URLs to fetch.
  AddBitmapToFetchQueue(GetAppIconUrl());
  AddBitmapToFetchQueue(GetImageUrl());

  // If there are no bitmaps, call show now.
  if (active_fetcher_count_ == 0) {
    Show(notification_manager, notifier_service, profile);
  }
}

void SyncedNotification::StartBitmapFetch() {
  // Now that we have queued and counted them all, start the fetching.
  ScopedVector<NotificationBitmapFetcher>::iterator iter;
  for (iter = fetchers_.begin(); iter != fetchers_.end(); ++iter) {
    (*iter)->Start(profile_);
  }
}

void SyncedNotification::AddBitmapToFetchQueue(const GURL& url) {
  // Check for dups, ignore any request for a dup.
  ScopedVector<NotificationBitmapFetcher>::iterator iter;
  for (iter = fetchers_.begin(); iter != fetchers_.end(); ++iter) {
    if ((*iter)->url() == url)
      return;
  }

  if (url.is_valid()) {
    ++active_fetcher_count_;
    fetchers_.push_back(new NotificationBitmapFetcher(url, this));
  }
}

void SyncedNotification::Show(NotificationUIManager* notification_manager,
                              ChromeNotifierService* notifier_service,
                              Profile* profile) {
  // Let NotificationUIManager know that the notification has been dismissed.
  if (SyncedNotification::kRead == GetReadState() ||
      SyncedNotification::kDismissed == GetReadState() ) {
    notification_manager->CancelById(GetKey());
    DVLOG(2) << "Dismissed or read notification arrived"
             << GetHeading() << " " << GetText();
    return;
  }

  // Set up the fields we need to send and create a Notification object.
  GURL image_url = GetImageUrl();
  string16 text = UTF8ToUTF16(GetText());
  string16 heading = UTF8ToUTF16(GetHeading());
  string16 description = UTF8ToUTF16(GetDescription());
  string16 annotation = UTF8ToUTF16(GetAnnotation());
  // TODO(petewil): Eventually put the display name of the sending service here.
  string16 display_source = UTF8ToUTF16(GetAppId());
  string16 replace_key = UTF8ToUTF16(GetKey());
  string16 notification_heading = heading;
  string16 notification_text = description;
  string16 newline = UTF8ToUTF16("\n");

  // The delegate will eventually catch calls that the notification
  // was read or deleted, and send the changes back to the server.
  scoped_refptr<NotificationDelegate> delegate =
      new ChromeNotifierDelegate(GetKey(), notifier_service);

  // Some inputs and fields are only used if there is a notification center.
  if (UseRichNotifications()) {
    base::Time creation_time =
        base::Time::FromDoubleT(static_cast<double>(GetCreationTime()));
    int priority = GetPriority();
    int notification_count = GetNotificationCount();
    unsigned int button_count = GetButtonCount();

    // Deduce which notification template to use from the data.
    message_center::NotificationType notification_type =
        message_center::NOTIFICATION_TYPE_BASE_FORMAT;
    if (!image_url.is_empty()) {
      notification_type = message_center::NOTIFICATION_TYPE_IMAGE;
    } else if (notification_count > 1) {
      notification_type = message_center::NOTIFICATION_TYPE_MULTIPLE;
    } else if (button_count > 0) {
      notification_type = message_center::NOTIFICATION_TYPE_BASE_FORMAT;
    }

    // Fill the optional fields with the information we need to make a
    // notification.
    message_center::RichNotificationData rich_notification_data;
    rich_notification_data.timestamp = creation_time;
    if (priority != SyncedNotification::kUndefinedPriority)
      rich_notification_data.priority = priority;

    // Fill in the button data.
    // TODO(petewil): Today Rich notifiations are limited to two buttons.
    // When rich notifications supports more, remove the
    // "&& i < kMaxNotificationButtonIndex" clause below.
    for (unsigned int i = 0;
         i < button_count
         && i < button_bitmaps_.size()
         && i < kMaxNotificationButtonIndex;
         ++i) {
      // Stop at the first button with no title
      std::string title = GetButtonTitle(i);
      if (title.empty())
        break;
      message_center::ButtonInfo button_info(UTF8ToUTF16(title));
      if (!button_bitmaps_[i].IsEmpty())
        button_info.icon = button_bitmaps_[i];
      rich_notification_data.buttons.push_back(button_info);
    }

    // Fill in the bitmap images.
    if (!image_bitmap_.IsEmpty())
      rich_notification_data.image = image_bitmap_;

    // Fill the individual notification fields for a multiple notification.
    if (notification_count > 1) {
      for (int ii = 0; ii < notification_count; ++ii) {
        message_center::NotificationItem item(
            UTF8ToUTF16(GetContainedNotificationTitle(ii)),
            UTF8ToUTF16(GetContainedNotificationMessage(ii)));
        rich_notification_data.items.push_back(item);
      }
    }

    // The text encompasses both the description and the annotation.
    if (!notification_text.empty())
      notification_text = notification_text + newline;
    notification_text = notification_text + annotation;

    // If there is a single person sending, use their picture instead of the app
    // icon.
    // TODO(petewil): Someday combine multiple profile photos here.
    gfx::Image icon_bitmap = app_icon_bitmap_;
    if (GetProfilePictureCount() == 1)  {
      icon_bitmap = sender_bitmap_;
    }

    Notification ui_notification(notification_type,
                                 GetOriginUrl(),
                                 notification_heading,
                                 notification_text,
                                 icon_bitmap,
                                 WebKit::WebTextDirectionDefault,
                                 display_source,
                                 replace_key,
                                 rich_notification_data,
                                 delegate.get());
    notification_manager->Add(ui_notification, profile);
  } else {
    // In this case we have a Webkit Notification, not a Rich Notification.
    Notification ui_notification(GetOriginUrl(),
                                 GetAppIconUrl(),
                                 notification_heading,
                                 notification_text,
                                 WebKit::WebTextDirectionDefault,
                                 display_source,
                                 replace_key,
                                 delegate.get());

    notification_manager->Add(ui_notification, profile);
  }

  DVLOG(1) << "Showing Synced Notification! " << heading << " " << text
           << " " << GetAppIconUrl() << " " << replace_key << " "
           << GetReadState();

  return;
}

// This should detect even small changes in case the server updated the
// notification.  We ignore the timestamp if other fields match.
bool SyncedNotification::EqualsIgnoringReadState(
    const SyncedNotification& other) const {
  if (GetTitle() == other.GetTitle() &&
      GetHeading() == other.GetHeading() &&
      GetDescription() == other.GetDescription() &&
      GetAnnotation() == other.GetAnnotation() &&
      GetAppId() == other.GetAppId() &&
      GetKey() == other.GetKey() &&
      GetOriginUrl() == other.GetOriginUrl() &&
      GetAppIconUrl() == other.GetAppIconUrl() &&
      GetImageUrl() == other.GetImageUrl() &&
      GetText() == other.GetText() &&
      // We intentionally skip read state
      GetCreationTime() == other.GetCreationTime() &&
      GetPriority() == other.GetPriority() &&
      GetDefaultDestinationTitle() == other.GetDefaultDestinationTitle() &&
      GetDefaultDestinationIconUrl() == other.GetDefaultDestinationIconUrl() &&
      GetNotificationCount() == other.GetNotificationCount() &&
      GetButtonCount() == other.GetButtonCount() &&
      GetProfilePictureCount() == other.GetProfilePictureCount()) {

    // If all the surface data matched, check, to see if contained data also
    // matches, titles and messages.
    size_t count = GetNotificationCount();
    for (size_t ii = 0; ii < count; ++ii) {
      if (GetContainedNotificationTitle(ii) !=
          other.GetContainedNotificationTitle(ii))
        return false;
      if (GetContainedNotificationMessage(ii) !=
          other.GetContainedNotificationMessage(ii))
        return false;
    }

    // Make sure buttons match.
    count = GetButtonCount();
    for (size_t jj = 0; jj < count; ++jj) {
      if (GetButtonTitle(jj) != other.GetButtonTitle(jj))
        return false;
      if (GetButtonIconUrl(jj) != other.GetButtonIconUrl(jj))
        return false;
    }

    // Make sure profile icons match
    count = GetButtonCount();
    for (size_t kk = 0; kk < count; ++kk) {
      if (GetProfilePictureUrl(kk) != other.GetProfilePictureUrl(kk))
        return false;
    }

    // If buttons and notifications matched, they are equivalent.
    return true;
  }

  return false;
}

void SyncedNotification::LogNotification() {
  std::string readStateString("Unread");
  if (SyncedNotification::kRead == GetReadState())
    readStateString = "Read";
  else if (SyncedNotification::kDismissed == GetReadState())
    readStateString = "Dismissed";

  DVLOG(2) << " Notification: Heading is " << GetHeading()
           << " description is " << GetDescription()
           << " key is " << GetKey()
           << " read state is " << readStateString;
}

// Set the read state on the notification, returns true for success.
void SyncedNotification::SetReadState(const ReadState& read_state) {

  // Convert the read state to the protobuf type for read state.
  if (kDismissed == read_state)
    specifics_.mutable_coalesced_notification()->set_read_state(
        sync_pb::CoalescedSyncedNotification_ReadState_DISMISSED);
  else if (kUnread == read_state)
    specifics_.mutable_coalesced_notification()->set_read_state(
        sync_pb::CoalescedSyncedNotification_ReadState_UNREAD);
  else if (kRead == read_state)
    specifics_.mutable_coalesced_notification()->set_read_state(
        sync_pb::CoalescedSyncedNotification_ReadState_READ);
  else
    NOTREACHED();
}

void SyncedNotification::NotificationHasBeenRead() {
  SetReadState(kRead);
}

void SyncedNotification::NotificationHasBeenDismissed() {
  SetReadState(kDismissed);
}

std::string SyncedNotification::GetTitle() const {
  if (!specifics_.coalesced_notification().render_info().expanded_info().
      simple_expanded_layout().has_title())
    return std::string();

  return specifics_.coalesced_notification().render_info().expanded_info().
      simple_expanded_layout().title();
}

std::string SyncedNotification::GetHeading() const {
  if (!specifics_.coalesced_notification().render_info().collapsed_info().
      simple_collapsed_layout().has_heading())
    return std::string();

  return specifics_.coalesced_notification().render_info().collapsed_info().
      simple_collapsed_layout().heading();
}

std::string SyncedNotification::GetDescription() const {
  if (!specifics_.coalesced_notification().render_info().collapsed_info().
      simple_collapsed_layout().has_description())
    return std::string();

  return specifics_.coalesced_notification().render_info().collapsed_info().
      simple_collapsed_layout().description();
}

std::string SyncedNotification::GetAnnotation() const {
  if (!specifics_.coalesced_notification().render_info().collapsed_info().
      simple_collapsed_layout().has_annotation())
    return std::string();

  return specifics_.coalesced_notification().render_info().collapsed_info().
      simple_collapsed_layout().annotation();
}

std::string SyncedNotification::GetAppId() const {
  if (!specifics_.coalesced_notification().has_app_id())
    return std::string();
  return specifics_.coalesced_notification().app_id();
}

std::string SyncedNotification::GetKey() const {
  if (!specifics_.coalesced_notification().has_key())
    return std::string();
  return specifics_.coalesced_notification().key();
}

GURL SyncedNotification::GetOriginUrl() const {
  std::string origin_url(kExtensionScheme);
  origin_url += GetAppId();
  return GURL(origin_url);
}

GURL SyncedNotification::GetAppIconUrl() const {
  if (!specifics_.coalesced_notification().render_info().collapsed_info().
      simple_collapsed_layout().has_app_icon())
    return GURL();

  std::string url_spec = specifics_.coalesced_notification().render_info().
              collapsed_info().simple_collapsed_layout().app_icon().url();

  return AddDefaultSchemaIfNeeded(url_spec);
}

// TODO(petewil): This ignores all but the first image.  If Rich Notifications
// supports more images someday, then fetch all images.
GURL SyncedNotification::GetImageUrl() const {
  if (specifics_.coalesced_notification().render_info().collapsed_info().
      simple_collapsed_layout().media_size() == 0)
    return GURL();

  if (!specifics_.coalesced_notification().render_info().collapsed_info().
      simple_collapsed_layout().media(0).image().has_url())
    return GURL();

  std::string url_spec = specifics_.coalesced_notification().render_info().
              collapsed_info().simple_collapsed_layout().media(0).image().url();

  return AddDefaultSchemaIfNeeded(url_spec);
}

std::string SyncedNotification::GetText() const {
  if (!specifics_.coalesced_notification().render_info().expanded_info().
      simple_expanded_layout().has_text())
    return std::string();

  return specifics_.coalesced_notification().render_info().expanded_info().
      simple_expanded_layout().text();
}

SyncedNotification::ReadState SyncedNotification::GetReadState() const {
  DCHECK(specifics_.coalesced_notification().has_read_state());

  sync_pb::CoalescedSyncedNotification_ReadState found_read_state =
      specifics_.coalesced_notification().read_state();

  if (found_read_state ==
      sync_pb::CoalescedSyncedNotification_ReadState_DISMISSED) {
    return kDismissed;
  } else if (found_read_state ==
             sync_pb::CoalescedSyncedNotification_ReadState_UNREAD) {
    return kUnread;
  } else if (found_read_state ==
             sync_pb::CoalescedSyncedNotification_ReadState_READ) {
    return kRead;
  } else {
    NOTREACHED();
    return static_cast<SyncedNotification::ReadState>(found_read_state);
  }
}

// Time in milliseconds since the unix epoch, or 0 if not available.
uint64 SyncedNotification::GetCreationTime() const {
  if (!specifics_.coalesced_notification().has_creation_time_msec())
    return 0;

  return specifics_.coalesced_notification().creation_time_msec();
}

int SyncedNotification::GetPriority() const {
  if (!specifics_.coalesced_notification().has_priority())
    return kUndefinedPriority;
  int protobuf_priority = specifics_.coalesced_notification().priority();

  // Convert the prioroty to the scheme used by the notification center.
  if (protobuf_priority ==
      sync_pb::CoalescedSyncedNotification_Priority_LOW) {
    return message_center::LOW_PRIORITY;
  } else if (protobuf_priority ==
             sync_pb::CoalescedSyncedNotification_Priority_STANDARD) {
    return message_center::DEFAULT_PRIORITY;
  } else if (protobuf_priority ==
             sync_pb::CoalescedSyncedNotification_Priority_HIGH) {
    // High priority synced notifications are considered default priority in
    // Chrome.
    return message_center::DEFAULT_PRIORITY;
  } else {
    // Complain if this is a new priority we have not seen before.
    DCHECK(protobuf_priority <
           sync_pb::CoalescedSyncedNotification_Priority_LOW  ||
           sync_pb::CoalescedSyncedNotification_Priority_HIGH <
           protobuf_priority);
    return kUndefinedPriority;
  }
}

size_t SyncedNotification::GetNotificationCount() const {
  return specifics_.coalesced_notification().render_info().
      expanded_info().collapsed_info_size();
}

size_t SyncedNotification::GetButtonCount() const {
  return specifics_.coalesced_notification().render_info().collapsed_info().
      target_size();
}

size_t SyncedNotification::GetProfilePictureCount() const {
  return specifics_.coalesced_notification().render_info().collapsed_info().
      simple_collapsed_layout().profile_image_size();
}

GURL SyncedNotification::GetProfilePictureUrl(unsigned int which_url) const {
  if (GetProfilePictureCount() <= which_url)
    return GURL();

  std::string url_spec = specifics_.coalesced_notification().render_info().
      collapsed_info().simple_collapsed_layout().profile_image(which_url).
      image_url();

  return AddDefaultSchemaIfNeeded(url_spec);
}


std::string SyncedNotification::GetDefaultDestinationTitle() const {
  if (!specifics_.coalesced_notification().render_info().collapsed_info().
      default_destination().icon().has_alt_text()) {
    return std::string();
  }
  return specifics_.coalesced_notification().render_info().collapsed_info().
      default_destination().icon().alt_text();
}

GURL SyncedNotification::GetDefaultDestinationIconUrl() const {
  if (!specifics_.coalesced_notification().render_info().collapsed_info().
      default_destination().icon().has_url()) {
    return GURL();
  }
  std::string url_spec = specifics_.coalesced_notification().render_info().
              collapsed_info().default_destination().icon().url();

  return AddDefaultSchemaIfNeeded(url_spec);
}

GURL SyncedNotification::GetDefaultDestinationUrl() const {
  if (!specifics_.coalesced_notification().render_info().collapsed_info().
      default_destination().has_url()) {
    return GURL();
  }
  std::string url_spec = specifics_.coalesced_notification().render_info().
              collapsed_info().default_destination().url();

  return AddDefaultSchemaIfNeeded(url_spec);
}

std::string SyncedNotification::GetButtonTitle(
    unsigned int which_button) const {
  // Must ensure that we have a target before trying to access it.
  if (GetButtonCount() <= which_button)
    return std::string();
  if (!specifics_.coalesced_notification().render_info().collapsed_info().
      target(which_button).action().icon().has_alt_text()) {
    return std::string();
  }
  return specifics_.coalesced_notification().render_info().collapsed_info().
      target(which_button).action().icon().alt_text();
}

GURL SyncedNotification::GetButtonIconUrl(unsigned int which_button) const {
  // Must ensure that we have a target before trying to access it.
  if (GetButtonCount() <= which_button)
    return GURL();
  if (!specifics_.coalesced_notification().render_info().collapsed_info().
      target(which_button).action().icon().has_url()) {
    return GURL();
  }
  std::string url_spec = specifics_.coalesced_notification().render_info().
              collapsed_info().target(which_button).action().icon().url();

  return AddDefaultSchemaIfNeeded(url_spec);
}

GURL SyncedNotification::GetButtonUrl(unsigned int which_button) const {
  // Must ensure that we have a target before trying to access it.
  if (GetButtonCount() <= which_button)
    return GURL();
  if (!specifics_.coalesced_notification().render_info().collapsed_info().
      target(which_button).action().has_url()) {
    return GURL();
  }
  std::string url_spec = specifics_.coalesced_notification().render_info().
              collapsed_info().target(which_button).action().url();

  return AddDefaultSchemaIfNeeded(url_spec);
}

std::string SyncedNotification::GetContainedNotificationTitle(
    int index) const {
  if (specifics_.coalesced_notification().render_info().expanded_info().
      collapsed_info_size() < index + 1)
    return std::string();

  return specifics_.coalesced_notification().render_info().expanded_info().
      collapsed_info(index).simple_collapsed_layout().heading();
}

std::string SyncedNotification::GetContainedNotificationMessage(
    int index) const {
  if (specifics_.coalesced_notification().render_info().expanded_info().
      collapsed_info_size() < index + 1)
    return std::string();

  return specifics_.coalesced_notification().render_info().expanded_info().
      collapsed_info(index).simple_collapsed_layout().description();
}

std::string SyncedNotification::GetSendingServiceId() const {
  // TODO(petewil): We are building a new protocol (a new sync datatype) to send
  // the service name and icon from the server.  For now this method is
  // hardcoded to the name of our first service using synced notifications.
  // Once the new protocol is built, remove this hardcoding.
  return kFirstSyncedNotificationServiceId;
}

}  // namespace notifier
