// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The ChromeNotifierService works together with sync to maintain the state of
// user notifications, which can then be presented in the notification center,
// via the Notification UI Manager.

#include "chrome/browser/notifications/sync_notifier/chrome_notifier_service.h"

#include <string>
#include <vector>

#include "base/command_line.h"
#include "chrome/browser/notifications/desktop_notification_service.h"
#include "chrome/browser/notifications/desktop_notification_service_factory.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/notifications/notification_ui_manager.h"
#include "chrome/browser/notifications/sync_notifier/chrome_notifier_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "sync/api/sync_change.h"
#include "sync/api/sync_change_processor.h"
#include "sync/api/sync_error_factory.h"
#include "sync/protocol/sync.pb.h"
#include "sync/protocol/synced_notification_specifics.pb.h"
#include "third_party/WebKit/public/web/WebTextDirection.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/message_center/notifier_settings.h"
#include "url/gurl.h"

namespace notifier {
namespace {

const char kFirstSyncedNotificationServiceId[] = "Google+";

}

bool ChromeNotifierService::avoid_bitmap_fetching_for_test_ = false;

ChromeNotifierService::ChromeNotifierService(Profile* profile,
                                             NotificationUIManager* manager)
    : profile_(profile), notification_manager_(manager) {
}
ChromeNotifierService::~ChromeNotifierService() {}

// Methods from BrowserContextKeyedService.
void ChromeNotifierService::Shutdown() {}

// syncer::SyncableService implementation.

// This is called at startup to sync with the server.
// This code is not thread safe.
syncer::SyncMergeResult ChromeNotifierService::MergeDataAndStartSyncing(
      syncer::ModelType type,
      const syncer::SyncDataList& initial_sync_data,
      scoped_ptr<syncer::SyncChangeProcessor> sync_processor,
      scoped_ptr<syncer::SyncErrorFactory> error_handler) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  DCHECK_EQ(syncer::SYNCED_NOTIFICATIONS, type);
  syncer::SyncMergeResult merge_result(syncer::SYNCED_NOTIFICATIONS);
  // A list of local changes to send up to the sync server.
  syncer::SyncChangeList new_changes;
  sync_processor_ = sync_processor.Pass();

  for (syncer::SyncDataList::const_iterator it = initial_sync_data.begin();
       it != initial_sync_data.end(); ++it) {
    const syncer::SyncData& sync_data = *it;
    DCHECK_EQ(syncer::SYNCED_NOTIFICATIONS, sync_data.GetDataType());

    // Build a local notification object from the sync data.
    scoped_ptr<SyncedNotification> incoming(CreateNotificationFromSyncData(
        sync_data));
    if (!incoming) {
      // TODO(petewil): Turn this into a NOTREACHED() call once we fix the
      // underlying problem causing bad data.
      LOG(WARNING) << "Badly formed sync data in incoming notification";
      continue;
    }

    // Process each incoming remote notification.
    const std::string& key = incoming->GetKey();
    DCHECK_GT(key.length(), 0U);
    SyncedNotification* found = FindNotificationById(key);

    if (NULL == found) {
      // If there are no conflicts, copy in the data from remote.
      Add(incoming.Pass());
    } else {
      // If the incoming (remote) and stored (local) notifications match
      // in all fields, we don't need to do anything here.
      if (incoming->EqualsIgnoringReadState(*found)) {

        if (incoming->GetReadState() == found->GetReadState()) {
          // Notification matches on the client and the server, nothing to do.
          continue;
        } else  {
          // If the read state is different, read wins for both places.
          if (incoming->GetReadState() == SyncedNotification::kDismissed) {
            // If it is marked as read on the server, but not the client.
            found->NotificationHasBeenDismissed();
            // Tell the Notification UI Manager to remove it.
            notification_manager_->CancelById(found->GetKey());
          } else if (incoming->GetReadState() == SyncedNotification::kRead) {
            // If it is marked as read on the server, but not the client.
            found->NotificationHasBeenRead();
            // Tell the Notification UI Manager to remove it.
            notification_manager_->CancelById(found->GetKey());
          } else {
            // If it is marked as read on the client, but not the server.
            syncer::SyncData sync_data = CreateSyncDataFromNotification(*found);
            new_changes.push_back(
                syncer::SyncChange(FROM_HERE,
                                   syncer::SyncChange::ACTION_UPDATE,
                                   sync_data));
          }
          // If local state changed, notify Notification UI Manager.
        }
      // For any other conflict besides read state, treat it as an update.
      } else {
        // If different, just replace the local with the remote.
        // TODO(petewil): Someday we may allow changes from the client to
        // flow upwards, when we do, we will need better merge resolution.
        found->Update(sync_data);

        // Tell the notification manager to update the notification.
        UpdateInMessageCenter(found);
      }
    }
  }

