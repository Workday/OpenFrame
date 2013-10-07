// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/notifications/notifications_api.h"

#include "base/callback.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/event_names.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/notifications/desktop_notification_service.h"
#include "chrome/browser/notifications/desktop_notification_service_factory.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/notifications/notification_ui_manager.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/features/feature.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/message_center/message_center_style.h"
#include "ui/message_center/message_center_util.h"
#include "ui/message_center/notifier_settings.h"
#include "url/gurl.h"

namespace extensions {

namespace {

const char kResultKey[] = "result";
const char kMissingRequiredPropertiesForCreateNotification[] =
    "Some of the required properties are missing: type, iconUrl, title and "
    "message.";
const char kUnexpectedProgressValueForNonProgressType[] =
    "The progress value should not be specified for non-progress notification";
const char kInvalidProgressValue[] =
    "The progress value should range from 0 to 100";

// Converts an object with width, height, and data in RGBA format into an
// gfx::Image (in ARGB format).
bool NotificationBitmapToGfxImage(
    api::notifications::NotificationBitmap* notification_bitmap,
    gfx::Image* return_image) {
  if (!notification_bitmap)
    return false;

  // Ensure a sane set of dimensions.
  const int max_width = message_center::kNotificationPreferredImageSize;
  const int max_height =
      message_center::kNotificationPreferredImageRatio * max_width;
  const int BYTES_PER_PIXEL = 4;

  const int width = notification_bitmap->width;
  const int height = notification_bitmap->height;

  if (width < 0 || height < 0 || width > max_width || height > max_height)
    return false;

  // Ensure we have rgba data.
  std::string* rgba_data = notification_bitmap->data.get();
  if (!rgba_data)
    return false;

  const size_t rgba_data_length = rgba_data->length();
  const size_t rgba_area = width * height;

  if (rgba_data_length != rgba_area * BYTES_PER_PIXEL)
    return false;

  // Now configure the bitmap with the sanitized dimensions.
  SkBitmap bitmap;
  bitmap.setConfig(SkBitmap::kARGB_8888_Config, width, height);

  // Allocate the actual backing store.
  if (!bitmap.allocPixels())
    return false;

  // Ensure that our bitmap and our data now refer to the same number of pixels.
  if (rgba_data_length != bitmap.getSafeSize())
    return false;

  uint32_t* pixels = bitmap.getAddr32(0, 0);
  const char* c_rgba_data = rgba_data->data();

  for (size_t t = 0; t < rgba_area; ++t) {
    // |c_rgba_data| is RGBA, pixels is ARGB.
    size_t rgba_index = t * BYTES_PER_PIXEL;
    pixels[t] = SkPreMultiplyColor(
        ((c_rgba_data[rgba_index + 3] & 0xFF) << 24) |
        ((c_rgba_data[rgba_index + 0] & 0xFF) << 16) |
        ((c_rgba_data[rgba_index + 1] & 0xFF) << 8) |
        ((c_rgba_data[rgba_index + 2] & 0xFF) << 0));
  }

  // TODO(dewittj): Handle HiDPI images.
  ui::ScaleFactor scale_factor(ui::SCALE_FACTOR_100P);
  gfx::ImageSkia skia(gfx::ImageSkiaRep(bitmap, scale_factor));
  *return_image = gfx::Image(skia);
  return true;
}

// Given an extension id and another id, returns an id that is unique
// relative to other extensions.
std::string CreateScopedIdentifier(const std::string& extension_id,
                                   const std::string& id) {
  return extension_id + "-" + id;
}

// Removes the unique internal identifier to send the ID as the
// extension expects it.
std::string StripScopeFromIdentifier(const std::string& extension_id,
                                     const std::string& id) {
  size_t index_of_separator = extension_id.length() + 1;
  DCHECK_LT(index_of_separator, id.length());

  return id.substr(index_of_separator);
}

class NotificationsApiDelegate : public NotificationDelegate {
 public:
  NotificationsApiDelegate(ApiFunction* api_function,
                           Profile* profile,
                           const std::string& extension_id,
                           const std::string& id)
      : api_function_(api_function),
        profile_(profile),
        extension_id_(extension_id),
        id_(id),
        scoped_id_(CreateScopedIdentifier(extension_id, id)),
        process_id_(-1) {
    DCHECK(api_function_.get());
    if (api_function_->render_view_host())
      process_id_ = api_function->render_view_host()->GetProcess()->GetID();
  }

