// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/file_manager/desktop_notifications.h"

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chromeos/extensions/file_manager/file_manager_util.h"
#include "chrome/browser/notifications/desktop_notification_service.h"
#include "chrome/browser/notifications/notification_delegate.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

namespace file_manager {
namespace {

struct NotificationTypeInfo {
  DesktopNotifications::NotificationType type;
  const char* notification_id_prefix;
  int icon_id;
  int title_id;
  int message_id;
};

// Information about notification types.
// The order of notification types in the array must match the order of types in
// NotificationType enum (i.e. the following MUST be satisfied:
// kNotificationTypes[type].type == type).
const NotificationTypeInfo kNotificationTypes[] = {
  {
    DesktopNotifications::DEVICE,  // type
    "Device_",  // notification_id_prefix
    IDR_FILES_APP_ICON,  // icon_id
    IDS_REMOVABLE_DEVICE_DETECTION_TITLE,  // title_id
    IDS_REMOVABLE_DEVICE_SCANNING_MESSAGE  // message_id
  },
  {
    DesktopNotifications::DEVICE_FAIL,  // type
    "DeviceFail_",  // notification_id_prefix
    IDR_FILES_APP_ICON,  // icon_id
    IDS_REMOVABLE_DEVICE_DETECTION_TITLE,  // title_id
    IDS_DEVICE_UNSUPPORTED_DEFAULT_MESSAGE  // message_id
  },
  {
    DesktopNotifications::DEVICE_EXTERNAL_STORAGE_DISABLED,  // type
    "DeviceFail_",  // nottification_id_prefix; same as for DEVICE_FAIL.
    IDR_FILES_APP_ICON,  // icon_id
    IDS_REMOVABLE_DEVICE_DETECTION_TITLE,  // title_id
    IDS_EXTERNAL_STORAGE_DISABLED_MESSAGE  // message_id
  },
  {
    DesktopNotifications::FORMAT_START,  // type
    "FormatStart_",  // notification_id_prefix
    IDR_FILES_APP_ICON,  // icon_id
    IDS_FORMATTING_OF_DEVICE_PENDING_TITLE,  // title_id
    IDS_FORMATTING_OF_DEVICE_PENDING_MESSAGE  // message_id
  },
  {
    DesktopNotifications::FORMAT_START_FAIL,  // type
    "FormatComplete_",  // notification_id_prefix
    IDR_FILES_APP_ICON,  // icon_id
    IDS_FORMATTING_OF_DEVICE_FAILED_TITLE,  // title_id
    IDS_FORMATTING_STARTED_FAILURE_MESSAGE  // message_id
  },
  {
    DesktopNotifications::FORMAT_SUCCESS,  // type
    "FormatComplete_",  // notification_id_prefix
    IDR_FILES_APP_ICON,  // icon_id
    IDS_FORMATTING_OF_DEVICE_FINISHED_TITLE,  // title_id
    IDS_FORMATTING_FINISHED_SUCCESS_MESSAGE  // message_id
  },
  {
    DesktopNotifications::FORMAT_FAIL,  // type
    "FormatComplete_",  // notifications_id_prefix
    IDR_FILES_APP_ICON,  // icon_id
    IDS_FORMATTING_OF_DEVICE_FAILED_TITLE,  // title_id
    IDS_FORMATTING_FINISHED_FAILURE_MESSAGE  // message_id
  },
};

int GetIconId(DesktopNotifications::NotificationType type) {
  DCHECK_GE(type, 0);
  DCHECK_LT(static_cast<size_t>(type), arraysize(kNotificationTypes));
  DCHECK(kNotificationTypes[type].type == type);

  return kNotificationTypes[type].icon_id;
}

string16 GetTitle(DesktopNotifications::NotificationType type) {
  DCHECK_GE(type, 0);
  DCHECK_LT(static_cast<size_t>(type), arraysize(kNotificationTypes));
  DCHECK(kNotificationTypes[type].type == type);

  int id = kNotificationTypes[type].title_id;
  if (id < 0)
    return string16();
  return l10n_util::GetStringUTF16(id);
}

string16 GetMessage(DesktopNotifications::NotificationType type) {
  DCHECK_GE(type, 0);
  DCHECK_LT(static_cast<size_t>(type), arraysize(kNotificationTypes));
  DCHECK(kNotificationTypes[type].type == type);

  int id = kNotificationTypes[type].message_id;
  if (id < 0)
    return string16();
  return l10n_util::GetStringUTF16(id);
}

std::string GetNotificationId(DesktopNotifications::NotificationType type,
                              const std::string& path) {
  DCHECK_GE(type, 0);
  DCHECK_LT(static_cast<size_t>(type), arraysize(kNotificationTypes));
  DCHECK(kNotificationTypes[type].type == type);

  std::string id_prefix(kNotificationTypes[type].notification_id_prefix);
  return id_prefix.append(path);
}

}  // namespace

// Manages file browser notifications. Generates a desktop notification on
// construction and removes it from the host when closed. Owned by the host.
class DesktopNotifications::NotificationMessage {
 public:
  class Delegate : public NotificationDelegate {
   public:
    Delegate(const base::WeakPtr<DesktopNotifications>& host,
             const std::string& id)
        : host_(host),
          id_(id) {}
    virtual void Display() OVERRIDE {}
    virtual void Error() OVERRIDE {}
    virtual void Close(bool by_user) OVERRIDE {
      if (host_)
        host_->RemoveNotificationById(id_);
    }
    virtual void Click() OVERRIDE {
      // TODO(tbarzic): Show more info page once we have one.
    }
    virtual std::string id() const OVERRIDE { return id_; }
    virtual content::RenderViewHost* GetRenderViewHost() const OVERRIDE {
      return NULL;
    }

