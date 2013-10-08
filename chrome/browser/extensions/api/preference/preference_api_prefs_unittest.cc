// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/prefs/mock_pref_change_callback.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/preference/preference_api.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_prefs_unittest.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "chrome/test/base/testing_profile.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::Value;

namespace keys = extension_manifest_keys;

namespace extensions {

namespace {

const char kPref1[] = "path1.subpath";
const char kPref2[] = "path2";
const char kPref3[] = "path3";
const char kPref4[] = "path4";

// Default values in case an extension pref value is not overridden.
const char kDefaultPref1[] = "default pref 1";
const char kDefaultPref2[] = "default pref 2";
const char kDefaultPref3[] = "default pref 3";
const char kDefaultPref4[] = "default pref 4";

}  // namespace

// An implementation of the PreferenceAPI which returns the ExtensionPrefs and
// ExtensionPrefValueMap from the TestExtensionPrefs, rather than from a
// profile (which we don't create in unittests).
class TestPreferenceAPI : public PreferenceAPIBase {
 public:
  explicit TestPreferenceAPI(TestExtensionPrefs* test_extension_prefs)
      : test_extension_prefs_(test_extension_prefs) { }
  ~TestPreferenceAPI() { }
 private:
  // PreferenceAPIBase implementation.
  virtual ExtensionPrefs* extension_prefs() OVERRIDE {
    return test_extension_prefs_->prefs();
  }
  virtual ExtensionPrefValueMap* extension_pref_value_map() OVERRIDE {
    return test_extension_prefs_->extension_pref_value_map();
  }

  TestExtensionPrefs* test_extension_prefs_;

  DISALLOW_COPY_AND_ASSIGN(TestPreferenceAPI);
};

class ExtensionControlledPrefsTest : public PrefsPrepopulatedTestBase {
 public:
  ExtensionControlledPrefsTest();
  virtual ~ExtensionControlledPrefsTest();

  virtual void RegisterPreferences(user_prefs::PrefRegistrySyncable* registry)
      OVERRIDE;
  void InstallExtensionControlledPref(Extension* extension,
                                      const std::string& key,
                                      base::Value* value);
  void InstallExtensionControlledPrefIncognito(Extension* extension,
                                               const std::string& key,
                                               base::Value* value);
  void InstallExtensionControlledPrefIncognitoSessionOnly(
      Extension* extension,
      const std::string& key,
      base::Value* value);
  void InstallExtension(Extension* extension);
  void UninstallExtension(const std::string& extension_id);

 protected:
  void EnsureExtensionInstalled(Extension* extension);
  void EnsureExtensionUninstalled(const std::string& extension_id);

