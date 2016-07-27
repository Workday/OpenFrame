// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome_frame/test/ie_configurator.h"

#include <windows.h>
#include <objbase.h>

#include <ios>
#include <list>
#include <vector>

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string16.h"
#include "base/time/time.h"
#include "base/win/registry.h"
#include "chrome_frame/chrome_tab.h"
#include "chrome_frame/test/chrome_frame_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chrome_frame_test {

namespace {

const wchar_t kKeyIEApprovedExtensions[] =
    L"Software\\Microsoft\\Internet Explorer\\Approved Extensions\\";
const wchar_t kKeyIEInformationBar[] =
    L"Software\\Microsoft\\Internet Explorer\\InformationBar";
const wchar_t kKeyIEMain[] =
    L"Software\\Microsoft\\Internet Explorer\\Main";
const wchar_t kKeyIEPhishingFilter[] =
    L"Software\\Microsoft\\Internet Explorer\\PhishingFilter";
const wchar_t kKeyIEBrowserEmulation[] =
    L"Software\\Microsoft\\Internet Explorer\\BrowserEmulation";
const wchar_t kKeyPoliciesExt[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Ext";
const wchar_t kValueEnabledV8[] = L"EnabledV8";
const wchar_t kValueEnabledV9[] = L"EnabledV9";
const wchar_t kValueFirstTime[] = L"FirstTime";
const wchar_t kValueIE8Completed[] = L"IE8RunOncePerInstallCompleted";
const wchar_t kValueIE8CompletionTime[] = L"IE8RunOnceCompletionTime";
const wchar_t kValueIE8RunOnceLastShown[] = L"IE8RunOnceLastShown";
const wchar_t kValueIE8RunOnceLastShownTimestamp[] =
    L"IE8RunOnceLastShown_TIMESTAMP";
const wchar_t kValueIE8TourNoShow[] = L"IE8TourNoShow";
const wchar_t kValueIE9Completed[] = L"IE9RunOncePerInstallCompleted";
const wchar_t kValueIE9CompletionTime[] = L"IE9RunOnceCompletionTime";
const wchar_t kValueIE9RunOnceLastShown[] = L"IE9RunOnceLastShown";
const wchar_t kValueIE9RunOnceLastShownTimestamp[] =
    L"IE9RunOnceLastShown_TIMESTAMP";
const wchar_t kValueIE9TourNoShow[] = L"IE9TourNoShow";
const wchar_t kValueIE10Completed[] = L"IE10RunOncePerInstallCompleted";
const wchar_t kValueIE10CompletionTime[] = L"IE10RunOnceCompletionTime";
const wchar_t kValueIE10RunOnceLastShown[] = L"IE10RunOnceLastShown";
const wchar_t kValueIE10RunOnceLastShownTimestamp[] =
    L"IE10RunOnceLastShown_TIMESTAMP";
const wchar_t kValueIgnoreFrameApprovalCheck[] = L"IgnoreFrameApprovalCheck";
const wchar_t kValueMSCompatibilityMode[] = L"MSCompatibilityMode";

// A helper class that accumulate a set of registry mutations and corresponding
// undo data via calls to its Add*() methods.  The mutations can be applied to
// the registry (repeatedly, if desired) via a call to Apply().  Revert() can be
// called to apply the accumulated undo data.
class RegistrySetter {
 public:
  RegistrySetter();
  ~RegistrySetter();

  // Adds a mutation that sets a REG_DWORD registry value, creating any
  // intermediate keys as necessary.  |key| and |value| must remain valid
  // throughout all calls to Apply().
  void AddDWORDValue(HKEY root, const wchar_t* key, const wchar_t* value,
                     DWORD data);

  // Adds a mutation that assigns a FILETIME to a REG_BINARY registry value,
  // creating any intermediate keys as necessary.  |key| and |value| must remain
  // valid throughout all calls to Apply().
  void AddFILETIMEValue(HKEY root, const wchar_t* key, const wchar_t* value,
                        FILETIME data);

  // Applies all mutations in the order they were added.  Errors encountered
  // along the way are logged, but do not stop progress.
  void Apply() const;

  // Applies the undo data in the reverse order that their corresponding
  // mutations were added.
  void Revert() const;

 private:
  // The data for an individual registry value.  A non-existent value is
  // indicated by type = REG_NONE and empty data.
  struct RegistryData {
    HKEY root;
    const wchar_t* key;
    const wchar_t* value;
    DWORD type;
    std::vector<uint8> data;
  };

  typedef std::list<RegistryData> RegistryDataList;

  // Adds a mutation to the end of the apply list with the given data, and a
  // mutation to the revert list with the current state of the value.
  void AddValue(HKEY root, const wchar_t* key, const wchar_t* value, DWORD type,
                const uint8* value_begin, size_t value_len);

  // Add a mutation to the revert list that restores a registry value to its
  // current state.  This only adds entries to set or remove |value|.  If
  // portions of the hierarchy identified by |key| do not exist, but are created
  // between the invocation of this method and the time the revert list is
  // applied, only |value| is deleted upon revert (intermediate keys are not
  // deleted).
  void SaveCurrentValue(HKEY root, const wchar_t* key, const wchar_t* value);

  // Applies all mutations in |data_list| in order.
  static void ApplyList(const RegistryDataList& data_list);

  RegistryDataList apply_list_;
  RegistryDataList revert_list_;

  DISALLOW_COPY_AND_ASSIGN(RegistrySetter);
};

// A Google Test event listener that delegates to a configurator.
class ConfiguratorDriver : public testing::EmptyTestEventListener {
 public:
  explicit ConfiguratorDriver(IEConfigurator* configurator);
  virtual ~ConfiguratorDriver();

  virtual void OnTestProgramStart(const testing::UnitTest& unit_test) OVERRIDE;
  virtual void OnTestStart(const testing::TestInfo& test_info) OVERRIDE;
  virtual void OnTestProgramEnd(const testing::UnitTest& unit_test) OVERRIDE;

 private:
  scoped_ptr<IEConfigurator> configurator_;
  DISALLOW_COPY_AND_ASSIGN(ConfiguratorDriver);
};

// A configurator for Internet Explorer 7.
class IE7Configurator : public IEConfigurator {
 public:
  IE7Configurator();
  virtual ~IE7Configurator();

  virtual void Initialize() OVERRIDE;
  virtual void ApplySettings() OVERRIDE;
  virtual void RevertSettings() OVERRIDE;

 private:
  RegistrySetter setter_;

  DISALLOW_COPY_AND_ASSIGN(IE7Configurator);
};

// A configurator for Internet Explorer 8, 9, and 10.
class ModernIEConfigurator : public IEConfigurator {
 public:
  explicit ModernIEConfigurator(IEVersion ie_version);
  virtual ~ModernIEConfigurator();

  virtual void Initialize() OVERRIDE;
  virtual void ApplySettings() OVERRIDE;
  virtual void RevertSettings() OVERRIDE;

 private:
  // The names of the registry values used to determine if IE's one-time
  // initialization has been completed.
  struct RunOnceValueNames {
    // This DWORD value is non-zero once initialization has been completed.
    const wchar_t* completed;
    // This 8-byte binary value is the FILETIME of completion.
    const wchar_t* completion_time;
    // This DWORD value is non-zero if run-once was previously deferred.
    const wchar_t* last_shown;
    // This 8-byte binary value is the FILETIME of run-once deferral.
    const wchar_t* last_shown_timestamp;
  };

  static const RunOnceValueNames kIE8ValueNames;
  static const RunOnceValueNames kIE9ValueNames;
  static const RunOnceValueNames kIE10ValueNames;

  static const RunOnceValueNames* RunOnceNamesForVersion(IEVersion ie_version);
  bool IsPerUserSetupComplete();
  static base::string16 GetChromeFrameBHOCLSID();
  static bool IsAddonPromptDisabledForChromeFrame();

  const IEVersion ie_version_;
  const RunOnceValueNames* run_once_value_names_;
  RegistrySetter setter_;

  DISALLOW_COPY_AND_ASSIGN(ModernIEConfigurator);
};

// RegistrySetter implementation.

RegistrySetter::RegistrySetter() {
}

RegistrySetter::~RegistrySetter() {
}

void RegistrySetter::AddValue(HKEY root,
                              const wchar_t* key,
                              const wchar_t* value,
                              DWORD type,
                              const uint8* value_begin,
                              size_t value_len) {
  RegistryData the_data = {
    root,
    key,
    value,
    type,
    std::vector<uint8>(value_begin, value_begin + value_len)
  };
  apply_list_.push_back(the_data);
  SaveCurrentValue(root, key, value);
}

void RegistrySetter::SaveCurrentValue(HKEY root,
                                      const wchar_t* key,
                                      const wchar_t* value) {
  base::win::RegKey the_key;
  RegistryData the_data = { root, key, value, REG_NONE };

  LONG result = the_key.Open(root, key, KEY_QUERY_VALUE);
  if (result == ERROR_SUCCESS) {
    DWORD size = 0;
    result = the_key.ReadValue(value, NULL, &size, &the_data.type);
    if (result == ERROR_FILE_NOT_FOUND) {
      // Add a mutation to delete the value.
      the_data.type = REG_NONE;
      revert_list_.push_front(the_data);
    } else if (result == ERROR_SUCCESS) {
      the_data.data.resize(size);
      result = the_key.ReadValue(value, &the_data.data[0], &size,
                                 &the_data.type);
      if (result == ERROR_SUCCESS) {
        revert_list_.push_front(the_data);
      } else {
        ::SetLastError(result);
        PLOG(ERROR) << __FUNCTION__ << " unexpected error reading data for "
            << value << " from key " << key
            << ". The current value will not be restored upon revert.";
      }
    } else {
      ::SetLastError(result);
      PLOG(ERROR) << __FUNCTION__ << " unexpected error reading " << value
          << " from key " << key
          << ". The current value will not be restored upon revert.";
    }
  } else if (result == ERROR_FILE_NOT_FOUND) {
    // Add a mutation to delete the value (but not any keys).
    revert_list_.push_front(the_data);
  } else {
    ::SetLastError(result);
    PLOG(ERROR) << __FUNCTION__ << " unexpected error opening key " << key
        << " to read value " << value
        << ". The current value will not be restored upon revert.";
  }
}

void RegistrySetter::AddDWORDValue(HKEY root,
                                   const wchar_t* key,
                                   const wchar_t* value,
                                   DWORD data) {
  const uint8* data_ptr = reinterpret_cast<uint8*>(&data);
  AddValue(root, key, value, REG_DWORD, data_ptr, sizeof(data));
}

void RegistrySetter::AddFILETIMEValue(HKEY root,
                                      const wchar_t* key,
                                      const wchar_t* value,
                                      FILETIME data) {
  const uint8* data_ptr = reinterpret_cast<uint8*>(&data);
  AddValue(root, key, value, REG_BINARY, data_ptr, sizeof(data));
}

// static
void RegistrySetter::ApplyList(const RegistryDataList& data_list) {
  base::win::RegKey key;
  LONG result = ERROR_SUCCESS;
  for (RegistryDataList::const_iterator scan(data_list.begin());
       scan != data_list.end(); ++scan) {
    const RegistryData& data = *scan;
    const bool do_delete = (data.type == REG_NONE && data.data.empty());
    result = do_delete ?
        key.Open(data.root, data.key, KEY_SET_VALUE) :
        key.Create(data.root, data.key, KEY_SET_VALUE);
    if (result == ERROR_SUCCESS) {
      if (do_delete) {
        result = key.DeleteValue(data.value);
      } else {
        result = key.WriteValue(data.value,
                                data.data.empty() ? NULL : &data.data[0],
                                data.data.size(), data.type);
      }
      if (result != ERROR_SUCCESS) {
        ::SetLastError(result);
        PLOG(ERROR) << "Failed to " << (do_delete ? "delete" : "set")
                    << " value " << data.value
                    << " in registry key " << data.key
                    << " in hive " << std::hex <<data.root << std::dec;
      }
    } else if (!do_delete || result != ERROR_FILE_NOT_FOUND) {
      ::SetLastError(result);
      PLOG(ERROR) << "Failed to create/open registry key " << data.key
                  << " in hive " << std::hex << data.root << std::dec;
    }
  }
}

void RegistrySetter::Apply() const {
  ApplyList(apply_list_);
}

void RegistrySetter::Revert() const {
  ApplyList(revert_list_);
}

// ConfiguratorDriver implementation.

ConfiguratorDriver::ConfiguratorDriver(IEConfigurator* configurator)
    : configurator_(configurator) {
  DCHECK(configurator);
}

ConfiguratorDriver::~ConfiguratorDriver() {
}

void ConfiguratorDriver::OnTestProgramStart(
    const testing::UnitTest& unit_test) {
  configurator_->Initialize();
}

void ConfiguratorDriver::OnTestStart(const testing::TestInfo& test_info) {
  configurator_->ApplySettings();
}

void ConfiguratorDriver::OnTestProgramEnd(const testing::UnitTest& unit_test) {
  configurator_->RevertSettings();
}

// IE7Configurator implementation

IE7Configurator::IE7Configurator() {
}

IE7Configurator::~IE7Configurator() {
}

void IE7Configurator::Initialize() {
  // Suppress the friendly "Hi!  I popped up an info bar for you.  Did you see
  // the info bar I just showed you?  There's a yellow thing in your IE window
  // that wasn't there before!  I call it an Information Bar.  Did you notice
  // the Information Bar?" dialog that it likes to show.
  setter_.AddDWORDValue(HKEY_CURRENT_USER, kKeyIEInformationBar,
                        kValueFirstTime, 0);
}

void IE7Configurator::ApplySettings() {
  setter_.Apply();
}

void IE7Configurator::RevertSettings() {
  setter_.Revert();
}

// ModernIEConfigurator implementation

const ModernIEConfigurator::RunOnceValueNames
    ModernIEConfigurator::kIE8ValueNames = {
  kValueIE8Completed,
  kValueIE8CompletionTime,
  kValueIE8RunOnceLastShown,
  kValueIE8RunOnceLastShownTimestamp,
};

const ModernIEConfigurator::RunOnceValueNames
    ModernIEConfigurator::kIE9ValueNames = {
  kValueIE9Completed,
  kValueIE9CompletionTime,
  kValueIE9RunOnceLastShown,
  kValueIE9RunOnceLastShownTimestamp,
};

const ModernIEConfigurator::RunOnceValueNames
    ModernIEConfigurator::kIE10ValueNames = {
  kValueIE10Completed,
  kValueIE10CompletionTime,
  kValueIE10RunOnceLastShown,
  kValueIE10RunOnceLastShownTimestamp,
};

ModernIEConfigurator::ModernIEConfigurator(IEVersion ie_version)
    : ie_version_(ie_version),
      run_once_value_names_(RunOnceNamesForVersion(ie_version)) {
}

ModernIEConfigurator::~ModernIEConfigurator() {
}

// static
const ModernIEConfigurator::RunOnceValueNames*
    ModernIEConfigurator::RunOnceNamesForVersion(
        IEVersion ie_version) {
  switch (ie_version) {
    case IE_8:
      return &kIE8ValueNames;
      break;
    case IE_9:
      return &kIE9ValueNames;
      break;
    case IE_10:
      return &kIE10ValueNames;
      break;
    default:
      NOTREACHED();
  }
  return NULL;
}

// Returns true if the per-user setup is complete.
bool ModernIEConfigurator::IsPerUserSetupComplete() {
  bool is_complete = false;
  base::win::RegKey key_main;

  if (key_main.Open(HKEY_CURRENT_USER, kKeyIEMain,
                    KEY_QUERY_VALUE) != ERROR_SUCCESS) {
    return false;
  }

  DWORD dword_value = 0;
  FILETIME shown_time = {};
  FILETIME completion_time = {};
  DWORD size = sizeof(completion_time);

  // See if the user has seen the first-run prompt.
  if (key_main.ReadValue(run_once_value_names_->last_shown_timestamp,
                         &shown_time, &size, NULL) != ERROR_SUCCESS ||
      size != sizeof(shown_time)) {
    return false;
  }

  // See if setup was completed.
  if (key_main.ReadValue(run_once_value_names_->completion_time,
                         &completion_time, &size, NULL) != ERROR_SUCCESS ||
      size != sizeof(completion_time)) {
    return false;
  }

  // See if setup was completed after the last time the prompt was shown.
  base::Time time_shown = base::Time::FromFileTime(shown_time);
  base::Time time_completed = base::Time::FromFileTime(completion_time);
  if (time_shown >= time_completed)
    return false;

  return true;
}

// Returns the path to the IE9 Approved Extensions key for Chrome Frame.
// static
base::string16 ModernIEConfigurator::GetChromeFrameBHOCLSID() {
  base::string16 bho_guid(39, L'\0');
  int guid_len = StringFromGUID2(CLSID_ChromeFrameBHO, &bho_guid[0],
                                 bho_guid.size());
  DCHECK_EQ(guid_len, static_cast<int>(bho_guid.size()));
  bho_guid.resize(guid_len - 1);
  return bho_guid;
}

// Returns true if the add-on enablement prompt is disabled by Group Policy.
// static
bool ModernIEConfigurator::IsAddonPromptDisabledForChromeFrame() {
  bool is_disabled = false;
  base::win::RegKey key;

  // See if the prompt is disabled for this user of IE.
  if (key.Open(HKEY_CURRENT_USER, kKeyIEApprovedExtensions,
               KEY_QUERY_VALUE) == ERROR_SUCCESS) {
    DWORD type = REG_NONE;
    DWORD size = 0;
    if (key.ReadValue(GetChromeFrameBHOCLSID().c_str(), NULL, &size,
                      &type) == ERROR_SUCCESS &&
        type == REG_BINARY) {
      is_disabled = true;
    }
  }

  // Now check if the prompt is disabled for all add-ons via Group Policy.
  if (!is_disabled &&
      key.Open(HKEY_LOCAL_MACHINE, kKeyPoliciesExt,
               KEY_QUERY_VALUE) == ERROR_SUCCESS) {
    DWORD ignore = 0;
    if (key.ReadValueDW(kValueIgnoreFrameApprovalCheck,
                        &ignore) == ERROR_SUCCESS &&
        ignore != 0) {
      is_disabled = true;
    }
  }

  return is_disabled;
}

void ModernIEConfigurator::Initialize() {
  // Check for per-user IE setup.
  if (!IsPerUserSetupComplete()) {
    base::Time time(base::Time::Now());
    const HKEY root = HKEY_CURRENT_USER;
    // Suppress the "Set up Internet Explorer" dialog.
    setter_.AddDWORDValue(root, kKeyIEMain, run_once_value_names_->last_shown,
                          1);
    setter_.AddDWORDValue(root, kKeyIEMain, run_once_value_names_->completed,
                          1);
    setter_.AddFILETIMEValue(root, kKeyIEMain,
                             run_once_value_names_->last_shown_timestamp,
                             time.ToFileTime());
    time += base::TimeDelta::FromMilliseconds(10);
    setter_.AddFILETIMEValue(root, kKeyIEMain,
                             run_once_value_names_->completion_time,
                             time.ToFileTime());
    if (ie_version_ < IE_10) {
      // Don't show a tour of IE 8 and 9.
      setter_.AddDWORDValue(
          root,
          kKeyIEMain,
          (ie_version_ == IE_8 ? kValueIE8TourNoShow : kValueIE9TourNoShow),
          1);
    }
    // Turn off the phishing filter.
    setter_.AddDWORDValue(
        root,
        kKeyIEPhishingFilter,
        (ie_version_ == IE_8 ? kValueEnabledV8 : kValueEnabledV9),
        0);
    // Don't download compatibility view lists.
    setter_.AddDWORDValue(root, kKeyIEBrowserEmulation,
                          kValueMSCompatibilityMode, 0);
  }

  // Turn off the "'Foo' add-on from 'Bar' is ready for use." prompt via Group
  // Policy.
  if (!IsAddonPromptDisabledForChromeFrame()) {
    setter_.AddDWORDValue(HKEY_LOCAL_MACHINE, kKeyPoliciesExt,
                          kValueIgnoreFrameApprovalCheck, 1);
  }
}

void ModernIEConfigurator::ApplySettings() {
  setter_.Apply();
}

void ModernIEConfigurator::RevertSettings() {
  setter_.Revert();
}

}  // namespace

// Configurator implementation.

IEConfigurator::IEConfigurator() {
}

IEConfigurator::~IEConfigurator() {
}

IEConfigurator* CreateConfigurator() {
  IEConfigurator* configurator = NULL;

  IEVersion ie_version = GetInstalledIEVersion();
  switch (ie_version) {
    case IE_7:
      configurator = new IE7Configurator();
      break;
    case IE_8:
    case IE_9:
    case IE_10:
      configurator = new ModernIEConfigurator(ie_version);
      break;
    default:
      break;
  }

  return configurator;
}

void InstallIEConfigurator() {
  IEConfigurator* configurator = CreateConfigurator();

  if (configurator != NULL) {
    testing::UnitTest::GetInstance()->listeners().Append(
        new ConfiguratorDriver(configurator));
  }
}

}  // namespace chrome_frame_test