  virtual void Display() OVERRIDE { }

  virtual void Error() OVERRIDE {
    scoped_ptr<base::ListValue> args(CreateBaseEventArgs());
    SendEvent(event_names::kOnNotificationError, args.Pass());
  }

  virtual void Close(bool by_user) OVERRIDE {
    scoped_ptr<base::ListValue> args(CreateBaseEventArgs());
    args->Append(Value::CreateBooleanValue(by_user));
    SendEvent(event_names::kOnNotificationClosed, args.Pass());
  }

  virtual void Click() OVERRIDE {
    scoped_ptr<base::ListValue> args(CreateBaseEventArgs());
    SendEvent(event_names::kOnNotificationClicked, args.Pass());
  }

  virtual bool HasClickedListener() OVERRIDE {
    return ExtensionSystem::Get(profile_)->event_router()->HasEventListener(
        event_names::kOnNotificationClicked);
  }

  virtual void ButtonClick(int index) OVERRIDE {
    scoped_ptr<base::ListValue> args(CreateBaseEventArgs());
    args->Append(Value::CreateIntegerValue(index));
    SendEvent(event_names::kOnNotificationButtonClicked, args.Pass());
  }

  virtual std::string id() const OVERRIDE {
    return scoped_id_;
  }

  virtual int process_id() const OVERRIDE {
    return process_id_;
  }

  virtual content::RenderViewHost* GetRenderViewHost() const OVERRIDE {
    // We're holding a reference to api_function_, so we know it'll be valid
    // until ReleaseRVH is called, and api_function_ (as a
    // UIThreadExtensionFunction) will zero out its copy of render_view_host
    // when the RVH goes away.
    if (!api_function_.get())
      return NULL;
    return api_function_->render_view_host();
  }

  virtual void ReleaseRenderViewHost() OVERRIDE {
    api_function_ = NULL;
  }

 private:
  virtual ~NotificationsApiDelegate() {}

  void SendEvent(const std::string& name, scoped_ptr<base::ListValue> args) {
    scoped_ptr<Event> event(new Event(name, args.Pass()));
    ExtensionSystem::Get(profile_)->event_router()->DispatchEventToExtension(
        extension_id_, event.Pass());
  }

  scoped_ptr<base::ListValue> CreateBaseEventArgs() {
    scoped_ptr<base::ListValue> args(new base::ListValue());
    args->Append(Value::CreateStringValue(id_));
    return args.Pass();
  }

  scoped_refptr<ApiFunction> api_function_;
  Profile* profile_;
  const std::string extension_id_;
  const std::string id_;
  const std::string scoped_id_;
  int process_id_;

