// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/parallel_authenticator.h"

#include <string>

#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/login/mock_login_status_consumer.h"
#include "chrome/browser/chromeos/login/mock_url_fetchers.h"
#include "chrome/browser/chromeos/login/mock_user_manager.h"
#include "chrome/browser/chromeos/login/test_attempt_state.h"
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_settings_test_helper.h"
#include "chrome/browser/chromeos/settings/stub_cros_settings_provider.h"
#include "chrome/test/base/testing_profile.h"
#include "chromeos/cryptohome/mock_async_method_caller.h"
#include "chromeos/cryptohome/mock_cryptohome_library.h"
#include "chromeos/dbus/fake_cryptohome_client.h"
#include "chromeos/dbus/mock_dbus_thread_manager_without_gmock.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "google_apis/gaia/mock_url_fetcher_factory.h"
#include "net/base/net_errors.h"
#include "net/url_request/url_request_status.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "url/gurl.h"

using ::testing::Invoke;
using ::testing::Return;
using ::testing::_;

namespace chromeos {

class TestOnlineAttempt : public OnlineAttempt {
 public:
  TestOnlineAttempt(AuthAttemptState* state,
                    AuthAttemptStateResolver* resolver)
      : OnlineAttempt(state, resolver) {
  }
};

class ParallelAuthenticatorTest : public testing::Test {
 public:
  ParallelAuthenticatorTest()
      : username_("me@nowhere.org"),
        password_("fakepass"),
        hash_ascii_("0a010000000000a0" + std::string(16, '0')),
        user_manager_enabler_(new MockUserManager) {
  }

  virtual ~ParallelAuthenticatorTest() {
    DCHECK(!mock_caller_);
  }

  virtual void SetUp() {
    mock_caller_ = new cryptohome::MockAsyncMethodCaller;
    cryptohome::AsyncMethodCaller::InitializeForTesting(mock_caller_);

    mock_cryptohome_library_ .reset(new MockCryptohomeLibrary());
    CryptohomeLibrary::SetForTest(mock_cryptohome_library_.get());

    auth_ = new ParallelAuthenticator(&consumer_);
    state_.reset(new TestAttemptState(UserContext(username_,
                                                  password_,
                                                  std::string()),
                                      hash_ascii_,
                                      "",
                                      "",
                                      User::USER_TYPE_REGULAR,
                                      false));
  }

  // Tears down the test fixture.
  virtual void TearDown() {
    CryptohomeLibrary::SetForTest(NULL);

    cryptohome::AsyncMethodCaller::Shutdown();
    mock_caller_ = NULL;
  }

  base::FilePath PopulateTempFile(const char* data, int data_len) {
    base::FilePath out;
    FILE* tmp_file = file_util::CreateAndOpenTemporaryFile(&out);
    EXPECT_NE(tmp_file, static_cast<FILE*>(NULL));
    EXPECT_EQ(file_util::WriteFile(out, data, data_len), data_len);
    EXPECT_TRUE(file_util::CloseFile(tmp_file));
    return out;
  }

  // Allow test to fail and exit gracefully, even if OnLoginFailure()
  // wasn't supposed to happen.
  void FailOnLoginFailure() {
    ON_CALL(consumer_, OnLoginFailure(_))
        .WillByDefault(Invoke(MockConsumer::OnFailQuitAndFail));
  }

  // Allow test to fail and exit gracefully, even if
  // OnRetailModeLoginSuccess() wasn't supposed to happen.
  void FailOnRetailModeLoginSuccess() {
    ON_CALL(consumer_, OnRetailModeLoginSuccess(_))
        .WillByDefault(Invoke(MockConsumer::OnRetailModeSuccessQuitAndFail));
  }

  // Allow test to fail and exit gracefully, even if OnLoginSuccess()
  // wasn't supposed to happen.
  void FailOnLoginSuccess() {
    ON_CALL(consumer_, OnLoginSuccess(_, _, _))
        .WillByDefault(Invoke(MockConsumer::OnSuccessQuitAndFail));
  }

