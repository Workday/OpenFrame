// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/extensions/default_apps.h"
#include "chrome/browser/extensions/external_pref_loader.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

using default_apps::Provider;

namespace extensions {

class MockExternalLoader : public ExternalLoader {
 public:
  MockExternalLoader() {}

  virtual void StartLoading() OVERRIDE {}
 private:
  virtual ~MockExternalLoader() {}
};

class DefaultAppsTest : public testing::Test {
 public:
  DefaultAppsTest() : loop_(base::MessageLoop::TYPE_IO),
      ui_thread_(content::BrowserThread::UI, &loop_) {}
  virtual ~DefaultAppsTest() {}
 private:
  base::MessageLoop loop_;
  content::TestBrowserThread ui_thread_;
};

#if !defined(OS_CHROMEOS) && !defined(OS_ANDROID)
// Chrome OS has different way of installing default apps.
// Android does not currently support installing apps via Chrome.
TEST_F(DefaultAppsTest, Install) {
  scoped_ptr<TestingProfile> profile(new TestingProfile());
  ExternalLoader* loader = new MockExternalLoader();

  Provider provider(profile.get(), NULL, loader, Manifest::INTERNAL,
                    Manifest::INTERNAL, Extension::NO_FLAGS);

  // The default apps should be installed if kDefaultAppsInstallState
  // is unknown.
  EXPECT_TRUE(provider.ShouldInstallInProfile());
  int state = profile->GetPrefs()->GetInteger(prefs::kDefaultAppsInstallState);
  EXPECT_TRUE(state == default_apps::kAlreadyInstalledDefaultApps);

  // The default apps should only be installed once.
  EXPECT_FALSE(provider.ShouldInstallInProfile());
  state = profile->GetPrefs()->GetInteger(prefs::kDefaultAppsInstallState);
  EXPECT_TRUE(state == default_apps::kAlreadyInstalledDefaultApps);

  // The default apps should not be installed if the state is
  // kNeverProvideDefaultApps
  profile->GetPrefs()->SetInteger(prefs::kDefaultAppsInstallState,
      default_apps::kNeverInstallDefaultApps);
  EXPECT_FALSE(provider.ShouldInstallInProfile());
  state = profile->GetPrefs()->GetInteger(prefs::kDefaultAppsInstallState);
  EXPECT_TRUE(state == default_apps::kNeverInstallDefaultApps);

  // The old default apps with kAlwaysInstallDefaultAppss should be migrated.
  profile->GetPrefs()->SetInteger(prefs::kDefaultAppsInstallState,
      default_apps::kProvideLegacyDefaultApps);
  EXPECT_TRUE(provider.ShouldInstallInProfile());
  state = profile->GetPrefs()->GetInteger(prefs::kDefaultAppsInstallState);
  EXPECT_TRUE(state == default_apps::kAlreadyInstalledDefaultApps);

  class DefaultTestingProfile : public TestingProfile {
    virtual  bool WasCreatedByVersionOrLater(
        const std::string& version) OVERRIDE {
      return false;
    }
  };
  profile.reset(new DefaultTestingProfile);
  Provider provider2(profile.get(), NULL, loader, Manifest::INTERNAL,
                     Manifest::INTERNAL, Extension::NO_FLAGS);
  // The old default apps with kProvideLegacyDefaultApps should be migrated
  // even if the profile version is older than Chrome version.
  profile->GetPrefs()->SetInteger(prefs::kDefaultAppsInstallState,
      default_apps::kProvideLegacyDefaultApps);
  EXPECT_TRUE(provider2.ShouldInstallInProfile());
  state = profile->GetPrefs()->GetInteger(prefs::kDefaultAppsInstallState);
  EXPECT_TRUE(state == default_apps::kAlreadyInstalledDefaultApps);
}
#endif

}  // namespace extensions
