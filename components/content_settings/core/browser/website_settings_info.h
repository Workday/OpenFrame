// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_CONTENT_SETTINGS_CORE_BROWSER_WEBSITE_SETTINGS_INFO_H_
#define COMPONENTS_CONTENT_SETTINGS_CORE_BROWSER_WEBSITE_SETTINGS_INFO_H_

#include <string>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "components/content_settings/core/common/content_settings_types.h"

namespace base {
class Value;
}  // namespace base

namespace content_settings {

// This class stores the properties related to a website setting.
class WebsiteSettingsInfo {
 public:
  enum SyncStatus { SYNCABLE, UNSYNCABLE };

  enum LossyStatus { LOSSY, NOT_LOSSY };

  enum ScopingType {
    // Settings scoped to the domain of the main frame only.
    TOP_LEVEL_DOMAIN_ONLY_SCOPE,

    // Settings scoped to the domain of the requesting frame only. This should
    // not generally be used.
    REQUESTING_DOMAIN_ONLY_SCOPE,

    // Settings scoped to the origin of the requesting frame only.
    REQUESTING_ORIGIN_ONLY_SCOPE,

    // Settings scoped to the combination of the origin of the requesting
    // frame and the origin of the top level frame.
    REQUESTING_ORIGIN_AND_TOP_LEVEL_ORIGIN_SCOPE
  };

  WebsiteSettingsInfo(ContentSettingsType type,
                      const std::string& name,
                      scoped_ptr<base::Value> initial_default_value,
                      SyncStatus sync_status,
                      LossyStatus lossy_status,
                      ScopingType scoping_type);
  ~WebsiteSettingsInfo();

  ContentSettingsType type() const { return type_; }
  const std::string& name() const { return name_; }

  const std::string& pref_name() const { return pref_name_; }
  const std::string& default_value_pref_name() const {
    return default_value_pref_name_;
  }
  const base::Value* initial_default_value() const {
    return initial_default_value_.get();
  }

  uint32 GetPrefRegistrationFlags() const;

  ScopingType scoping_type() const { return scoping_type_; }

 private:
  const ContentSettingsType type_;
  const std::string name_;

  const std::string pref_name_;
  const std::string default_value_pref_name_;
  const scoped_ptr<base::Value> initial_default_value_;
  const SyncStatus sync_status_;
  const LossyStatus lossy_status_;
  const ScopingType scoping_type_;

  DISALLOW_COPY_AND_ASSIGN(WebsiteSettingsInfo);
};

}  // namespace content_settings

#endif  // COMPONENTS_CONTENT_SETTINGS_CORE_BROWSER_WEBSITE_SETTINGS_INFO_H_