  // Send up the changes that were made locally.
  if (new_changes.size() > 0) {
    merge_result.set_error(sync_processor_->ProcessSyncChanges(
        FROM_HERE, new_changes));
  }

  return merge_result;
}

void ChromeNotifierService::StopSyncing(syncer::ModelType type) {
  DCHECK_EQ(syncer::SYNCED_NOTIFICATIONS, type);
  // TODO(petewil): implement
}

syncer::SyncDataList ChromeNotifierService::GetAllSyncData(
      syncer::ModelType type) const {
  DCHECK_EQ(syncer::SYNCED_NOTIFICATIONS, type);
  syncer::SyncDataList sync_data;

  // Copy our native format data into a SyncDataList format.
  ScopedVector<SyncedNotification>::const_iterator it =
      notification_data_.begin();
  for (; it != notification_data_.end(); ++it) {
    sync_data.push_back(CreateSyncDataFromNotification(**it));
  }

  return sync_data;
}

// This method is called when there is an incoming sync change from the server.
syncer::SyncError ChromeNotifierService::ProcessSyncChanges(
      const tracked_objects::Location& from_here,
      const syncer::SyncChangeList& change_list) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  syncer::SyncError error;

  for (syncer::SyncChangeList::const_iterator it = change_list.begin();
       it != change_list.end(); ++it) {
    syncer::SyncData sync_data = it->sync_data();
    DCHECK_EQ(syncer::SYNCED_NOTIFICATIONS, sync_data.GetDataType());
    syncer::SyncChange::SyncChangeType change_type = it->change_type();

    scoped_ptr<SyncedNotification> new_notification(
        CreateNotificationFromSyncData(sync_data));
    if (!new_notification.get()) {
      NOTREACHED() << "Failed to read notification.";
      continue;
    }

    const std::string& key = new_notification->GetKey();
    DCHECK_GT(key.length(), 0U);
    SyncedNotification* found = FindNotificationById(key);

    switch (change_type) {
      case syncer::SyncChange::ACTION_ADD:
        // Intentional fall through, cases are identical.
      case syncer::SyncChange::ACTION_UPDATE:
        if (found == NULL) {
          Add(new_notification.Pass());
          break;
        }
        // Update it in our store.
        found->Update(sync_data);
        // Tell the notification manager to update the notification.
        UpdateInMessageCenter(found);
        break;

      case syncer::SyncChange::ACTION_DELETE:
        if (found == NULL) {
          break;
        }
        // Remove it from our store.
        FreeNotificationById(key);
        // Remove it from the message center.
        UpdateInMessageCenter(new_notification.get());
        // TODO(petewil): Do I need to remember that it was deleted in case the
        // add arrives after the delete?  If so, how long do I need to remember?
        break;

      default:
        NOTREACHED();
        break;
    }
  }

  return error;
}

// Support functions for data type conversion.

// Static method.  Get to the sync data in our internal format.
syncer::SyncData ChromeNotifierService::CreateSyncDataFromNotification(
    const SyncedNotification& notification) {
  // Construct the sync_data using the specifics from the notification.
  return syncer::SyncData::CreateLocalData(
      notification.GetKey(), notification.GetKey(),
      notification.GetEntitySpecifics());
}

