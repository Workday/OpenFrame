// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/prefs/pref_service.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/pref_names.h"

#if defined(OS_CHROMEOS)
#include "chromeos/dbus/fake_update_engine_client.h"
#include "chromeos/dbus/mock_dbus_thread_manager_without_gmock.h"

using chromeos::UpdateEngineClient;
#endif

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, GetIncognitoModeAvailability) {
  PrefService* pref_service = browser()->profile()->GetPrefs();
  pref_service->SetInteger(prefs::kIncognitoModeAvailability, 1);

  EXPECT_TRUE(RunComponentExtensionTest(
      "system/get_incognito_mode_availability")) << message_;
}

#if defined(OS_CHROMEOS)

class GetUpdateStatusApiTest : public ExtensionApiTest {
 public:
  GetUpdateStatusApiTest() : fake_update_engine_client_(NULL) {}

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    ExtensionApiTest::SetUpInProcessBrowserTestFixture();
    chromeos::MockDBusThreadManagerWithoutGMock* mock_dbus_thread_manager =
        new chromeos::MockDBusThreadManagerWithoutGMock;
    chromeos::DBusThreadManager::InitializeForTesting(mock_dbus_thread_manager);
    fake_update_engine_client_ =
        mock_dbus_thread_manager->fake_update_engine_client();
  }

  virtual void TearDownInProcessBrowserTestFixture() OVERRIDE {
    chromeos::DBusThreadManager::Shutdown();
    ExtensionApiTest::TearDownInProcessBrowserTestFixture();
  }

 protected:
  chromeos::FakeUpdateEngineClient* fake_update_engine_client_;

 private:
  DISALLOW_COPY_AND_ASSIGN(GetUpdateStatusApiTest);
};

IN_PROC_BROWSER_TEST_F(GetUpdateStatusApiTest, Progress) {
  UpdateEngineClient::Status status_not_available;
  status_not_available.status = UpdateEngineClient::UPDATE_STATUS_IDLE;
  UpdateEngineClient::Status status_updating;
  status_updating.status = UpdateEngineClient::UPDATE_STATUS_DOWNLOADING;
  status_updating.download_progress = 0.5;
  UpdateEngineClient::Status status_boot_needed;
  status_boot_needed.status =
      UpdateEngineClient::UPDATE_STATUS_UPDATED_NEED_REBOOT;

  // The fake client returns the last status in this order.
  fake_update_engine_client_->PushLastStatus(status_not_available);
  fake_update_engine_client_->PushLastStatus(status_updating);
  fake_update_engine_client_->PushLastStatus(status_boot_needed);

  ASSERT_TRUE(RunComponentExtensionTest(
      "system/get_update_status")) << message_;
}

#endif