  // Allow test to fail and exit gracefully, even if
  // OnOffTheRecordLoginSuccess() wasn't supposed to happen.
  void FailOnGuestLoginSuccess() {
    ON_CALL(consumer_, OnOffTheRecordLoginSuccess())
        .WillByDefault(Invoke(MockConsumer::OnGuestSuccessQuitAndFail));
  }

  void ExpectLoginFailure(const LoginFailure& failure) {
    EXPECT_CALL(consumer_, OnLoginFailure(failure))
        .WillOnce(Invoke(MockConsumer::OnFailQuit))
        .RetiresOnSaturation();
  }

  void ExpectRetailModeLoginSuccess() {
    EXPECT_CALL(consumer_, OnRetailModeLoginSuccess(_))
        .WillOnce(Invoke(MockConsumer::OnRetailModeSuccessQuit))
        .RetiresOnSaturation();
  }

  void ExpectLoginSuccess(const std::string& username,
                          const std::string& password,
                          const std::string& username_hash_,
                          bool pending) {
    EXPECT_CALL(consumer_, OnLoginSuccess(UserContext(username,
                                                      password,
                                                      std::string(),
                                                      username_hash_),
                                          pending,
                                          true /* using_oauth */))
        .WillOnce(Invoke(MockConsumer::OnSuccessQuit))
        .RetiresOnSaturation();
  }

  void ExpectGuestLoginSuccess() {
    EXPECT_CALL(consumer_, OnOffTheRecordLoginSuccess())
        .WillOnce(Invoke(MockConsumer::OnGuestSuccessQuit))
        .RetiresOnSaturation();
  }

  void ExpectPasswordChange() {
    EXPECT_CALL(consumer_, OnPasswordChangeDetected())
        .WillOnce(Invoke(MockConsumer::OnMigrateQuit))
        .RetiresOnSaturation();
  }

  void RunResolve(ParallelAuthenticator* auth) {
    auth->Resolve();
    base::MessageLoop::current()->RunUntilIdle();
  }

  void SetAttemptState(ParallelAuthenticator* auth, TestAttemptState* state) {
    auth->set_attempt_state(state);
  }

  ParallelAuthenticator::AuthState SetAndResolveState(
      ParallelAuthenticator* auth, TestAttemptState* state) {
    auth->set_attempt_state(state);
    return auth->ResolveState();
  }

  void SetOwnerState(bool owner_check_finished, bool check_result) {
    auth_->SetOwnerState(owner_check_finished, check_result);
  }

  void FakeOnlineAttempt() {
    auth_->set_online_attempt(new TestOnlineAttempt(state_.get(), auth_.get()));
  }

  content::TestBrowserThreadBundle thread_bundle_;

  std::string username_;
  std::string password_;
  std::string username_hash_;
  std::string hash_ascii_;

  ScopedStubNetworkLibraryEnabler stub_network_library_enabler_;
  ScopedDeviceSettingsTestHelper device_settings_test_helper_;
  ScopedTestCrosSettings test_cros_settings_;

  ScopedUserManagerEnabler user_manager_enabler_;

  scoped_ptr<MockCryptohomeLibrary> mock_cryptohome_library_;

  cryptohome::MockAsyncMethodCaller* mock_caller_;

