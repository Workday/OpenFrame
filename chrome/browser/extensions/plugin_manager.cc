// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/plugin_manager.h"
#include "chrome/browser/plugins/chrome_plugin_service_filter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/extensions/api/plugins/plugins_handler.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/common/pepper_plugin_info.h"
#include "url/gurl.h"

using content::PluginService;

static const char* kNaClPluginMimeType = "application/x-nacl";

namespace extensions {

PluginManager::PluginManager(Profile* profile) : profile_(profile) {
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_LOADED,
                 content::Source<Profile>(profile));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_UNLOADED,
                 content::Source<Profile>(profile));
}

PluginManager::~PluginManager() {
}

static base::LazyInstance<ProfileKeyedAPIFactory<PluginManager> >
    g_factory = LAZY_INSTANCE_INITIALIZER;

// static
ProfileKeyedAPIFactory<PluginManager>* PluginManager::GetFactoryInstance() {
  return &g_factory.Get();
}

void PluginManager::Observe(int type,
                            const content::NotificationSource& source,
                            const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_EXTENSION_LOADED) {
    const Extension* extension =
        content::Details<const Extension>(details).ptr();

    bool plugins_or_nacl_changed = false;
    if (PluginInfo::HasPlugins(extension)) {
      const PluginInfo::PluginVector* plugins =
          PluginInfo::GetPlugins(extension);
      CHECK(plugins);
      plugins_or_nacl_changed = true;
      for (PluginInfo::PluginVector::const_iterator plugin = plugins->begin();
           plugin != plugins->end(); ++plugin) {
        PluginService::GetInstance()->RefreshPlugins();
        PluginService::GetInstance()->AddExtraPluginPath(plugin->path);
        ChromePluginServiceFilter* filter =
            ChromePluginServiceFilter::GetInstance();
        if (plugin->is_public) {
          filter->RestrictPluginToProfileAndOrigin(
              plugin->path, profile_, GURL());
        } else {
          filter->RestrictPluginToProfileAndOrigin(
              plugin->path, profile_, extension->url());
        }
      }
    }

    const NaClModuleInfo::List* nacl_modules =
        NaClModuleInfo::GetNaClModules(extension);
    if (nacl_modules) {
      plugins_or_nacl_changed = true;
      for (NaClModuleInfo::List::const_iterator module = nacl_modules->begin();
           module != nacl_modules->end(); ++module) {
        RegisterNaClModule(*module);
      }
      UpdatePluginListWithNaClModules();
    }

    if (plugins_or_nacl_changed)
      PluginService::GetInstance()->PurgePluginListCache(profile_, false);

  } else if (type == chrome::NOTIFICATION_EXTENSION_UNLOADED) {
    const Extension* extension =
        content::Details<UnloadedExtensionInfo>(details)->extension;

    bool plugins_or_nacl_changed = false;
    if (PluginInfo::HasPlugins(extension)) {
      const PluginInfo::PluginVector* plugins =
          PluginInfo::GetPlugins(extension);
      plugins_or_nacl_changed = true;
      for (PluginInfo::PluginVector::const_iterator plugin = plugins->begin();
           plugin != plugins->end(); ++plugin) {
        PluginService::GetInstance()->ForcePluginShutdown(plugin->path);
        PluginService::GetInstance()->RefreshPlugins();
        PluginService::GetInstance()->RemoveExtraPluginPath(plugin->path);
        ChromePluginServiceFilter::GetInstance()->UnrestrictPlugin(
            plugin->path);
      }
    }

    const NaClModuleInfo::List* nacl_modules =
        NaClModuleInfo::GetNaClModules(extension);
    if (nacl_modules) {
      plugins_or_nacl_changed = true;
      for (NaClModuleInfo::List::const_iterator module = nacl_modules->begin();
           module != nacl_modules->end(); ++module) {
        UnregisterNaClModule(*module);
      }
      UpdatePluginListWithNaClModules();
    }

    if (plugins_or_nacl_changed)
      PluginService::GetInstance()->PurgePluginListCache(profile_, false);

  } else {
    NOTREACHED();
  }
}

void PluginManager::RegisterNaClModule(const NaClModuleInfo& info) {
  DCHECK(FindNaClModule(info.url) == nacl_module_list_.end());
  nacl_module_list_.push_front(info);
}

void PluginManager::UnregisterNaClModule(const NaClModuleInfo& info) {
  NaClModuleInfo::List::iterator iter = FindNaClModule(info.url);
  DCHECK(iter != nacl_module_list_.end());
  nacl_module_list_.erase(iter);
}

void PluginManager::UpdatePluginListWithNaClModules() {
  // An extension has been added which has a nacl_module component, which means
  // there is a MIME type that module wants to handle, so we need to add that
  // MIME type to plugins which handle NaCl modules in order to allow the
  // individual modules to handle these types.
  base::FilePath path;
  if (!PathService::Get(chrome::FILE_NACL_PLUGIN, &path))
    return;
  const content::PepperPluginInfo* pepper_info =
      PluginService::GetInstance()->GetRegisteredPpapiPluginInfo(path);
  if (!pepper_info)
    return;

  std::vector<content::WebPluginMimeType>::const_iterator mime_iter;
  // Check each MIME type the plugins handle for the NaCl MIME type.
  for (mime_iter = pepper_info->mime_types.begin();
       mime_iter != pepper_info->mime_types.end(); ++mime_iter) {
    if (mime_iter->mime_type == kNaClPluginMimeType) {
      // This plugin handles "application/x-nacl".

      PluginService::GetInstance()->UnregisterInternalPlugin(pepper_info->path);

      content::WebPluginInfo info = pepper_info->ToWebPluginInfo();

      for (NaClModuleInfo::List::const_iterator iter =
               nacl_module_list_.begin();
           iter != nacl_module_list_.end(); ++iter) {
        // Add the MIME type specified in the extension to this NaCl plugin,
        // With an extra "nacl" argument to specify the location of the NaCl
        // manifest file.
        content::WebPluginMimeType mime_type_info;
        mime_type_info.mime_type = iter->mime_type;
        mime_type_info.additional_param_names.push_back(UTF8ToUTF16("nacl"));
        mime_type_info.additional_param_values.push_back(
            UTF8ToUTF16(iter->url.spec()));
        info.mime_types.push_back(mime_type_info);
      }

      PluginService::GetInstance()->RefreshPlugins();
      PluginService::GetInstance()->RegisterInternalPlugin(info, true);
      // This plugin has been modified, no need to check the rest of its
      // types, but continue checking other plugins.
      break;
    }
  }
}

NaClModuleInfo::List::iterator PluginManager::FindNaClModule(const GURL& url) {
  for (NaClModuleInfo::List::iterator iter = nacl_module_list_.begin();
       iter != nacl_module_list_.end(); ++iter) {
    if (iter->url == url)
      return iter;
  }
  return nacl_module_list_.end();
}

}  // namespace extensions