// Static Method.  Convert from SyncData to our internal format.
scoped_ptr<SyncedNotification>
    ChromeNotifierService::CreateNotificationFromSyncData(
        const syncer::SyncData& sync_data) {
  // Get a pointer to our data within the sync_data object.
  sync_pb::SyncedNotificationSpecifics specifics =
      sync_data.GetSpecifics().synced_notification();

  // Check for mandatory fields in the sync_data object.
  if (!specifics.has_coalesced_notification() ||
      !specifics.coalesced_notification().has_key() ||
      !specifics.coalesced_notification().has_read_state()) {
    DVLOG(1) << "Synced Notification missing mandatory fields "
             << "has coalesced notification? "
             << specifics.has_coalesced_notification()
             << " has key? " << specifics.coalesced_notification().has_key()
             << " has read state? "
             << specifics.coalesced_notification().has_read_state();
    return scoped_ptr<SyncedNotification>();
  }

  bool is_well_formed_unread_notification =
      (static_cast<SyncedNotification::ReadState>(
          specifics.coalesced_notification().read_state()) ==
       SyncedNotification::kUnread &&
       specifics.coalesced_notification().has_render_info());
  bool is_well_formed_read_notification =
      (static_cast<SyncedNotification::ReadState>(
          specifics.coalesced_notification().read_state()) ==
       SyncedNotification::kRead &&
       specifics.coalesced_notification().has_render_info());
  bool is_well_formed_dismissed_notification =
      (static_cast<SyncedNotification::ReadState>(
          specifics.coalesced_notification().read_state()) ==
       SyncedNotification::kDismissed);

  // If the notification is poorly formed, return a null pointer.
  if (!is_well_formed_unread_notification &&
      !is_well_formed_read_notification &&
      !is_well_formed_dismissed_notification) {
    DVLOG(1) << "Synced Notification is not well formed."
             << " unread well formed? "
             << is_well_formed_unread_notification
             << " dismissed well formed? "
             << is_well_formed_dismissed_notification
             << " read well formed? "
             << is_well_formed_read_notification;
    return scoped_ptr<SyncedNotification>();
  }

  // Create a new notification object based on the supplied sync_data.
  scoped_ptr<SyncedNotification> notification(
      new SyncedNotification(sync_data));

  return notification.Pass();
}

// This returns a pointer into a vector that we own.  Caller must not free it.
// Returns NULL if no match is found.
SyncedNotification* ChromeNotifierService::FindNotificationById(
    const std::string& notification_id) {
  // TODO(petewil): We can make a performance trade off here.
  // While the vector has good locality of reference, a map has faster lookup.
  // Based on how big we expect this to get, maybe change this to a map.
  ScopedVector<SyncedNotification>::const_iterator it =
      notification_data_.begin();
  for (; it != notification_data_.end(); ++it) {
    SyncedNotification* notification = *it;
    if (notification_id == notification->GetKey())
      return *it;
  }

  return NULL;
}

void ChromeNotifierService::FreeNotificationById(
    const std::string& notification_id) {
  ScopedVector<SyncedNotification>::iterator it = notification_data_.begin();
  for (; it != notification_data_.end(); ++it) {
    SyncedNotification* notification = *it;
    if (notification_id == notification->GetKey()) {
      notification_data_.erase(it);
      return;
    }
  }
}

void ChromeNotifierService::GetSyncedNotificationServices(
    std::vector<message_center::Notifier*>* notifiers) {
  // TODO(mukai|petewil): Check the profile's eligibility before adding the
  // sample app.

  // TODO(petewil): Really obtain the list of synced notification sending
  // services from the server and create the list of ids here.  Until then, we
  // are hardcoding the service names.  Once that is done, remove this
  // hardcoding.
  // crbug.com/248337
  DesktopNotificationService* desktop_notification_service =
      DesktopNotificationServiceFactory::GetForProfile(profile_);
  message_center::NotifierId notifier_id(
      message_center::NotifierId::SYNCED_NOTIFICATION_SERVICE,
      kFirstSyncedNotificationServiceId);
  message_center::Notifier* notifier_service = new message_center::Notifier(
      notifier_id,
      l10n_util::GetStringUTF16(
          IDS_FIRST_SYNCED_NOTIFICATION_SERVICE_NAME),
      desktop_notification_service->IsNotifierEnabled(notifier_id));

  // Add icons for our sending services.
  // TODO(petewil): Replace this temporary hardcoding with a new sync datatype
  // to dynamically get the name and icon for each synced notification sending
  // service.  Until then, we use hardcoded service icons for all services.
  // crbug.com/248337
  notifier_service->icon = ui::ResourceBundle::GetSharedInstance().
      GetImageNamed(IDR_TEMPORARY_GOOGLE_PLUS_ICON);

  notifiers->push_back(notifier_service);
}

