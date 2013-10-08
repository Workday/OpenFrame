// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/component_extension_ime_manager_impl.h"

#include "base/file_util.h"
#include "base/logging.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_file_util.h"
#include "chrome/common/extensions/extension_l10n_util.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "content/public/browser/browser_thread.h"
#include "ui/base/l10n/l10n_util.h"

namespace chromeos {

namespace {

struct WhitelistedComponentExtensionIME {
  const char* id;
  const char* path;
} whitelisted_component_extension[] = {
  {
    // ChromeOS Keyboards extension.
    "jhffeifommiaekmbkkjlpmilogcfdohp",
    "/usr/share/chromeos-assets/input_methods/keyboard_layouts",
  },
  {
    // ChromeOS Hangul Input.
    "bdgdidmhaijohebebipajioienkglgfo",
    "/usr/share/chromeos-assets/input_methods/hangul",
  },
#if defined(OFFICIAL_BUILD)
  {
    // Official Google Japanese Input.
    "fpfbhcjppmaeaijcidgiibchfbnhbelj",
    "/usr/share/chromeos-assets/input_methods/nacl_mozc",
  },
  {
    // Google Chinese Input (zhuyin)
    "goedamlknlnjaengojinmfgpmdjmkooo",
    "/usr/share/chromeos-assets/input_methods/zhuyin",
  },
  {
    // Google Chinese Input (pinyin)
    "nmblnjkfdkabgdofidlkienfnnbjhnab",
    "/usr/share/chromeos-assets/input_methods/pinyin",
  },
  {
    // Google Chinese Input (cangjie)
    "gjhclobljhjhgoebiipblnmdodbmpdgd",
    "/usr/share/chromeos-assets/input_methods/cangjie",
  },
  {
    // Google input tools.
    "gjaehgfemfahhmlgpdfknkhdnemmolop",
    "/usr/share/chromeos-assets/input_methods/input_tools",
  },
#else
  {
    // Open-sourced Pinyin Chinese Input Method.
    "cpgalbafkoofkjmaeonnfijgpfennjjn",
    "/usr/share/chromeos-assets/input_methods/pinyin",
  },
  {
    // Open-sourced Zhuyin Chinese Input Method.
    "ekbifjdfhkmdeeajnolmgdlmkllopefi",
    "/usr/share/chromeos-assets/input_methods/zhuyin",
  },
  {
    // Open-sourced Cangjie Chinese Input Method.
    "aeebooiibjahgpgmhkeocbeekccfknbj",
    "/usr/share/chromeos-assets/input_methods/cangjie",
  },
  {
    // Open-sourced Mozc Japanese Input.
    "bbaiamgfapehflhememkfglaehiobjnk",
    "/usr/share/chromeos-assets/input_methods/nacl_mozc",
  },
#endif
};

extensions::ComponentLoader* GetComponentLoader() {
  Profile* profile = ProfileManager::GetDefaultProfileOrOffTheRecord();
  extensions::ExtensionSystem* extension_system =
      extensions::ExtensionSystem::Get(profile);
  ExtensionService* extension_service = extension_system->extension_service();
  return extension_service->component_loader();
}
}  // namespace

ComponentExtensionIMEManagerImpl::ComponentExtensionIMEManagerImpl()
    : is_initialized_(false),
      weak_ptr_factory_(this) {
}

ComponentExtensionIMEManagerImpl::~ComponentExtensionIMEManagerImpl() {
}

std::vector<ComponentExtensionIME> ComponentExtensionIMEManagerImpl::ListIME() {
  DCHECK(thread_checker_.CalledOnValidThread());
  return component_extension_list_;
}

bool ComponentExtensionIMEManagerImpl::Load(const std::string& extension_id,
                                            const std::string& manifest,
                                            const base::FilePath& file_path) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (loaded_extension_id_.find(extension_id) != loaded_extension_id_.end())
    return false;
  const std::string loaded_extension_id =
      GetComponentLoader()->Add(manifest, file_path);
  DCHECK_EQ(loaded_extension_id, extension_id);
  loaded_extension_id_.insert(extension_id);
  return true;
}

bool ComponentExtensionIMEManagerImpl::Unload(const std::string& extension_id,
                                              const base::FilePath& file_path) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (loaded_extension_id_.find(extension_id) == loaded_extension_id_.end())
    return false;
  GetComponentLoader()->Remove(extension_id);
  loaded_extension_id_.erase(extension_id);
  return true;
}

scoped_ptr<DictionaryValue> ComponentExtensionIMEManagerImpl::GetManifest(
    const base::FilePath& file_path) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));
  std::string error;
  scoped_ptr<DictionaryValue> manifest(
      extension_file_util::LoadManifest(file_path, &error));
  if (!manifest.get())
    LOG(ERROR) << "Failed at getting manifest";
  if (!extension_l10n_util::LocalizeExtension(file_path,
                                              manifest.get(),
                                              &error))
    LOG(ERROR) << "Localization failed";

  return manifest.Pass();
}