   private:
    virtual ~Delegate() {}

    base::WeakPtr<DesktopNotifications> host_;
    std::string id_;

    DISALLOW_COPY_AND_ASSIGN(Delegate);
  };

  NotificationMessage(DesktopNotifications* host,
                      Profile* profile,
                      NotificationType type,
                      const std::string& notification_id,
                      const string16& message)
      : message_(message) {
    const gfx::Image& icon =
        ResourceBundle::GetSharedInstance().GetNativeImageNamed(
            GetIconId(type));
    // TODO(mukai): refactor here to invoke NotificationUIManager directly.
    const string16 replace_id = UTF8ToUTF16(notification_id);
    DesktopNotificationService::AddIconNotification(
        util::GetFileBrowserExtensionUrl(), GetTitle(type),
        message, icon, replace_id,
        new Delegate(host->AsWeakPtr(), notification_id), profile);
  }

  ~NotificationMessage() {}

  // Used in test.
  string16 message() { return message_; }

 private:
  string16 message_;

  DISALLOW_COPY_AND_ASSIGN(NotificationMessage);
};

struct DesktopNotifications::MountRequestsInfo {
  bool mount_success_exists;
  bool fail_message_finalized;
  bool fail_notification_shown;
  bool non_parent_device_failed;
  bool device_notification_hidden;

