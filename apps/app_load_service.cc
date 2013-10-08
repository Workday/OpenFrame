// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/app_load_service.h"

#include "apps/app_load_service_factory.h"
#include "apps/launcher.h"
#include "apps/shell_window_registry.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/unpacked_installer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"

using extensions::Extension;
using extensions::ExtensionPrefs;

namespace apps {

AppLoadService::PostReloadAction::PostReloadAction()
    : command_line(CommandLine::NO_PROGRAM) {
}

AppLoadService::AppLoadService(Profile* profile)
    : profile_(profile) {
  registrar_.Add(
      this, chrome::NOTIFICATION_EXTENSION_HOST_DID_STOP_LOADING,
      content::NotificationService::AllSources());
  registrar_.Add(
      this, chrome::NOTIFICATION_EXTENSION_UNLOADED,
      content::NotificationService::AllSources());
}

AppLoadService::~AppLoadService() {}

void AppLoadService::RestartApplication(const std::string& extension_id) {
  post_reload_actions_[extension_id].action_type = RESTART;
  ExtensionService* service = extensions::ExtensionSystem::Get(profile_)->
      extension_service();
  DCHECK(service);
  service->ReloadExtension(extension_id);
}

bool AppLoadService::LoadAndLaunch(const base::FilePath& extension_path,
                                   const CommandLine& command_line,
                                   const base::FilePath& current_dir) {
  std::string extension_id;
  if (!extensions::UnpackedInstaller::Create(profile_->GetExtensionService())->
          LoadFromCommandLine(base::FilePath(extension_path), &extension_id)) {
    return false;
  }

  // Schedule the app to be launched once loaded.
  PostReloadAction& action = post_reload_actions_[extension_id];
  action.action_type = LAUNCH_WITH_COMMAND_LINE;
  action.command_line = command_line;
  action.current_dir = current_dir;
  return true;
}

// static
AppLoadService* AppLoadService::Get(Profile* profile) {
  return apps::AppLoadServiceFactory::GetForProfile(profile);
}

void AppLoadService::Observe(int type,
                             const content::NotificationSource& source,
                             const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_EXTENSION_HOST_DID_STOP_LOADING: {
      extensions::ExtensionHost* host =
          content::Details<extensions::ExtensionHost>(details).ptr();
      const Extension* extension = host->extension();
      // It is possible for an extension to be unloaded before it stops loading.
      if (!extension)
        break;
      std::map<std::string, PostReloadAction>::iterator it =
          post_reload_actions_.find(extension->id());
      if (it == post_reload_actions_.end())
        break;

      switch (it->second.action_type) {
        case LAUNCH:
          LaunchPlatformApp(profile_, extension);
          break;
        case RESTART:
          RestartPlatformApp(profile_, extension);
          break;
        case LAUNCH_WITH_COMMAND_LINE:
          LaunchPlatformAppWithCommandLine(
              profile_, extension, &it->second.command_line,
              it->second.current_dir);
          break;
        default:
          NOTREACHED();
      }

      post_reload_actions_.erase(it);
      break;
    }
    case chrome::NOTIFICATION_EXTENSION_UNLOADED: {
      const extensions::UnloadedExtensionInfo* unload_info =
          content::Details<extensions::UnloadedExtensionInfo>(details).ptr();
      if (!unload_info->extension->is_platform_app())
        break;

      if (WasUnloadedForReload(*unload_info) &&
          HasShellWindows(unload_info->extension->id()) &&
          !HasPostReloadAction(unload_info->extension->id())) {
        post_reload_actions_[unload_info->extension->id()].action_type = LAUNCH;
      }
      break;
    }
    default:
      NOTREACHED();
  }
}

bool AppLoadService::HasShellWindows(const std::string& extension_id) {
  return !ShellWindowRegistry::Get(profile_)->
      GetShellWindowsForApp(extension_id).empty();
}

bool AppLoadService::WasUnloadedForReload(
    const extensions::UnloadedExtensionInfo& unload_info) {
  if (unload_info.reason == extension_misc::UNLOAD_REASON_DISABLE) {
    ExtensionPrefs* prefs = ExtensionPrefs::Get(profile_);
     return (prefs->GetDisableReasons(unload_info.extension->id()) &
        Extension::DISABLE_RELOAD) != 0;
  }
  return false;
}

bool AppLoadService::HasPostReloadAction(const std::string& extension_id) {
  return post_reload_actions_.find(extension_id) != post_reload_actions_.end();
}

}  // namespace apps