  DISALLOW_COPY_AND_ASSIGN(NotificationsApiDelegate);
};

}  // namespace

bool NotificationsApiFunction::IsNotificationsApiAvailable() {
  // We need to check this explicitly rather than letting
  // _permission_features.json enforce it, because we're sharing the
  // chrome.notifications permissions namespace with WebKit notifications.
  return GetExtension()->is_platform_app() || GetExtension()->is_extension();
}

NotificationsApiFunction::NotificationsApiFunction() {
}

NotificationsApiFunction::~NotificationsApiFunction() {
}

bool NotificationsApiFunction::CreateNotification(
    const std::string& id,
    api::notifications::NotificationOptions* options) {
  // First, make sure the required fields exist: type, title, message,  icon.
  // These fields are defined as optional in IDL such that they can be used as
  // optional for notification updates. But for notification creations, they
  // should be present.
  if (options->type == api::notifications::TEMPLATE_TYPE_NONE ||
      !options->icon_url || !options->title || !options->message) {
    SetError(kMissingRequiredPropertiesForCreateNotification);
    return false;
  }

  // Extract required fields: type, title, message, and icon.
  message_center::NotificationType type =
      MapApiTemplateTypeToType(options->type);
  const string16 title(UTF8ToUTF16(*options->title));
  const string16 message(UTF8ToUTF16(*options->message));
  gfx::Image icon;

  // TODO(dewittj): Return error if this fails.
  NotificationBitmapToGfxImage(options->icon_bitmap.get(), &icon);

  // Then, handle any optional data that's been provided.
  message_center::RichNotificationData optional_fields;
  if (message_center::IsRichNotificationEnabled()) {
    if (options->priority.get())
      optional_fields.priority = *options->priority;

    if (options->event_time.get())
      optional_fields.timestamp = base::Time::FromJsTime(*options->event_time);

    if (options->buttons.get()) {
      // Currently we allow up to 2 buttons.
      size_t number_of_buttons = options->buttons->size();
      number_of_buttons = number_of_buttons > 2 ? 2 : number_of_buttons;

      for (size_t i = 0; i < number_of_buttons; i++) {
        message_center::ButtonInfo info(
            UTF8ToUTF16((*options->buttons)[i]->title));
        NotificationBitmapToGfxImage((*options->buttons)[i]->icon_bitmap.get(),
                                     &info.icon);
        optional_fields.buttons.push_back(info);
      }
    }

    if (options->expanded_message.get()) {
      optional_fields.expanded_message =
          UTF8ToUTF16(*options->expanded_message);
    }

    bool has_image = NotificationBitmapToGfxImage(options->image_bitmap.get(),
                                                  &optional_fields.image);
    // We should have an image if and only if the type is an image type.
    if (has_image != (type == message_center::NOTIFICATION_TYPE_IMAGE))
      return false;

    // We should have list items if and only if the type is a multiple type.
    bool has_list_items = options->items.get() && options->items->size() > 0;
    if (has_list_items != (type == message_center::NOTIFICATION_TYPE_MULTIPLE))
      return false;

    if (options->progress.get() != NULL) {
      // We should have progress if and only if the type is a progress type.
      if (type != message_center::NOTIFICATION_TYPE_PROGRESS) {
        SetError(kUnexpectedProgressValueForNonProgressType);
        return false;
      }
      optional_fields.progress = *options->progress;
      // Progress value should range from 0 to 100.
      if (optional_fields.progress < 0 || optional_fields.progress > 100) {
        SetError(kInvalidProgressValue);
        return false;
      }
    }

    if (has_list_items) {
      using api::notifications::NotificationItem;
      std::vector<linked_ptr<NotificationItem> >::iterator i;
      for (i = options->items->begin(); i != options->items->end(); ++i) {
        message_center::NotificationItem item(UTF8ToUTF16(i->get()->title),
                                              UTF8ToUTF16(i->get()->message));
        optional_fields.items.push_back(item);
      }
    }
  }

  NotificationsApiDelegate* api_delegate(new NotificationsApiDelegate(
      this,
      profile(),
      extension_->id(),
      id));  // ownership is passed to Notification
  Notification notification(type,
                            extension_->url(),
                            title,
                            message,
                            icon,
                            WebKit::WebTextDirectionDefault,
                            UTF8ToUTF16(extension_->name()),
                            UTF8ToUTF16(api_delegate->id()),
                            optional_fields,
                            api_delegate);

  g_browser_process->notification_ui_manager()->Add(notification, profile());
  return true;
}

bool NotificationsApiFunction::UpdateNotification(
    const std::string& id,
    api::notifications::NotificationOptions* options,
    Notification* notification) {
  // Update optional fields if provided.
  if (options->type != api::notifications::TEMPLATE_TYPE_NONE)
    notification->set_type(MapApiTemplateTypeToType(options->type));
  if (options->title)
    notification->set_title(UTF8ToUTF16(*options->title));
  if (options->message)
    notification->set_message(UTF8ToUTF16(*options->message));

  // TODO(dewittj): Return error if this fails.
  if (options->icon_bitmap) {
    gfx::Image icon;
    NotificationBitmapToGfxImage(options->icon_bitmap.get(), &icon);
    notification->set_icon(icon);
  }

  message_center::RichNotificationData optional_fields;
  if (message_center::IsRichNotificationEnabled()) {
    if (options->priority)
      notification->set_priority(*options->priority);

    if (options->event_time)
      notification->set_timestamp(base::Time::FromJsTime(*options->event_time));

    if (options->buttons) {
      // Currently we allow up to 2 buttons.
      size_t number_of_buttons = options->buttons->size();
      number_of_buttons = number_of_buttons > 2 ? 2 : number_of_buttons;

      for (size_t i = 0; i < number_of_buttons; i++) {
        message_center::ButtonInfo info(
            UTF8ToUTF16((*options->buttons)[i]->title));
        NotificationBitmapToGfxImage((*options->buttons)[i]->icon_bitmap.get(),
                                     &info.icon);
        optional_fields.buttons.push_back(info);
      }
    }

    if (options->expanded_message) {
      notification->set_expanded_message(
          UTF8ToUTF16(*options->expanded_message));
    }

    gfx::Image image;
    if (NotificationBitmapToGfxImage(options->image_bitmap.get(), &image)) {
      // We should have an image if and only if the type is an image type.
      if (notification->type() != message_center::NOTIFICATION_TYPE_IMAGE)
        return false;
      notification->set_image(image);
    }

    if (options->progress) {
      // We should have progress if and only if the type is a progress type.
      if (notification->type() != message_center::NOTIFICATION_TYPE_PROGRESS) {
        SetError(kUnexpectedProgressValueForNonProgressType);
        return false;
      }
      int progress = *options->progress;
      // Progress value should range from 0 to 100.
      if (progress < 0 || progress > 100) {
        SetError(kInvalidProgressValue);
        return false;
      }
      notification->set_progress(progress);
    }

    if (options->items.get() && options->items->size() > 0) {
      // We should have list items if and only if the type is a multiple type.
      if (notification->type() != message_center::NOTIFICATION_TYPE_MULTIPLE)
        return false;

      std::vector< message_center::NotificationItem> items;
      using api::notifications::NotificationItem;
      std::vector<linked_ptr<NotificationItem> >::iterator i;
      for (i = options->items->begin(); i != options->items->end(); ++i) {
        message_center::NotificationItem item(UTF8ToUTF16(i->get()->title),
                                              UTF8ToUTF16(i->get()->message));
        items.push_back(item);
      }
      notification->set_items(items);
    }
  }

  g_browser_process->notification_ui_manager()->Update(
      *notification, profile());
  return true;
}

bool NotificationsApiFunction::IsNotificationsApiEnabled() {
  DesktopNotificationService* service =
      DesktopNotificationServiceFactory::GetForProfile(profile());
  return service->IsNotifierEnabled(message_center::NotifierId(
      message_center::NotifierId::APPLICATION, extension_->id()));
}

bool NotificationsApiFunction::RunImpl() {
  if (IsNotificationsApiAvailable() && IsNotificationsApiEnabled()) {
    return RunNotificationsApi();
  } else {
    SendResponse(false);
    return true;
  }
}

message_center::NotificationType
NotificationsApiFunction::MapApiTemplateTypeToType(
    api::notifications::TemplateType type) {
  switch (type) {
    case api::notifications::TEMPLATE_TYPE_NONE:
    case api::notifications::TEMPLATE_TYPE_BASIC:
      return message_center::NOTIFICATION_TYPE_BASE_FORMAT;
    case api::notifications::TEMPLATE_TYPE_IMAGE:
      return message_center::NOTIFICATION_TYPE_IMAGE;
    case api::notifications::TEMPLATE_TYPE_LIST:
      return message_center::NOTIFICATION_TYPE_MULTIPLE;
    case api::notifications::TEMPLATE_TYPE_PROGRESS:
      return message_center::NOTIFICATION_TYPE_PROGRESS;
    default:
      // Gracefully handle newer application code that is running on an older
      // runtime that doesn't recognize the requested template.
      return message_center::NOTIFICATION_TYPE_BASE_FORMAT;
  }
}

const char kNotificationPrefix[] = "extensions.api.";

static uint64 next_id_ = 0;

NotificationsCreateFunction::NotificationsCreateFunction() {
}

NotificationsCreateFunction::~NotificationsCreateFunction() {
}

bool NotificationsCreateFunction::RunNotificationsApi() {
  params_ = api::notifications::Create::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params_.get());