void ChromeNotifierService::MarkNotificationAsRead(
    const std::string& key) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  SyncedNotification* notification = FindNotificationById(key);
  CHECK(notification != NULL);

  notification->NotificationHasBeenRead();
  syncer::SyncChangeList new_changes;

  syncer::SyncData sync_data = CreateSyncDataFromNotification(*notification);
  new_changes.push_back(
      syncer::SyncChange(FROM_HERE,
                         syncer::SyncChange::ACTION_UPDATE,
                         sync_data));

  // Send up the changes that were made locally.
  sync_processor_->ProcessSyncChanges(FROM_HERE, new_changes);
}

// Add a new notification to our data structure.  This takes ownership
// of the passed in pointer.
void ChromeNotifierService::Add(scoped_ptr<SyncedNotification> notification) {
  SyncedNotification* notification_copy = notification.get();
  // Take ownership of the object and put it into our local storage.
  notification_data_.push_back(notification.release());

  // If the user is not interested in this type of notification, ignore it.
  std::vector<std::string>::iterator iter =
      find(enabled_sending_services_.begin(),
           enabled_sending_services_.end(),
           notification_copy->GetSendingServiceId());
  if (iter == enabled_sending_services_.end()) {
    return;
  }

  UpdateInMessageCenter(notification_copy);
}

void ChromeNotifierService::AddForTest(
    scoped_ptr<notifier::SyncedNotification> notification) {
  notification_data_.push_back(notification.release());
}

void ChromeNotifierService::UpdateInMessageCenter(
    SyncedNotification* notification) {
  // If the feature is disabled, exit now.
  if (!notifier::ChromeNotifierServiceFactory::UseSyncedNotifications(
      CommandLine::ForCurrentProcess()))
    return;

  notification->LogNotification();

  if (notification->GetReadState() == SyncedNotification::kUnread) {
    // If the message is unread, update it.
    Display(notification);
  } else {
    // If the message is read or deleted, dismiss it from the center.
    // We intentionally ignore errors if it is not in the center.
    notification_manager_->CancelById(notification->GetKey());
  }
}

void ChromeNotifierService::Display(SyncedNotification* notification) {
  // Set up to fetch the bitmaps.
  notification->QueueBitmapFetchJobs(notification_manager_,
                                     this,
                                     profile_);

  // Our tests cannot use the network for reliability reasons.
  if (avoid_bitmap_fetching_for_test_) {
    return;
  }

  // Start the bitmap fetching, Show() will be called when the last bitmap
  // either arrives or times out.
  notification->StartBitmapFetch();
}

void ChromeNotifierService::OnSyncedNotificationServiceEnabled(
    const std::string& notifier_id, bool enabled) {
  std::vector<std::string>::iterator iter;

  iter = find(enabled_sending_services_.begin(),
              enabled_sending_services_.end(),
              notifier_id);

  // Add the notifier_id if it is enabled and not already there.
  if (iter == enabled_sending_services_.end() && enabled) {
    enabled_sending_services_.push_back(notifier_id);
    // TODO(petewil) Check now for any outstanding notifications.
  // Remove the notifier_id if it is disabled and present.
  } else if (iter != enabled_sending_services_.end() && !enabled) {
    enabled_sending_services_.erase(iter);
  }

  // Otherwise, nothing to do, we can exit.
  return;
}

}  // namespace notifier
