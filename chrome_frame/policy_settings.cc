// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome_frame/policy_settings.h"

#include <algorithm>

#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/registry.h"
#include "chrome_frame/utils.h"
#include "policy/policy_constants.h"

namespace {

// This array specifies the order in which registry keys are tested.  Do not
// change this unless the decision is made product-wide (i.e., in Chrome's
// configuration policy provider).
const HKEY kRootKeys[] = {
  HKEY_LOCAL_MACHINE,
  HKEY_CURRENT_USER
};

}  // namespace

PolicySettings::RendererForUrl PolicySettings::GetRendererForUrl(
    const wchar_t* url) {
  RendererForUrl renderer = default_renderer_;
  std::vector<std::wstring>::const_iterator it;
  for (it = renderer_exclusion_list_.begin();
       it != renderer_exclusion_list_.end(); ++it) {
    if (MatchPattern(url, (*it))) {
      renderer = (renderer == RENDER_IN_HOST) ?
          RENDER_IN_CHROME_FRAME : RENDER_IN_HOST;
      break;
    }
  }
  return renderer;
}

PolicySettings::RendererForUrl PolicySettings::GetRendererForContentType(
    const wchar_t* content_type) {
  DCHECK(content_type);
  RendererForUrl renderer = RENDERER_NOT_SPECIFIED;
  std::vector<std::wstring>::const_iterator it;
  for (it = content_type_list_.begin();
       it != content_type_list_.end(); ++it) {
    if (lstrcmpiW(content_type, (*it).c_str()) == 0) {
      renderer = RENDER_IN_CHROME_FRAME;
      break;
    }
  }
  return renderer;
}

const CommandLine& PolicySettings::AdditionalLaunchParameters() const {
  return additional_launch_parameters_;
}

// static
void PolicySettings::ReadUrlSettings(
    RendererForUrl* default_renderer,
    std::vector<std::wstring>* renderer_exclusion_list) {
  DCHECK(default_renderer);
  DCHECK(renderer_exclusion_list);

  *default_renderer = RENDERER_NOT_SPECIFIED;
  renderer_exclusion_list->clear();

  base::win::RegKey config_key;
  DWORD value = RENDERER_NOT_SPECIFIED;
  std::wstring settings_value(
      ASCIIToWide(policy::key::kChromeFrameRendererSettings));
  for (int i = 0; i < arraysize(kRootKeys); ++i) {
    if ((config_key.Open(kRootKeys[i], policy::kRegistryChromePolicyKey,
                         KEY_READ) == ERROR_SUCCESS) &&
        (config_key.ReadValueDW(settings_value.c_str(),
                                &value) == ERROR_SUCCESS)) {
        break;
    }
  }

  DCHECK(value == RENDERER_NOT_SPECIFIED ||
         value == RENDER_IN_HOST ||
         value == RENDER_IN_CHROME_FRAME) <<
      "invalid default renderer setting: " << value;

  if (value != RENDER_IN_HOST && value != RENDER_IN_CHROME_FRAME) {
    DVLOG(1) << "default renderer not specified via policy";
  } else {
    *default_renderer = static_cast<RendererForUrl>(value);
    const char* exclusion_list_name = (*default_renderer == RENDER_IN_HOST) ?
        policy::key::kRenderInChromeFrameList :
        policy::key::kRenderInHostList;

    EnumerateKeyValues(config_key.Handle(),
        ASCIIToWide(exclusion_list_name).c_str(), renderer_exclusion_list);

    DVLOG(1) << "Default renderer as specified via policy: "
             << *default_renderer
             << " exclusion list size: " << renderer_exclusion_list->size();
  }
}

// static
void PolicySettings::ReadContentTypeSetting(
    std::vector<std::wstring>* content_type_list) {
  DCHECK(content_type_list);

  std::wstring sub_key(policy::kRegistryChromePolicyKey);
  sub_key += L"\\";
  sub_key += ASCIIToWide(policy::key::kChromeFrameContentTypes);

  content_type_list->clear();
  for (int i = 0; i < arraysize(kRootKeys) && content_type_list->empty();
       ++i) {
    EnumerateKeyValues(kRootKeys[i], sub_key.c_str(), content_type_list);
  }
}

// static
void PolicySettings::ReadStringSetting(const char* value_name,
                                       std::wstring* value) {
  DCHECK(value);
  value->clear();
  base::win::RegKey config_key;
  std::wstring value_name_str(ASCIIToWide(value_name));
  for (int i = 0; i < arraysize(kRootKeys); ++i) {
    if ((config_key.Open(kRootKeys[i], policy::kRegistryChromePolicyKey,
                         KEY_READ) == ERROR_SUCCESS) &&
        (config_key.ReadValue(value_name_str.c_str(),
                              value) == ERROR_SUCCESS)) {
      break;
    }
  }
}

// static
void PolicySettings::ReadBoolSetting(const char* value_name, bool* value) {
  DCHECK(value);
  base::win::RegKey config_key;
  string16 value_name_str(ASCIIToWide(value_name));
  DWORD dword_value = 0;
  for (int i = 0; i < arraysize(kRootKeys); ++i) {
    if ((config_key.Open(kRootKeys[i], policy::kRegistryChromePolicyKey,
                         KEY_QUERY_VALUE) == ERROR_SUCCESS) &&
        (config_key.ReadValueDW(value_name_str.c_str(),
                                &dword_value) == ERROR_SUCCESS)) {
      *value = (dword_value != 0);
      break;
    }
  }
}

void PolicySettings::RefreshFromRegistry() {
  RendererForUrl default_renderer;
  std::vector<std::wstring> renderer_exclusion_list;
  std::vector<std::wstring> content_type_list;
  std::wstring application_locale;
  CommandLine additional_launch_parameters(CommandLine::NO_PROGRAM);
  std::wstring additional_parameters_str;
  bool suppress_turndown_prompt = false;

  // Read the latest settings from the registry
  ReadUrlSettings(&default_renderer, &renderer_exclusion_list);
  ReadContentTypeSetting(&content_type_list);
  ReadStringSetting(policy::key::kApplicationLocaleValue, &application_locale);
  ReadStringSetting(policy::key::kAdditionalLaunchParameters,
                    &additional_parameters_str);
  if (!additional_parameters_str.empty()) {
    additional_parameters_str.insert(0, L"fake.exe ");
    additional_launch_parameters.ParseFromString(additional_parameters_str);
  }
  ReadBoolSetting(policy::key::kSuppressChromeFrameTurndownPrompt,
                  &suppress_turndown_prompt);

  // Nofail swap in the new values.  (Note: this is all that need be protected
  // under a mutex if/when this becomes thread safe.)
  using std::swap;

  swap(default_renderer_, default_renderer);
  swap(renderer_exclusion_list_, renderer_exclusion_list);
  swap(content_type_list_, content_type_list);
  swap(application_locale_, application_locale);
  swap(additional_launch_parameters_, additional_launch_parameters);
  swap(suppress_turndown_prompt_, suppress_turndown_prompt);
}

// static
PolicySettings* PolicySettings::GetInstance() {
  return Singleton<PolicySettings>::get();
}