  // If the caller provided a notificationId, use that. Otherwise, generate
  // one. Note that there's nothing stopping an app developer from passing in
  // arbitrary "extension.api.999" notificationIds that will collide with
  // future generated IDs. It doesn't seem necessary to try to prevent this; if
  // developers want to hurt themselves, we'll let them.
  const std::string extension_id(extension_->id());
  std::string notification_id;
  if (!params_->notification_id.empty())
    notification_id = params_->notification_id;
  else
    notification_id = kNotificationPrefix + base::Uint64ToString(next_id_++);

  SetResult(Value::CreateStringValue(notification_id));

  // TODO(dewittj): Add more human-readable error strings if this fails.
  if (!CreateNotification(notification_id, &params_->options))
    return false;

  SendResponse(true);

  return true;
}

NotificationsUpdateFunction::NotificationsUpdateFunction() {
}

NotificationsUpdateFunction::~NotificationsUpdateFunction() {
}

bool NotificationsUpdateFunction::RunNotificationsApi() {
  params_ = api::notifications::Update::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params_.get());

  // We are in update.  If the ID doesn't exist, succeed but call the callback
  // with "false".
  const Notification* matched_notification =
      g_browser_process->notification_ui_manager()->FindById(
          CreateScopedIdentifier(extension_->id(), params_->notification_id));
  if (!matched_notification) {
    SetResult(Value::CreateBooleanValue(false));
    SendResponse(true);
    return true;
  }

  // If we have trouble updating the notification (could be improper use of API
  // or some other reason), mark the function as failed, calling the callback
  // with false.
  // TODO(dewittj): Add more human-readable error strings if this fails.
  Notification notification = *matched_notification;
  bool could_update_notification = UpdateNotification(
      params_->notification_id, &params_->options, &notification);
  SetResult(Value::CreateBooleanValue(could_update_notification));
  if (!could_update_notification)
    return false;

  // No trouble, created the notification, send true to the callback and
  // succeed.
  SendResponse(true);
  return true;
}

