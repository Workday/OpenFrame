// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/network_profile_handler.h"

#include <algorithm>

#include "base/bind.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/shill_manager_client.h"
#include "chromeos/dbus/shill_profile_client.h"
#include "chromeos/network/network_profile_observer.h"
#include "chromeos/network/network_state_handler.h"
#include "dbus/object_path.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

bool ConvertListValueToStringVector(const base::ListValue& string_list,
                                    std::vector<std::string>* result) {
  for (size_t i = 0; i < string_list.GetSize(); ++i) {
    std::string str;
    if (!string_list.GetString(i, &str))
      return false;
    result->push_back(str);
  }
  return true;
}

void LogProfileRequestError(const std::string& profile_path,
                            const std::string& error_name,
                            const std::string& error_message) {
  LOG(ERROR) << "Error when requesting properties for profile "
             << profile_path << ": " << error_message;
}

class ProfilePathEquals {
 public:
  explicit ProfilePathEquals(const std::string& path)
      : path_(path) {
  }

  bool operator()(const NetworkProfile& profile) {
    return profile.path == path_;
  }

 private:
  std::string path_;
};

}  // namespace

// static
const char NetworkProfileHandler::kSharedProfilePath[] = "/profile/default";

void NetworkProfileHandler::AddObserver(NetworkProfileObserver* observer) {
  observers_.AddObserver(observer);
}

void NetworkProfileHandler::RemoveObserver(NetworkProfileObserver* observer) {
  observers_.RemoveObserver(observer);
}

void NetworkProfileHandler::GetManagerPropertiesCallback(
    DBusMethodCallStatus call_status,
    const base::DictionaryValue& properties) {
  if (DBUS_METHOD_CALL_FAILURE) {
    LOG(ERROR) << "Error when requesting manager properties.";
    return;
  }

  const base::Value* profiles = NULL;
  properties.GetWithoutPathExpansion(flimflam::kProfilesProperty, &profiles);
  if (!profiles) {
    LOG(ERROR) << "Manager properties returned from Shill don't contain "
               << "the field " << flimflam::kProfilesProperty;
    return;
  }
  OnPropertyChanged(flimflam::kProfilesProperty, *profiles);
}

void NetworkProfileHandler::OnPropertyChanged(const std::string& name,
                                              const base::Value& value) {
  if (name != flimflam::kProfilesProperty)
    return;

  const base::ListValue* profiles_value = NULL;
  value.GetAsList(&profiles_value);
  DCHECK(profiles_value);

  std::vector<std::string> new_profile_paths;
  bool result = ConvertListValueToStringVector(*profiles_value,
                                               &new_profile_paths);
  DCHECK(result);

  VLOG(2) << "Profiles: " << profiles_.size();
  // Search for removed profiles.
  std::vector<std::string> removed_profile_paths;
  for (ProfileList::const_iterator it = profiles_.begin();
       it != profiles_.end(); ++it) {
    if (std::find(new_profile_paths.begin(),
                  new_profile_paths.end(),
                  it->path) == new_profile_paths.end()) {
      removed_profile_paths.push_back(it->path);
    }
  }

  for (std::vector<std::string>::const_iterator it =
           removed_profile_paths.begin();
       it != removed_profile_paths.end(); ++it) {
    RemoveProfile(*it);
  }

  for (std::vector<std::string>::const_iterator it = new_profile_paths.begin();
       it != new_profile_paths.end(); ++it) {
    // Skip known profiles. The associated userhash should never change.
    if (GetProfileForPath(*it))
      continue;

    VLOG(2) << "Requesting properties of profile path " << *it << ".";
    DBusThreadManager::Get()->GetShillProfileClient()->GetProperties(
        dbus::ObjectPath(*it),
        base::Bind(&NetworkProfileHandler::GetProfilePropertiesCallback,
                   weak_ptr_factory_.GetWeakPtr(),
                   *it),
        base::Bind(&LogProfileRequestError, *it));
  }

  // When the profile list changes, ServiceCompleteList may also change, so
  // trigger a Manager update to request the updated list.
  if (network_state_handler_)
    network_state_handler_->UpdateManagerProperties();
}

void NetworkProfileHandler::GetProfilePropertiesCallback(
    const std::string& profile_path,
    const base::DictionaryValue& properties) {
  std::string userhash;
  properties.GetStringWithoutPathExpansion(shill::kUserHashProperty,
                                           &userhash);

  AddProfile(NetworkProfile(profile_path, userhash));
}

void NetworkProfileHandler::AddProfile(const NetworkProfile& profile) {
  VLOG(2) << "Adding profile " << profile.ToDebugString() << ".";
  profiles_.push_back(profile);
  FOR_EACH_OBSERVER(NetworkProfileObserver, observers_,
                    OnProfileAdded(profiles_.back()));
}

void NetworkProfileHandler::RemoveProfile(const std::string& profile_path) {
  VLOG(2) << "Removing profile for path " << profile_path << ".";
  ProfileList::iterator found = std::find_if(profiles_.begin(), profiles_.end(),
                                             ProfilePathEquals(profile_path));
  if (found == profiles_.end())
    return;
  NetworkProfile profile = *found;
  profiles_.erase(found);
  FOR_EACH_OBSERVER(NetworkProfileObserver, observers_,
                    OnProfileRemoved(profile));
}

const NetworkProfile* NetworkProfileHandler::GetProfileForPath(
    const std::string& profile_path) const {
  ProfileList::const_iterator found =
      std::find_if(profiles_.begin(), profiles_.end(),
                   ProfilePathEquals(profile_path));

  if (found == profiles_.end())
    return NULL;
  return &*found;
}

const NetworkProfile* NetworkProfileHandler::GetProfileForUserhash(
    const std::string& userhash) const {
  for (NetworkProfileHandler::ProfileList::const_iterator it =
           profiles_.begin();
       it != profiles_.end(); ++it) {
    if (it->userhash == userhash)
      return &*it;
  }
  return NULL;
}

const NetworkProfile* NetworkProfileHandler::GetDefaultUserProfile() const {
  for (NetworkProfileHandler::ProfileList::const_iterator it =
           profiles_.begin();
       it != profiles_.end(); ++it) {
    if (!it->userhash.empty())
      return &*it;
  }
  return NULL;
}

NetworkProfileHandler::NetworkProfileHandler()
    : network_state_handler_(NULL),
      weak_ptr_factory_(this) {
}

void NetworkProfileHandler::Init(NetworkStateHandler* network_state_handler) {
  network_state_handler_ = network_state_handler;

  DBusThreadManager::Get()->GetShillManagerClient()->
      AddPropertyChangedObserver(this);

  // Request the initial profile list.
  DBusThreadManager::Get()->GetShillManagerClient()->GetProperties(
      base::Bind(&NetworkProfileHandler::GetManagerPropertiesCallback,
                 weak_ptr_factory_.GetWeakPtr()));
}

NetworkProfileHandler::~NetworkProfileHandler() {
  DBusThreadManager::Get()->GetShillManagerClient()->
      RemovePropertyChangedObserver(this);
}

}  // namespace chromeos