void ComponentExtensionIMEManagerImpl::InitializeAsync(
    const base::Closure& callback) {
  DCHECK(!is_initialized_);
  DCHECK(thread_checker_.CalledOnValidThread());

  std::vector<ComponentExtensionIME>* component_extension_ime_list
      = new std::vector<ComponentExtensionIME>;
  content::BrowserThread::PostTaskAndReply(
      content::BrowserThread::FILE,
      FROM_HERE,
      base::Bind(&ComponentExtensionIMEManagerImpl::ReadComponentExtensionsInfo,
                 base::Unretained(component_extension_ime_list)),
      base::Bind(
          &ComponentExtensionIMEManagerImpl::OnReadComponentExtensionsInfo,
          weak_ptr_factory_.GetWeakPtr(),
          base::Owned(component_extension_ime_list),
          callback));
}

bool ComponentExtensionIMEManagerImpl::IsInitialized() {
  return is_initialized_;
}

// static
bool ComponentExtensionIMEManagerImpl::ReadEngineComponent(
    const DictionaryValue& dict,
    ComponentExtensionEngine* out) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));
  DCHECK(out);
  std::string type;
  if (!dict.GetString(extension_manifest_keys::kType, &type))
    return false;
  if (type != "ime")
    return false;
  if (!dict.GetString(extension_manifest_keys::kId, &out->engine_id))
    return false;
  if (!dict.GetString(extension_manifest_keys::kName, &out->display_name))
    return false;

  std::set<std::string> languages;
  const base::Value* language_value = NULL;
  if (dict.Get(extension_manifest_keys::kLanguage, &language_value)) {
    if (language_value->GetType() == base::Value::TYPE_STRING) {
      std::string language_str;
      language_value->GetAsString(&language_str);
      languages.insert(language_str);
    } else if (language_value->GetType() == base::Value::TYPE_LIST) {
      const base::ListValue* language_list = NULL;
      language_value->GetAsList(&language_list);
      for (size_t j = 0; j < language_list->GetSize(); ++j) {
        std::string language_str;
        if (language_list->GetString(j, &language_str))
          languages.insert(language_str);
      }
    }
  }
  DCHECK(!languages.empty());
  out->language_codes.assign(languages.begin(), languages.end());

  const ListValue* layouts = NULL;
  if (!dict.GetList(extension_manifest_keys::kLayouts, &layouts))
    return false;

  for (size_t i = 0; i < layouts->GetSize(); ++i) {
    std::string buffer;
    if (layouts->GetString(i, &buffer))
      out->layouts.push_back(buffer);
  }
  return true;
}

// static
bool ComponentExtensionIMEManagerImpl::ReadExtensionInfo(
    const DictionaryValue& manifest,
    const std::string& extension_id,
    ComponentExtensionIME* out) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));
  if (!manifest.GetString(extension_manifest_keys::kDescription,
                          &out->description))
    return false;
  std::string url_string;
  if (!manifest.GetString(extension_manifest_keys::kOptionsPage, &url_string))
    return true;  // It's okay to return true on no option page case.

  GURL url = extensions::Extension::GetResourceURL(
      extensions::Extension::GetBaseURLFromExtensionId(extension_id),
      url_string);
  if (!url.is_valid())
    return false;
  out->options_page_url = url;
  return true;
}

// static
void ComponentExtensionIMEManagerImpl::ReadComponentExtensionsInfo(
    std::vector<ComponentExtensionIME>* out_imes) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));
  DCHECK(out_imes);
  for (size_t i = 0; i < arraysize(whitelisted_component_extension); ++i) {
    ComponentExtensionIME component_ime;
    component_ime.path = base::FilePath(
        whitelisted_component_extension[i].path);

    const base::FilePath manifest_path =
        component_ime.path.Append("manifest.json");

    if (!base::PathExists(component_ime.path) ||
        !base::PathExists(manifest_path))
      continue;

    if (!file_util::ReadFileToString(manifest_path, &component_ime.manifest))
      continue;

    scoped_ptr<DictionaryValue> manifest = GetManifest(component_ime.path);
    if (!manifest.get())
      continue;

    if (!ReadExtensionInfo(*manifest.get(),
                           whitelisted_component_extension[i].id,
                           &component_ime))
      continue;
    component_ime.id = whitelisted_component_extension[i].id;

    const ListValue* component_list;
    if (!manifest->GetList(extension_manifest_keys::kInputComponents,
                           &component_list))
      continue;

    for (size_t i = 0; i < component_list->GetSize(); ++i) {
      const DictionaryValue* dictionary;
      if (!component_list->GetDictionary(i, &dictionary))
        continue;

      ComponentExtensionEngine engine;
      ReadEngineComponent(*dictionary, &engine);
      component_ime.engines.push_back(engine);
    }
    out_imes->push_back(component_ime);
  }
}

void ComponentExtensionIMEManagerImpl::OnReadComponentExtensionsInfo(
    std::vector<ComponentExtensionIME>* result,
    const base::Closure& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(result);
  component_extension_list_ = *result;
  is_initialized_ = true;
  callback.Run();
}

}  // namespace chromeos