NotificationsClearFunction::NotificationsClearFunction() {
}

NotificationsClearFunction::~NotificationsClearFunction() {
}

bool NotificationsClearFunction::RunNotificationsApi() {
  params_ = api::notifications::Clear::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params_.get());

  bool cancel_result = g_browser_process->notification_ui_manager()->CancelById(
      CreateScopedIdentifier(extension_->id(), params_->notification_id));

  SetResult(Value::CreateBooleanValue(cancel_result));
  SendResponse(true);

  return true;
}

NotificationsGetAllFunction::NotificationsGetAllFunction() {}

NotificationsGetAllFunction::~NotificationsGetAllFunction() {}

bool NotificationsGetAllFunction::RunNotificationsApi() {
  NotificationUIManager* notification_ui_manager =
      g_browser_process->notification_ui_manager();
  std::set<std::string> notification_ids =
      notification_ui_manager->GetAllIdsByProfileAndSourceOrigin(
          profile_, extension_->url());

  scoped_ptr<base::DictionaryValue> result(new base::DictionaryValue());

  for (std::set<std::string>::iterator iter = notification_ids.begin();
       iter != notification_ids.end(); iter++) {
    result->SetBooleanWithoutPathExpansion(
        StripScopeFromIdentifier(extension_->id(), *iter), true);
  }

  SetResult(result.release());
  SendResponse(true);

  return true;
}

}  // namespace extensions
