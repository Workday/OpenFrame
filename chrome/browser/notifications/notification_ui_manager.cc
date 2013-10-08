// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/notification_ui_manager.h"

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/notifications/balloon_notification_ui_manager.h"
#include "chrome/browser/notifications/message_center_notification_manager.h"
#include "chrome/browser/notifications/message_center_settings_controller.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_info_cache.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "ui/message_center/message_center_util.h"

// static
bool NotificationUIManager::DelegatesToMessageCenter() {
  // In ChromeOS, it always uses MessageCenterNotificationManager. The flag of
  // --enable-rich-notifications switches the contents and behaviors inside of
  // the message center.
#if defined(OS_CHROMEOS)
  return true;
#endif
  return message_center::IsRichNotificationEnabled();
}

#if !defined(OS_MACOSX)
// static
NotificationUIManager* NotificationUIManager::Create(PrefService* local_state) {
  if (DelegatesToMessageCenter()) {
    ProfileInfoCache* profile_info_cache =
        &g_browser_process->profile_manager()->GetProfileInfoCache();
    scoped_ptr<message_center::NotifierSettingsProvider> settings_provider(
        new MessageCenterSettingsController(profile_info_cache));
    return new MessageCenterNotificationManager(
        g_browser_process->message_center(),
        local_state,
        settings_provider.Pass());
  }

  BalloonNotificationUIManager* balloon_manager =
      new BalloonNotificationUIManager(local_state);
  balloon_manager->SetBalloonCollection(BalloonCollection::Create());
  return balloon_manager;
}
#endif