  TestPreferenceAPI test_preference_api_;
};

ExtensionControlledPrefsTest::ExtensionControlledPrefsTest()
    : PrefsPrepopulatedTestBase(),
      test_preference_api_(&prefs_) {
}

ExtensionControlledPrefsTest::~ExtensionControlledPrefsTest() {
}

void ExtensionControlledPrefsTest::RegisterPreferences(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(
      kPref1, kDefaultPref1, user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterStringPref(
      kPref2, kDefaultPref2, user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterStringPref(
      kPref3, kDefaultPref3, user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterStringPref(
      kPref4, kDefaultPref4, user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

void ExtensionControlledPrefsTest::InstallExtensionControlledPref(
    Extension* extension,
    const std::string& key,
    Value* value) {
  EnsureExtensionInstalled(extension);
  test_preference_api_.SetExtensionControlledPref(
      extension->id(), key, kExtensionPrefsScopeRegular, value);
}

void ExtensionControlledPrefsTest::InstallExtensionControlledPrefIncognito(
    Extension* extension,
    const std::string& key,
    Value* value) {
  EnsureExtensionInstalled(extension);
  test_preference_api_.SetExtensionControlledPref(
      extension->id(), key, kExtensionPrefsScopeIncognitoPersistent, value);
}

void ExtensionControlledPrefsTest::
InstallExtensionControlledPrefIncognitoSessionOnly(Extension* extension,
                                                   const std::string& key,
                                                   Value* value) {
  EnsureExtensionInstalled(extension);
  test_preference_api_.SetExtensionControlledPref(
      extension->id(), key, kExtensionPrefsScopeIncognitoSessionOnly, value);
}

void ExtensionControlledPrefsTest::InstallExtension(Extension* extension) {
  EnsureExtensionInstalled(extension);
}

void ExtensionControlledPrefsTest::UninstallExtension(
    const std::string& extension_id) {
  EnsureExtensionUninstalled(extension_id);
}

void ExtensionControlledPrefsTest::EnsureExtensionInstalled(
    Extension* extension) {
  // Install extension the first time a preference is set for it.
  Extension* extensions[] = { extension1(),
                              extension2(),
                              extension3(),
                              extension4() };
  for (size_t i = 0; i < kNumInstalledExtensions; ++i) {
    if (extension == extensions[i] && !installed_[i]) {
      prefs()->OnExtensionInstalled(extension,
                                    Extension::ENABLED,
                                    Blacklist::NOT_BLACKLISTED,
                                    syncer::StringOrdinal());
      installed_[i] = true;
      break;
    }
  }
}

void ExtensionControlledPrefsTest::EnsureExtensionUninstalled(
    const std::string& extension_id) {
  Extension* extensions[] = { extension1(),
                              extension2(),
                              extension3(),
                              extension4() };
  for (size_t i = 0; i < kNumInstalledExtensions; ++i) {
    if (extensions[i]->id() == extension_id) {
      installed_[i] = false;
      break;
    }
  }
  prefs()->OnExtensionUninstalled(extension_id, Manifest::INTERNAL, false);
}

class ControlledPrefsInstallOneExtension
    : public ExtensionControlledPrefsTest {
  virtual void Initialize() OVERRIDE {
    InstallExtensionControlledPref(extension1(),
                                   kPref1,
                                   Value::CreateStringValue("val1"));
  }
  virtual void Verify() OVERRIDE {
    std::string actual = prefs()->pref_service()->GetString(kPref1);
    EXPECT_EQ("val1", actual);
  }
};
TEST_F(ControlledPrefsInstallOneExtension,
       ControlledPrefsInstallOneExtension) { }

// Check that we do not forget persistent incognito values after a reload.
class ControlledPrefsInstallIncognitoPersistent
    : public ExtensionControlledPrefsTest {
 public:
  virtual void Initialize() OVERRIDE {
    InstallExtensionControlledPref(
        extension1(), kPref1, Value::CreateStringValue("val1"));
    InstallExtensionControlledPrefIncognito(
        extension1(), kPref1, Value::CreateStringValue("val2"));
    scoped_ptr<PrefService> incog_prefs(prefs_.CreateIncognitoPrefService());
    std::string actual = incog_prefs->GetString(kPref1);
    EXPECT_EQ("val2", actual);
  }

  virtual void Verify() OVERRIDE {
    // Main pref service shall see only non-incognito settings.
    std::string actual = prefs()->pref_service()->GetString(kPref1);
    EXPECT_EQ("val1", actual);
    // Incognito pref service shall see incognito values.
    scoped_ptr<PrefService> incog_prefs(prefs_.CreateIncognitoPrefService());
    actual = incog_prefs->GetString(kPref1);
    EXPECT_EQ("val2", actual);
  }
};
TEST_F(ControlledPrefsInstallIncognitoPersistent,
       ControlledPrefsInstallIncognitoPersistent) { }

// Check that we forget 'session only' incognito values after a reload.
class ControlledPrefsInstallIncognitoSessionOnly
    : public ExtensionControlledPrefsTest {
 public:
  ControlledPrefsInstallIncognitoSessionOnly() : iteration_(0) {}

  virtual void Initialize() OVERRIDE {
    InstallExtensionControlledPref(
        extension1(), kPref1, Value::CreateStringValue("val1"));
    InstallExtensionControlledPrefIncognitoSessionOnly(
        extension1(), kPref1, Value::CreateStringValue("val2"));
    scoped_ptr<PrefService> incog_prefs(prefs_.CreateIncognitoPrefService());
    std::string actual = incog_prefs->GetString(kPref1);
    EXPECT_EQ("val2", actual);
  }
  virtual void Verify() OVERRIDE {
    // Main pref service shall see only non-incognito settings.
    std::string actual = prefs()->pref_service()->GetString(kPref1);
    EXPECT_EQ("val1", actual);
    // Incognito pref service shall see session-only incognito values only
    // during first run. Once the pref service was reloaded, all values shall be
    // discarded.
    scoped_ptr<PrefService> incog_prefs(prefs_.CreateIncognitoPrefService());
    actual = incog_prefs->GetString(kPref1);
    if (iteration_ == 0) {
      EXPECT_EQ("val2", actual);
    } else {
      EXPECT_EQ("val1", actual);
    }
    ++iteration_;
  }
  int iteration_;
};
TEST_F(ControlledPrefsInstallIncognitoSessionOnly,
       ControlledPrefsInstallIncognitoSessionOnly) { }

class ControlledPrefsUninstallExtension : public ExtensionControlledPrefsTest {
  virtual void Initialize() OVERRIDE {
    InstallExtensionControlledPref(
        extension1(), kPref1, Value::CreateStringValue("val1"));
    InstallExtensionControlledPref(
        extension1(), kPref2, Value::CreateStringValue("val2"));
    ContentSettingsStore* store = prefs()->content_settings_store();
    ContentSettingsPattern pattern =
        ContentSettingsPattern::FromString("http://[*.]example.com");
    store->SetExtensionContentSetting(extension1()->id(),
                                      pattern, pattern,
                                      CONTENT_SETTINGS_TYPE_IMAGES,
                                      std::string(),
                                      CONTENT_SETTING_BLOCK,
                                      kExtensionPrefsScopeRegular);

    UninstallExtension(extension1()->id());
  }
  virtual void Verify() OVERRIDE {
    EXPECT_FALSE(prefs()->HasPrefForExtension(extension1()->id()));

    std::string actual;
    actual = prefs()->pref_service()->GetString(kPref1);
    EXPECT_EQ(kDefaultPref1, actual);
    actual = prefs()->pref_service()->GetString(kPref2);
    EXPECT_EQ(kDefaultPref2, actual);
  }
};
TEST_F(ControlledPrefsUninstallExtension,
       ControlledPrefsUninstallExtension) { }

// Tests triggering of notifications to registered observers.
class ControlledPrefsNotifyWhenNeeded : public ExtensionControlledPrefsTest {
  virtual void Initialize() OVERRIDE {
    using testing::_;
    using testing::Mock;
    using testing::StrEq;

    MockPrefChangeCallback observer(prefs()->pref_service());
    PrefChangeRegistrar registrar;
    registrar.Init(prefs()->pref_service());
    registrar.Add(kPref1, observer.GetCallback());

    MockPrefChangeCallback incognito_observer(prefs()->pref_service());
    scoped_ptr<PrefService> incog_prefs(prefs_.CreateIncognitoPrefService());
    PrefChangeRegistrar incognito_registrar;
    incognito_registrar.Init(incog_prefs.get());
    incognito_registrar.Add(kPref1, incognito_observer.GetCallback());

    // Write value and check notification.
    EXPECT_CALL(observer, OnPreferenceChanged(_));
    EXPECT_CALL(incognito_observer, OnPreferenceChanged(_));
    InstallExtensionControlledPref(extension1(), kPref1,
        Value::CreateStringValue("https://www.chromium.org"));
    Mock::VerifyAndClearExpectations(&observer);
    Mock::VerifyAndClearExpectations(&incognito_observer);

    // Write same value.
    EXPECT_CALL(observer, OnPreferenceChanged(_)).Times(0);
    EXPECT_CALL(incognito_observer, OnPreferenceChanged(_)).Times(0);
    InstallExtensionControlledPref(extension1(), kPref1,
        Value::CreateStringValue("https://www.chromium.org"));
    Mock::VerifyAndClearExpectations(&observer);
    Mock::VerifyAndClearExpectations(&incognito_observer);

    // Change value.
    EXPECT_CALL(observer, OnPreferenceChanged(_));
    EXPECT_CALL(incognito_observer, OnPreferenceChanged(_));
    InstallExtensionControlledPref(extension1(), kPref1,
        Value::CreateStringValue("chrome://newtab"));
    Mock::VerifyAndClearExpectations(&observer);
    Mock::VerifyAndClearExpectations(&incognito_observer);
    // Change only incognito persistent value.
    EXPECT_CALL(observer, OnPreferenceChanged(_)).Times(0);
    EXPECT_CALL(incognito_observer, OnPreferenceChanged(_));
    InstallExtensionControlledPrefIncognito(extension1(), kPref1,
        Value::CreateStringValue("chrome://newtab2"));
    Mock::VerifyAndClearExpectations(&observer);
    Mock::VerifyAndClearExpectations(&incognito_observer);

    // Change only incognito session-only value.
    EXPECT_CALL(observer, OnPreferenceChanged(_)).Times(0);
    EXPECT_CALL(incognito_observer, OnPreferenceChanged(_));
    InstallExtensionControlledPrefIncognito(extension1(), kPref1,
        Value::CreateStringValue("chrome://newtab3"));
    Mock::VerifyAndClearExpectations(&observer);
    Mock::VerifyAndClearExpectations(&incognito_observer);

    // Uninstall.
    EXPECT_CALL(observer, OnPreferenceChanged(_));
    EXPECT_CALL(incognito_observer, OnPreferenceChanged(_));
    UninstallExtension(extension1()->id());
    Mock::VerifyAndClearExpectations(&observer);
    Mock::VerifyAndClearExpectations(&incognito_observer);

    registrar.Remove(kPref1);
    incognito_registrar.Remove(kPref1);
  }
  virtual void Verify() OVERRIDE {
    std::string actual = prefs()->pref_service()->GetString(kPref1);
    EXPECT_EQ(kDefaultPref1, actual);
  }
};
TEST_F(ControlledPrefsNotifyWhenNeeded,
       ControlledPrefsNotifyWhenNeeded) { }

// Tests disabling an extension.
class ControlledPrefsDisableExtension : public ExtensionControlledPrefsTest {
  virtual void Initialize() OVERRIDE {
    InstallExtensionControlledPref(
        extension1(), kPref1, Value::CreateStringValue("val1"));
    std::string actual = prefs()->pref_service()->GetString(kPref1);
    EXPECT_EQ("val1", actual);
    prefs()->SetExtensionState(extension1()->id(), Extension::DISABLED);
  }
  virtual void Verify() OVERRIDE {
    std::string actual = prefs()->pref_service()->GetString(kPref1);
    EXPECT_EQ(kDefaultPref1, actual);
  }
};
TEST_F(ControlledPrefsDisableExtension, ControlledPrefsDisableExtension) { }

// Tests disabling and reenabling an extension.
class ControlledPrefsReenableExtension : public ExtensionControlledPrefsTest {
  virtual void Initialize() OVERRIDE {
    InstallExtensionControlledPref(
        extension1(), kPref1, Value::CreateStringValue("val1"));
    prefs()->SetExtensionState(extension1()->id(), Extension::DISABLED);
    prefs()->SetExtensionState(extension1()->id(), Extension::ENABLED);
  }
  virtual void Verify() OVERRIDE {
    std::string actual = prefs()->pref_service()->GetString(kPref1);
    EXPECT_EQ("val1", actual);
  }
};
TEST_F(ControlledPrefsDisableExtension, ControlledPrefsReenableExtension) { }

// Mock class to test whether objects are deleted correctly.
class MockStringValue : public StringValue {
 public:
  explicit MockStringValue(const std::string& in_value)
      : StringValue(in_value) {
  }
  virtual ~MockStringValue() {
    Die();
  }
  MOCK_METHOD0(Die, void());
};

class ControlledPrefsSetExtensionControlledPref
    : public ExtensionControlledPrefsTest {
 public:
  virtual void Initialize() OVERRIDE {
    MockStringValue* v1 = new MockStringValue("https://www.chromium.org");
    MockStringValue* v2 = new MockStringValue("https://www.chromium.org");
    MockStringValue* v1i = new MockStringValue("https://www.chromium.org");
    MockStringValue* v2i = new MockStringValue("https://www.chromium.org");
    // Ownership is taken, value shall not be deleted.
    EXPECT_CALL(*v1, Die()).Times(0);
    EXPECT_CALL(*v1i, Die()).Times(0);
    InstallExtensionControlledPref(extension1(), kPref1, v1);
    InstallExtensionControlledPrefIncognito(extension1(), kPref1, v1i);
    testing::Mock::VerifyAndClearExpectations(v1);
    testing::Mock::VerifyAndClearExpectations(v1i);
    // Make sure there is no memory leak and both values are deleted.
    EXPECT_CALL(*v1, Die()).Times(1);
    EXPECT_CALL(*v1i, Die()).Times(1);
    EXPECT_CALL(*v2, Die()).Times(1);
    EXPECT_CALL(*v2i, Die()).Times(1);
    InstallExtensionControlledPref(extension1(), kPref1, v2);
    InstallExtensionControlledPrefIncognito(extension1(), kPref1, v2i);
    prefs_.RecreateExtensionPrefs();
    testing::Mock::VerifyAndClearExpectations(v1);
    testing::Mock::VerifyAndClearExpectations(v1i);
    testing::Mock::VerifyAndClearExpectations(v2);
    testing::Mock::VerifyAndClearExpectations(v2i);
  }

  virtual void Verify() OVERRIDE {
  }
};
TEST_F(ControlledPrefsSetExtensionControlledPref,
       ControlledPrefsSetExtensionControlledPref) { }

// Tests that the switches::kDisableExtensions command-line flag prevents
// extension controlled preferences from being enacted.
class ControlledPrefsDisableExtensions : public ExtensionControlledPrefsTest {
 public:
  ControlledPrefsDisableExtensions()
      : iteration_(0) {}
  virtual ~ControlledPrefsDisableExtensions() {}
  virtual void Initialize() OVERRIDE {
    InstallExtensionControlledPref(
        extension1(), kPref1, Value::CreateStringValue("val1"));
    // This becomes only active in the second verification phase.
    prefs_.set_extensions_disabled(true);
  }
  virtual void Verify() OVERRIDE {
    std::string actual = prefs()->pref_service()->GetString(kPref1);
    if (iteration_ == 0) {
      EXPECT_EQ("val1", actual);
      ++iteration_;
    } else {
      EXPECT_EQ(kDefaultPref1, actual);
    }
  }

 private:
  int iteration_;
};
TEST_F(ControlledPrefsDisableExtensions, ControlledPrefsDisableExtensions) { }

}  // namespace extensions