  MockConsumer consumer_;
  scoped_refptr<ParallelAuthenticator> auth_;
  scoped_ptr<TestAttemptState> state_;
};

TEST_F(ParallelAuthenticatorTest, OnLoginSuccess) {
  EXPECT_CALL(consumer_, OnLoginSuccess(UserContext(username_,
                                                    password_,
                                                    std::string(),
                                                    username_hash_),
                                        false, true /* using oauth */))
      .Times(1)
      .RetiresOnSaturation();

  SetAttemptState(auth_.get(), state_.release());
  auth_->OnLoginSuccess(false);
}

TEST_F(ParallelAuthenticatorTest, OnPasswordChangeDetected) {
  EXPECT_CALL(consumer_, OnPasswordChangeDetected())
      .Times(1)
      .RetiresOnSaturation();
  SetAttemptState(auth_.get(), state_.release());
  auth_->OnPasswordChangeDetected();
}

TEST_F(ParallelAuthenticatorTest, ResolveNothingDone) {
  EXPECT_EQ(ParallelAuthenticator::CONTINUE,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, ResolvePossiblePwChange) {
  // Set a fake online attempt so that we return intermediate cryptohome state.
  FakeOnlineAttempt();

  // Set up state as though a cryptohome mount attempt has occurred
  // and been rejected.
  state_->PresetCryptohomeStatus(false, cryptohome::MOUNT_ERROR_KEY_FAILURE);

  EXPECT_EQ(ParallelAuthenticator::POSSIBLE_PW_CHANGE,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, ResolvePossiblePwChangeToFailedMount) {
  // Set up state as though a cryptohome mount attempt has occurred
  // and been rejected.
  state_->PresetCryptohomeStatus(false, cryptohome::MOUNT_ERROR_KEY_FAILURE);

  // When there is no online attempt and online results, POSSIBLE_PW_CHANGE
  EXPECT_EQ(ParallelAuthenticator::FAILED_MOUNT,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, ResolveNeedOldPw) {
  // Set up state as though a cryptohome mount attempt has occurred
  // and been rejected because of unmatched key; additionally,
  // an online auth attempt has completed successfully.
  state_->PresetCryptohomeStatus(false, cryptohome::MOUNT_ERROR_KEY_FAILURE);
  state_->PresetOnlineLoginStatus(LoginFailure::LoginFailureNone());

  EXPECT_EQ(ParallelAuthenticator::NEED_OLD_PW,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, ResolveOwnerNeededDirectFailedMount) {
  // Set up state as though a cryptohome mount attempt has occurred
  // and succeeded but we are in safe mode and the current user is not owner.
  // This is a high level test to verify the proper transitioning in this mode
  // only. It is not testing that we properly verify that the user is an owner
  // or that we really are in "safe-mode".
  state_->PresetCryptohomeStatus(true, cryptohome::MOUNT_ERROR_NONE);
  SetOwnerState(true, false);

  EXPECT_EQ(ParallelAuthenticator::OWNER_REQUIRED,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, ResolveOwnerNeededMount) {
  // Set up state as though a cryptohome mount attempt has occurred
  // and succeeded but we are in safe mode and the current user is not owner.
  // This test will check that the "safe-mode" policy is not set and will let
  // the mount finish successfully.
  state_->PresetCryptohomeStatus(true, cryptohome::MOUNT_ERROR_NONE);
  SetOwnerState(false, false);
  // and test that the mount has succeeded.
  state_.reset(new TestAttemptState(UserContext(username_,
                                                password_,
                                                std::string()),
                                    hash_ascii_,
                                    "",
                                    "",
                                    User::USER_TYPE_REGULAR,
                                    false));
  state_->PresetCryptohomeStatus(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_EQ(ParallelAuthenticator::OFFLINE_LOGIN,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, ResolveOwnerNeededFailedMount) {
  FailOnLoginSuccess();  // Set failing on success as the default...
  LoginFailure failure = LoginFailure(LoginFailure::OWNER_REQUIRED);
  ExpectLoginFailure(failure);

  MockDBusThreadManagerWithoutGMock* mock_dbus_thread_manager =
      new MockDBusThreadManagerWithoutGMock;
  DBusThreadManager::InitializeForTesting(mock_dbus_thread_manager);
  FakeCryptohomeClient* fake_cryptohome_client  =
      mock_dbus_thread_manager->fake_cryptohome_client();
  fake_cryptohome_client->set_unmount_result(true);

  CrosSettingsProvider* device_settings_provider;
  StubCrosSettingsProvider stub_settings_provider;
  // Set up state as though a cryptohome mount attempt has occurred
  // and succeeded but we are in safe mode and the current user is not owner.
  state_->PresetCryptohomeStatus(true, cryptohome::MOUNT_ERROR_NONE);
  SetOwnerState(false, false);
  // Remove the real DeviceSettingsProvider and replace it with a stub.
  device_settings_provider =
      CrosSettings::Get()->GetProvider(chromeos::kReportDeviceVersionInfo);
  EXPECT_TRUE(device_settings_provider != NULL);
  EXPECT_TRUE(
      CrosSettings::Get()->RemoveSettingsProvider(device_settings_provider));
  CrosSettings::Get()->AddSettingsProvider(&stub_settings_provider);
  CrosSettings::Get()->SetBoolean(kPolicyMissingMitigationMode, true);

  EXPECT_EQ(ParallelAuthenticator::CONTINUE,
            SetAndResolveState(auth_.get(), state_.release()));
  // Let the owner verification run.
  device_settings_test_helper_.Flush();
  // and test that the mount has succeeded.
  state_.reset(new TestAttemptState(UserContext(username_,
                                                password_,
                                                std::string()),
                                    hash_ascii_,
                                    "",
                                    "",
                                    User::USER_TYPE_REGULAR,
                                    false));
  state_->PresetCryptohomeStatus(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_EQ(ParallelAuthenticator::OWNER_REQUIRED,
            SetAndResolveState(auth_.get(), state_.release()));

  EXPECT_TRUE(
      CrosSettings::Get()->RemoveSettingsProvider(&stub_settings_provider));
  CrosSettings::Get()->AddSettingsProvider(device_settings_provider);
  DBusThreadManager::Get()->Shutdown();
}

TEST_F(ParallelAuthenticatorTest, DriveFailedMount) {
  FailOnLoginSuccess();
  ExpectLoginFailure(LoginFailure(LoginFailure::COULD_NOT_MOUNT_CRYPTOHOME));

  // Set up state as though a cryptohome mount attempt has occurred
  // and failed.
  state_->PresetCryptohomeStatus(false, cryptohome::MOUNT_ERROR_NONE);
  SetAttemptState(auth_.get(), state_.release());

  RunResolve(auth_.get());
}

TEST_F(ParallelAuthenticatorTest, DriveGuestLogin) {
  ExpectGuestLoginSuccess();
  FailOnLoginFailure();

  // Set up mock cryptohome library to respond as though a tmpfs mount
  // attempt has occurred and succeeded.
  mock_caller_->SetUp(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncMountGuest(_))
      .Times(1)
      .RetiresOnSaturation();

  auth_->LoginOffTheRecord();
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, DriveGuestLoginButFail) {
  FailOnGuestLoginSuccess();
  ExpectLoginFailure(LoginFailure(LoginFailure::COULD_NOT_MOUNT_TMPFS));

  // Set up mock cryptohome library to respond as though a tmpfs mount
  // attempt has occurred and failed.
  mock_caller_->SetUp(false, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncMountGuest(_))
      .Times(1)
      .RetiresOnSaturation();

  auth_->LoginOffTheRecord();
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, DriveRetailModeUserLogin) {
  ExpectRetailModeLoginSuccess();
  FailOnLoginFailure();

  // Set up mock cryptohome library to respond as though a tmpfs mount
  // attempt has occurred and succeeded.
  mock_caller_->SetUp(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncMountGuest(_))
      .Times(1)
      .RetiresOnSaturation();

  auth_->LoginRetailMode();
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, DriveRetailModeLoginButFail) {
  FailOnRetailModeLoginSuccess();
  ExpectLoginFailure(LoginFailure(LoginFailure::COULD_NOT_MOUNT_TMPFS));

  // Set up mock cryptohome library to respond as though a tmpfs mount
  // attempt has occurred and failed.
  mock_caller_->SetUp(false, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncMountGuest(_))
      .Times(1)
      .RetiresOnSaturation();

  auth_->LoginRetailMode();
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, DriveDataResync) {
  ExpectLoginSuccess(username_,
                     password_,
                     cryptohome::MockAsyncMethodCaller::kFakeSanitizedUsername,
                     false);
  FailOnLoginFailure();

  // Set up mock cryptohome library to respond successfully to a cryptohome
  // remove attempt and a cryptohome create attempt (indicated by the
  // |CREATE_IF_MISSING| flag to AsyncMount).
  mock_caller_->SetUp(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncRemove(username_, _))
      .Times(1)
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_caller_, AsyncMount(username_, hash_ascii_,
                                        cryptohome::CREATE_IF_MISSING, _))
      .Times(1)
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_caller_, AsyncGetSanitizedUsername(username_, _))
      .Times(1)
      .RetiresOnSaturation();

  state_->PresetOnlineLoginStatus(LoginFailure::LoginFailureNone());
  SetAttemptState(auth_.get(), state_.release());

  auth_->ResyncEncryptedData();
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, DriveResyncFail) {
  FailOnLoginSuccess();
  ExpectLoginFailure(LoginFailure(LoginFailure::DATA_REMOVAL_FAILED));

  // Set up mock cryptohome library to fail a cryptohome remove attempt.
  mock_caller_->SetUp(false, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncRemove(username_, _))
      .Times(1)
      .RetiresOnSaturation();

  SetAttemptState(auth_.get(), state_.release());

  auth_->ResyncEncryptedData();
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, DriveRequestOldPassword) {
  FailOnLoginSuccess();
  ExpectPasswordChange();

  state_->PresetCryptohomeStatus(false, cryptohome::MOUNT_ERROR_KEY_FAILURE);
  state_->PresetOnlineLoginStatus(LoginFailure::LoginFailureNone());
  SetAttemptState(auth_.get(), state_.release());

  RunResolve(auth_.get());
}

TEST_F(ParallelAuthenticatorTest, DriveDataRecover) {
  ExpectLoginSuccess(username_,
                     password_,
                     cryptohome::MockAsyncMethodCaller::kFakeSanitizedUsername,
                     false);
  FailOnLoginFailure();

  // Set up mock cryptohome library to respond successfully to a key migration.
  mock_caller_->SetUp(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncMigrateKey(username_, _, hash_ascii_, _))
      .Times(1)
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_caller_, AsyncMount(username_, hash_ascii_,
                                        cryptohome::MOUNT_FLAGS_NONE, _))
      .Times(1)
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_caller_, AsyncGetSanitizedUsername(username_, _))
        .Times(1)
        .RetiresOnSaturation();
  EXPECT_CALL(*mock_cryptohome_library_, GetSystemSalt())
      .WillOnce(Return(std::string()))
      .RetiresOnSaturation();

  state_->PresetOnlineLoginStatus(LoginFailure::LoginFailureNone());
  SetAttemptState(auth_.get(), state_.release());

  auth_->RecoverEncryptedData(std::string());
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, DriveDataRecoverButFail) {
  FailOnLoginSuccess();
  ExpectPasswordChange();

  // Set up mock cryptohome library to fail a key migration attempt,
  // asserting that the wrong password was used.
  mock_caller_->SetUp(false, cryptohome::MOUNT_ERROR_KEY_FAILURE);
  EXPECT_CALL(*mock_caller_, AsyncMigrateKey(username_, _, hash_ascii_, _))
      .Times(1)
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_cryptohome_library_, GetSystemSalt())
      .WillOnce(Return(std::string()))
      .RetiresOnSaturation();

  SetAttemptState(auth_.get(), state_.release());

  auth_->RecoverEncryptedData(std::string());
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, ResolveNoMount) {
  // Set a fake online attempt so that we return intermediate cryptohome state.
  FakeOnlineAttempt();

  // Set up state as though a cryptohome mount attempt has occurred
  // and been rejected because the user doesn't exist.
  state_->PresetCryptohomeStatus(false,
                                 cryptohome::MOUNT_ERROR_USER_DOES_NOT_EXIST);

  EXPECT_EQ(ParallelAuthenticator::NO_MOUNT,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, ResolveNoMountToFailedMount) {
  // Set up state as though a cryptohome mount attempt has occurred
  // and been rejected because the user doesn't exist.
  state_->PresetCryptohomeStatus(false,
                                 cryptohome::MOUNT_ERROR_USER_DOES_NOT_EXIST);

  // When there is no online attempt and online results, NO_MOUNT will be
  // resolved to FAILED_MOUNT.
  EXPECT_EQ(ParallelAuthenticator::FAILED_MOUNT,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, ResolveCreateNew) {
  // Set up state as though a cryptohome mount attempt has occurred
  // and been rejected because the user doesn't exist; additionally,
  // an online auth attempt has completed successfully.
  state_->PresetCryptohomeStatus(false,
                                 cryptohome::MOUNT_ERROR_USER_DOES_NOT_EXIST);
  state_->PresetOnlineLoginStatus(LoginFailure::LoginFailureNone());

  EXPECT_EQ(ParallelAuthenticator::CREATE_NEW,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, DriveCreateForNewUser) {
  ExpectLoginSuccess(username_,
                     password_,
                     cryptohome::MockAsyncMethodCaller::kFakeSanitizedUsername,
                     false);
  FailOnLoginFailure();

  // Set up mock cryptohome library to respond successfully to a cryptohome
  // create attempt (indicated by the |CREATE_IF_MISSING| flag to AsyncMount).
  mock_caller_->SetUp(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncMount(username_, hash_ascii_,
                                        cryptohome::CREATE_IF_MISSING, _))
      .Times(1)
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_caller_, AsyncGetSanitizedUsername(username_, _))
      .Times(1)
      .RetiresOnSaturation();

  // Set up state as though a cryptohome mount attempt has occurred
  // and been rejected because the user doesn't exist; additionally,
  // an online auth attempt has completed successfully.
  state_->PresetCryptohomeStatus(false,
                                 cryptohome::MOUNT_ERROR_USER_DOES_NOT_EXIST);
  state_->PresetOnlineLoginStatus(LoginFailure::LoginFailureNone());
  SetAttemptState(auth_.get(), state_.release());

  RunResolve(auth_.get());
}

TEST_F(ParallelAuthenticatorTest, DriveOfflineLogin) {
  ExpectLoginSuccess(username_, password_, username_hash_, false);
  FailOnLoginFailure();

  // Set up state as though a cryptohome mount attempt has occurred and
  // succeeded.
  state_->PresetCryptohomeStatus(true, cryptohome::MOUNT_ERROR_NONE);
  SetAttemptState(auth_.get(), state_.release());

  RunResolve(auth_.get());
}

TEST_F(ParallelAuthenticatorTest, DriveOnlineLogin) {
  ExpectLoginSuccess(username_, password_, username_hash_, false);
  FailOnLoginFailure();

  // Set up state as though a cryptohome mount attempt has occurred and
  // succeeded.
  state_->PresetCryptohomeStatus(true, cryptohome::MOUNT_ERROR_NONE);
  state_->PresetOnlineLoginStatus(LoginFailure::LoginFailureNone());
  SetAttemptState(auth_.get(), state_.release());

  RunResolve(auth_.get());
}

TEST_F(ParallelAuthenticatorTest, DriveUnlock) {
  ExpectLoginSuccess(username_, std::string(), std::string(), false);
  FailOnLoginFailure();

  // Set up mock cryptohome library to respond successfully to a cryptohome
  // key-check attempt.
  mock_caller_->SetUp(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncCheckKey(username_, _, _))
      .Times(1)
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_cryptohome_library_, GetSystemSalt())
      .WillOnce(Return(std::string()))
      .RetiresOnSaturation();

  auth_->AuthenticateToUnlock(UserContext(username_,
                                          std::string(),
                                          std::string()));
  base::MessageLoop::current()->Run();
}

}  // namespace chromeos