  MountRequestsInfo() : mount_success_exists(false),
                        fail_message_finalized(false),
                        fail_notification_shown(false),
                        non_parent_device_failed(false),
                        device_notification_hidden(false) {
  }
};

DesktopNotifications::DesktopNotifications(Profile* profile)
    : profile_(profile) {
}

DesktopNotifications::~DesktopNotifications() {
  STLDeleteContainerPairSecondPointers(notification_map_.begin(),
                                       notification_map_.end());
}

void DesktopNotifications::RegisterDevice(const std::string& path) {
  mount_requests_.insert(MountRequestsMap::value_type(path,
                                                      MountRequestsInfo()));
}

void DesktopNotifications::UnregisterDevice(const std::string& path) {
  mount_requests_.erase(path);
}

void DesktopNotifications::ManageNotificationsOnMountCompleted(
    const std::string& system_path, const std::string& label, bool is_parent,
    bool success, bool is_unsupported) {
  MountRequestsMap::iterator it = mount_requests_.find(system_path);
  if (it == mount_requests_.end())
    return;

  // We have to hide device scanning notification if we haven't done it already.
  if (!it->second.device_notification_hidden) {
    HideNotification(DEVICE, system_path);
    it->second.device_notification_hidden = true;
  }

  // Check if there is fail notification for parent device. If so, disregard it.
  // (parent device contains partition table, which is unmountable).
  if (!is_parent && it->second.fail_notification_shown &&
      !it->second.non_parent_device_failed) {
    HideNotification(DEVICE_FAIL, system_path);
    it->second.fail_notification_shown = false;
  }

  // If notification can't change any more, no need to continue.
  if (it->second.fail_message_finalized)
    return;

  // Do we have a multi-partition device for which at least one mount failed.
  bool fail_on_multipartition_device =
      success ? it->second.non_parent_device_failed
      : it->second.mount_success_exists ||
      it->second.non_parent_device_failed;

  base::string16 message;
  if (fail_on_multipartition_device) {
    it->second.fail_message_finalized = true;
    message = label.empty() ?
        l10n_util::GetStringUTF16(
            IDS_MULTIPART_DEVICE_UNSUPPORTED_DEFAULT_MESSAGE) :
        l10n_util::GetStringFUTF16(
            IDS_MULTIPART_DEVICE_UNSUPPORTED_MESSAGE, UTF8ToUTF16(label));
  } else if (!success) {
    // First device failed.
    if (!is_unsupported) {
      message = label.empty() ?
          l10n_util::GetStringUTF16(IDS_DEVICE_UNKNOWN_DEFAULT_MESSAGE) :
          l10n_util::GetStringFUTF16(IDS_DEVICE_UNKNOWN_MESSAGE,
                                     UTF8ToUTF16(label));
    } else {
      message = label.empty() ?
          l10n_util::GetStringUTF16(IDS_DEVICE_UNSUPPORTED_DEFAULT_MESSAGE) :
          l10n_util::GetStringFUTF16(IDS_DEVICE_UNSUPPORTED_MESSAGE,
                                     UTF8ToUTF16(label));
    }
  }

  if (success) {
    it->second.mount_success_exists = true;
  } else {
    it->second.non_parent_device_failed |= !is_parent;
  }

  if (message.empty())
    return;

  if (it->second.fail_notification_shown) {
    HideNotification(DEVICE_FAIL, system_path);
  } else {
    it->second.fail_notification_shown = true;
  }

  ShowNotificationWithMessage(DEVICE_FAIL, system_path, message);
}

void DesktopNotifications::ShowNotification(NotificationType type,
                                            const std::string& path) {
  ShowNotificationWithMessage(type, path, GetMessage(type));
}

void DesktopNotifications::ShowNotificationWithMessage(
    NotificationType type,
    const std::string& path,
    const string16& message) {
  std::string notification_id = GetNotificationId(type, path);
  hidden_notifications_.erase(notification_id);
  ShowNotificationById(type, notification_id, message);
}

void DesktopNotifications::ShowNotificationDelayed(
    NotificationType type,
    const std::string& path,
    base::TimeDelta delay) {
  std::string notification_id = GetNotificationId(type, path);
  hidden_notifications_.erase(notification_id);
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&DesktopNotifications::ShowNotificationById, AsWeakPtr(),
                 type, notification_id, GetMessage(type)),
      delay);
}

void DesktopNotifications::HideNotification(NotificationType type,
                                            const std::string& path) {
  std::string notification_id = GetNotificationId(type, path);
  HideNotificationById(notification_id);
}

void DesktopNotifications::HideNotificationDelayed(
    NotificationType type, const std::string& path, base::TimeDelta delay) {
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&DesktopNotifications::HideNotification, AsWeakPtr(),
                 type, path),
      delay);
}

void DesktopNotifications::ShowNotificationById(
    NotificationType type,
    const std::string& notification_id,
    const string16& message) {
  if (hidden_notifications_.find(notification_id) !=
      hidden_notifications_.end()) {
    // Notification was hidden after a delayed show was requested.
    hidden_notifications_.erase(notification_id);
    return;
  }
  if (notification_map_.find(notification_id) != notification_map_.end()) {
    // Remove any existing notification with |notification_id|.
    // Will trigger Delegate::Close which will call RemoveNotificationById.
    DesktopNotificationService::RemoveNotification(notification_id);
    DCHECK(notification_map_.find(notification_id) == notification_map_.end());
  }
  // Create a new notification with |notification_id|.
  NotificationMessage* new_message =
      new NotificationMessage(this, profile_, type, notification_id, message);
  notification_map_[notification_id] = new_message;
}

void DesktopNotifications::HideNotificationById(
    const std::string& notification_id) {
  NotificationMap::iterator it = notification_map_.find(notification_id);
  if (it != notification_map_.end()) {
    // Will trigger Delegate::Close which will call RemoveNotificationById.
    DesktopNotificationService::RemoveNotification(notification_id);
  } else {
    // Mark as hidden so it does not get shown from a delayed task.
    hidden_notifications_.insert(notification_id);
  }
}

void DesktopNotifications::RemoveNotificationById(
    const std::string& notification_id) {
  NotificationMap::iterator it = notification_map_.find(notification_id);
  if (it != notification_map_.end()) {
    NotificationMessage* notification = it->second;
    notification_map_.erase(it);
    delete notification;
  }
}

string16 DesktopNotifications::GetNotificationMessageForTest(
    const std::string& id) const {
  NotificationMap::const_iterator it = notification_map_.find(id);
  if (it == notification_map_.end())
    return string16();
  return it->second->message();
}

}  // namespace file_manager
