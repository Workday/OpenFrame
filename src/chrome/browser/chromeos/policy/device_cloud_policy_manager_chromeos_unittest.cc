// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/device_cloud_policy_manager_chromeos.h"

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/prefs/pref_registry_simple.h"
#include "base/prefs/testing_pref_service.h"
#include "base/run_loop.h"
#include "chrome/browser/chromeos/policy/device_cloud_policy_store_chromeos.h"
#include "chrome/browser/chromeos/policy/enterprise_install_attributes.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_oauth2_token_service.h"
#include "chrome/browser/chromeos/settings/device_oauth2_token_service_factory.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#include "chrome/browser/chromeos/settings/device_settings_test_helper.h"
#include "chrome/browser/policy/cloud/cloud_policy_client.h"
#include "chrome/browser/policy/cloud/mock_device_management_service.h"
#include "chrome/browser/policy/external_data_fetcher.h"
#include "chrome/browser/policy/proto/chromeos/chrome_device_policy.pb.h"
#include "chrome/browser/policy/proto/cloud/device_management_backend.pb.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chromeos/cryptohome/cryptohome_library.h"
#include "chromeos/cryptohome/mock_cryptohome_library.h"
#include "chromeos/dbus/cryptohome_client.h"
#include "chromeos/dbus/dbus_client_implementation_type.h"
#include "google_apis/gaia/gaia_oauth_client.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "net/url_request/url_request_test_util.h"
#include "policy/policy_constants.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::AnyNumber;
using testing::AtMost;
using testing::DoAll;
using testing::Mock;
using testing::SaveArg;
using testing::_;

namespace em = enterprise_management;

namespace policy {
namespace {

void CopyLockResult(base::RunLoop* loop,
                    EnterpriseInstallAttributes::LockResult* out,
                    EnterpriseInstallAttributes::LockResult result) {
  *out = result;
  loop->Quit();
}

class DeviceCloudPolicyManagerChromeOSTest
    : public chromeos::DeviceSettingsTestBase {
 protected:
  DeviceCloudPolicyManagerChromeOSTest()
      : cryptohome_library_(chromeos::CryptohomeLibrary::GetTestImpl()),
        stub_cryptohome_client_(chromeos::CryptohomeClient::Create(
            chromeos::STUB_DBUS_CLIENT_IMPLEMENTATION, NULL)),
        install_attributes_(cryptohome_library_.get(),
                            stub_cryptohome_client_.get()),
        store_(new DeviceCloudPolicyStoreChromeOS(&device_settings_service_,
                                                  &install_attributes_)),
        manager_(make_scoped_ptr(store_), &install_attributes_) {}

  virtual void SetUp() OVERRIDE {
    DeviceSettingsTestBase::SetUp();
    chrome::RegisterLocalState(local_state_.registry());
    manager_.Init();

    // DeviceOAuth2TokenService uses the system request context to fetch
    // OAuth tokens, then writes the token to local state, encrypting it
    // first with methods in CryptohomeLibrary.
    request_context_getter_ = new net::TestURLRequestContextGetter(
        loop_.message_loop_proxy());
    TestingBrowserProcess::GetGlobal()->SetSystemRequestContext(
        request_context_getter_.get());
    TestingBrowserProcess::GetGlobal()->SetLocalState(&local_state_);
    chromeos::DeviceOAuth2TokenServiceFactory::Initialize();
    chromeos::CryptohomeLibrary::SetForTest(cryptohome_library_.get());
    url_fetcher_response_code_ = 200;
    url_fetcher_response_string_ = "{\"access_token\":\"accessToken4Test\","
                                   "\"expires_in\":1234,"
                                   "\"refresh_token\":\"refreshToken4Test\"}";
  }

  virtual void TearDown() OVERRIDE {
    manager_.Shutdown();
    DeviceSettingsTestBase::TearDown();

    chromeos::DeviceOAuth2TokenServiceFactory::Shutdown();
    chromeos::CryptohomeLibrary::SetForTest(NULL);
    TestingBrowserProcess::GetGlobal()->SetLocalState(NULL);
  }

  scoped_ptr<chromeos::CryptohomeLibrary> cryptohome_library_;
  scoped_ptr<chromeos::CryptohomeClient> stub_cryptohome_client_;
  EnterpriseInstallAttributes install_attributes_;

  scoped_refptr<net::URLRequestContextGetter> request_context_getter_;
  net::TestURLFetcherFactory url_fetcher_factory_;
  int url_fetcher_response_code_;
  string url_fetcher_response_string_;
  TestingPrefServiceSimple local_state_;
  MockDeviceManagementService device_management_service_;
  chromeos::ScopedTestDeviceSettingsService test_device_settings_service_;
  chromeos::ScopedTestCrosSettings test_cros_settings_;

  DeviceCloudPolicyStoreChromeOS* store_;
  DeviceCloudPolicyManagerChromeOS manager_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DeviceCloudPolicyManagerChromeOSTest);
};

TEST_F(DeviceCloudPolicyManagerChromeOSTest, FreshDevice) {
  owner_key_util_->Clear();
  FlushDeviceSettings();
  EXPECT_TRUE(manager_.IsInitializationComplete(POLICY_DOMAIN_CHROME));

  manager_.Connect(&local_state_,
                   &device_management_service_,
                   scoped_ptr<CloudPolicyClient::StatusProvider>());

  PolicyBundle bundle;
  EXPECT_TRUE(manager_.policies().Equals(bundle));
}

TEST_F(DeviceCloudPolicyManagerChromeOSTest, EnrolledDevice) {
  base::RunLoop loop;
  EnterpriseInstallAttributes::LockResult result;
  install_attributes_.LockDevice(PolicyBuilder::kFakeUsername,
                                 DEVICE_MODE_ENTERPRISE,
                                 PolicyBuilder::kFakeDeviceId,
                                 base::Bind(&CopyLockResult, &loop, &result));
  loop.Run();
  ASSERT_EQ(EnterpriseInstallAttributes::LOCK_SUCCESS, result);

  FlushDeviceSettings();
  EXPECT_EQ(CloudPolicyStore::STATUS_OK, store_->status());
  EXPECT_TRUE(manager_.IsInitializationComplete(POLICY_DOMAIN_CHROME));

  PolicyBundle bundle;
  bundle.Get(PolicyNamespace(POLICY_DOMAIN_CHROME, std::string()))
      .Set(key::kDeviceMetricsReportingEnabled,
           POLICY_LEVEL_MANDATORY,
           POLICY_SCOPE_MACHINE,
           Value::CreateBooleanValue(false),
           NULL);
  EXPECT_TRUE(manager_.policies().Equals(bundle));

  manager_.Connect(&local_state_,
                   &device_management_service_,
                   scoped_ptr<CloudPolicyClient::StatusProvider>());
  EXPECT_TRUE(manager_.policies().Equals(bundle));

  manager_.Shutdown();
  EXPECT_TRUE(manager_.policies().Equals(bundle));

  EXPECT_EQ(manager_.GetRobotAccountId(),
            PolicyBuilder::kFakeServiceAccountIdentity);
}

TEST_F(DeviceCloudPolicyManagerChromeOSTest, ConsumerDevice) {
  FlushDeviceSettings();
  EXPECT_EQ(CloudPolicyStore::STATUS_BAD_STATE, store_->status());
  EXPECT_TRUE(manager_.IsInitializationComplete(POLICY_DOMAIN_CHROME));

  PolicyBundle bundle;
  EXPECT_TRUE(manager_.policies().Equals(bundle));

  manager_.Connect(&local_state_,
                   &device_management_service_,
                   scoped_ptr<CloudPolicyClient::StatusProvider>());
  EXPECT_TRUE(manager_.policies().Equals(bundle));

  manager_.Shutdown();
  EXPECT_TRUE(manager_.policies().Equals(bundle));
}

class DeviceCloudPolicyManagerChromeOSEnrollmentTest
    : public DeviceCloudPolicyManagerChromeOSTest {
 public:
  void Done(EnrollmentStatus status) {
    status_ = status;
    done_ = true;
  }

 protected:
  DeviceCloudPolicyManagerChromeOSEnrollmentTest()
      : is_auto_enrollment_(false),
        register_status_(DM_STATUS_SUCCESS),
        policy_fetch_status_(DM_STATUS_SUCCESS),
        robot_auth_fetch_status_(DM_STATUS_SUCCESS),
        store_result_(true),
        status_(EnrollmentStatus::ForStatus(EnrollmentStatus::STATUS_SUCCESS)),
        done_(false) {}

  virtual void SetUp() OVERRIDE {
    DeviceCloudPolicyManagerChromeOSTest::SetUp();

    // Set up test data.
    device_policy_.SetDefaultNewSigningKey();
    device_policy_.policy_data().set_timestamp(
        (base::Time::NowFromSystemTime() -
         base::Time::UnixEpoch()).InMilliseconds());
    device_policy_.Build();

    register_response_.mutable_register_response()->set_device_management_token(
        PolicyBuilder::kFakeToken);
    policy_fetch_response_.mutable_policy_response()->add_response()->CopyFrom(
        device_policy_.policy());
    robot_auth_fetch_response_.mutable_service_api_access_response()
        ->set_auth_code("auth_code_for_test");
    loaded_blob_ = device_policy_.GetBlob();

    // Initialize the manager.
    FlushDeviceSettings();
    EXPECT_EQ(CloudPolicyStore::STATUS_BAD_STATE, store_->status());
    EXPECT_TRUE(manager_.IsInitializationComplete(POLICY_DOMAIN_CHROME));

    PolicyBundle bundle;
    EXPECT_TRUE(manager_.policies().Equals(bundle));

    manager_.Connect(&local_state_,
                     &device_management_service_,
                     scoped_ptr<CloudPolicyClient::StatusProvider>());
  }

  void ExpectFailedEnrollment(EnrollmentStatus::Status status) {
    EXPECT_EQ(status, status_.status());
    EXPECT_FALSE(store_->is_managed());
    PolicyBundle empty_bundle;
    EXPECT_TRUE(manager_.policies().Equals(empty_bundle));
  }

  void ExpectSuccessfulEnrollment() {
    EXPECT_EQ(EnrollmentStatus::STATUS_SUCCESS, status_.status());
    EXPECT_EQ(DEVICE_MODE_ENTERPRISE, install_attributes_.GetMode());
    EXPECT_TRUE(store_->has_policy());
    EXPECT_TRUE(store_->is_managed());
    ASSERT_TRUE(manager_.core()->client());
    EXPECT_TRUE(manager_.core()->client()->is_registered());

    PolicyBundle bundle;
    bundle.Get(PolicyNamespace(POLICY_DOMAIN_CHROME, std::string()))
        .Set(key::kDeviceMetricsReportingEnabled,
             POLICY_LEVEL_MANDATORY,
             POLICY_SCOPE_MACHINE,
             Value::CreateBooleanValue(false),
             NULL);
    EXPECT_TRUE(manager_.policies().Equals(bundle));
  }

  void RunTest() {
    // Trigger enrollment.
    MockDeviceManagementJob* register_job = NULL;
    EXPECT_CALL(device_management_service_,
                CreateJob(DeviceManagementRequestJob::TYPE_REGISTRATION))
        .Times(AtMost(1))
        .WillOnce(device_management_service_.CreateAsyncJob(&register_job));
    EXPECT_CALL(device_management_service_, StartJob(_, _, _, _, _, _, _))
        .Times(AtMost(1))
        .WillOnce(DoAll(SaveArg<5>(&client_id_),
                        SaveArg<6>(&register_request_)));
    DeviceCloudPolicyManagerChromeOS::AllowedDeviceModes modes;
    modes[DEVICE_MODE_ENTERPRISE] = true;
    manager_.StartEnrollment(
        "auth token", is_auto_enrollment_, modes,
        base::Bind(&DeviceCloudPolicyManagerChromeOSEnrollmentTest::Done,
                   base::Unretained(this)));
    Mock::VerifyAndClearExpectations(&device_management_service_);

    if (done_)
      return;

    // Process registration.
    ASSERT_TRUE(register_job);
    MockDeviceManagementJob* policy_fetch_job = NULL;
    EXPECT_CALL(device_management_service_,
                CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
        .Times(AtMost(1))
        .WillOnce(device_management_service_.CreateAsyncJob(&policy_fetch_job));
    EXPECT_CALL(device_management_service_, StartJob(_, _, _, _, _, _, _))
        .Times(AtMost(1));
    register_job->SendResponse(register_status_, register_response_);
    Mock::VerifyAndClearExpectations(&device_management_service_);

    if (done_)
      return;

    // Process policy fetch.
    ASSERT_TRUE(policy_fetch_job);
    policy_fetch_job->SendResponse(policy_fetch_status_,
                                   policy_fetch_response_);

    if (done_)
      return;

    // Process verification.
    MockDeviceManagementJob* robot_auth_fetch_job = NULL;
    EXPECT_CALL(device_management_service_,
                CreateJob(DeviceManagementRequestJob::TYPE_API_AUTH_CODE_FETCH))
        .Times(AtMost(1))
        .WillOnce(device_management_service_.CreateAsyncJob(
            &robot_auth_fetch_job));
    EXPECT_CALL(device_management_service_, StartJob(_, _, _, _, _, _, _))
        .Times(AtMost(1));
    base::RunLoop().RunUntilIdle();
    Mock::VerifyAndClearExpectations(&device_management_service_);

    if (done_)
      return;

    // Process robot auth token fetch.
    ASSERT_TRUE(robot_auth_fetch_job);
    robot_auth_fetch_job->SendResponse(robot_auth_fetch_status_,
                                       robot_auth_fetch_response_);
    Mock::VerifyAndClearExpectations(&device_management_service_);

    if (done_)
      return;

    // Process robot refresh token fetch if the auth code fetch succeeded.
    // DeviceCloudPolicyManagerChromeOS holds an EnrollmentHandlerChromeOS which
    // holds a GaiaOAuthClient that fetches the refresh token during enrollment.
    // We return a successful OAuth response via a TestURLFetcher to trigger the
    // happy path for these classes so that enrollment can continue.
    if (robot_auth_fetch_status_ == DM_STATUS_SUCCESS) {
      net::TestURLFetcher* url_fetcher = url_fetcher_factory_.GetFetcherByID(
          gaia::GaiaOAuthClient::kUrlFetcherId);
      ASSERT_TRUE(url_fetcher);
      url_fetcher->SetMaxRetriesOn5xx(0);
      url_fetcher->set_status(net::URLRequestStatus());
      url_fetcher->set_response_code(url_fetcher_response_code_);
      url_fetcher->SetResponseString(url_fetcher_response_string_);
      url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);
    }
    base::RunLoop().RunUntilIdle();

    // Process policy store.
    device_settings_test_helper_.set_store_result(store_result_);
    device_settings_test_helper_.FlushStore();
    EXPECT_EQ(device_policy_.GetBlob(),
              device_settings_test_helper_.policy_blob());

    if (done_)
      return;

    // Key installation, policy load and refresh token save.
    device_settings_test_helper_.set_policy_blob(loaded_blob_);
    owner_key_util_->SetPublicKeyFromPrivateKey(
        *device_policy_.GetNewSigningKey());
    ReloadDeviceSettings();

    if (done_)
      return;

    // Process robot refresh token store.
    EXPECT_EQ(
        "refreshToken4Test",
        chromeos::DeviceOAuth2TokenServiceFactory::Get()->GetRefreshToken());
  }

  bool is_auto_enrollment_;

  DeviceManagementStatus register_status_;
  em::DeviceManagementResponse register_response_;

  DeviceManagementStatus policy_fetch_status_;
  em::DeviceManagementResponse policy_fetch_response_;

  DeviceManagementStatus robot_auth_fetch_status_;
  em::DeviceManagementResponse robot_auth_fetch_response_;

  bool store_result_;
  std::string loaded_blob_;

  em::DeviceManagementRequest register_request_;
  std::string client_id_;
  EnrollmentStatus status_;

  bool done_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DeviceCloudPolicyManagerChromeOSEnrollmentTest);
};

TEST_F(DeviceCloudPolicyManagerChromeOSEnrollmentTest, Success) {
  RunTest();
  ExpectSuccessfulEnrollment();
}

TEST_F(DeviceCloudPolicyManagerChromeOSEnrollmentTest, AutoEnrollment) {
  is_auto_enrollment_ = true;
  RunTest();
  ExpectSuccessfulEnrollment();
  EXPECT_TRUE(register_request_.register_request().auto_enrolled());
}

TEST_F(DeviceCloudPolicyManagerChromeOSEnrollmentTest, Reenrollment) {
  base::RunLoop loop;
  EnterpriseInstallAttributes::LockResult result;
  install_attributes_.LockDevice(PolicyBuilder::kFakeUsername,
                                 DEVICE_MODE_ENTERPRISE,
                                 PolicyBuilder::kFakeDeviceId,
                                 base::Bind(&CopyLockResult, &loop, &result));
  loop.Run();
  ASSERT_EQ(EnterpriseInstallAttributes::LOCK_SUCCESS, result);

  RunTest();
  ExpectSuccessfulEnrollment();
  EXPECT_TRUE(register_request_.register_request().reregister());
  EXPECT_EQ(PolicyBuilder::kFakeDeviceId, client_id_);
}

TEST_F(DeviceCloudPolicyManagerChromeOSEnrollmentTest, RegistrationFailed) {
  register_status_ = DM_STATUS_REQUEST_FAILED;
  RunTest();
  ExpectFailedEnrollment(EnrollmentStatus::STATUS_REGISTRATION_FAILED);
  EXPECT_EQ(DM_STATUS_REQUEST_FAILED, status_.client_status());
}

// Policy server implementations are not required to support robot auth
// tokens, so the following 4 Robot* tests verify that enrollment succeeds
// even if the robot auth token fetch fails.
TEST_F(DeviceCloudPolicyManagerChromeOSEnrollmentTest,
       RobotAuthCodeFetchFailed) {
  robot_auth_fetch_status_ = DM_STATUS_REQUEST_FAILED;
  RunTest();
  ExpectSuccessfulEnrollment();
}

TEST_F(DeviceCloudPolicyManagerChromeOSEnrollmentTest,
       RobotRefreshTokenFetchResponseCodeFailed) {
  url_fetcher_response_code_ = 400;
  RunTest();
  ExpectSuccessfulEnrollment();
}

TEST_F(DeviceCloudPolicyManagerChromeOSEnrollmentTest,
       RobotRefreshTokenFetchResponseStringFailed) {
  url_fetcher_response_string_ = "invalid response json";
  RunTest();
  ExpectSuccessfulEnrollment();
}

TEST_F(DeviceCloudPolicyManagerChromeOSEnrollmentTest, RobotRefreshSaveFailed) {
  // Without a DeviceOAuth2TokenService, the refresh token can't be saved.
  chromeos::DeviceOAuth2TokenServiceFactory::Shutdown();
  RunTest();
  ExpectSuccessfulEnrollment();
}

TEST_F(DeviceCloudPolicyManagerChromeOSEnrollmentTest, PolicyFetchFailed) {
  policy_fetch_status_ = DM_STATUS_REQUEST_FAILED;
  RunTest();
  ExpectFailedEnrollment(EnrollmentStatus::STATUS_POLICY_FETCH_FAILED);
  EXPECT_EQ(DM_STATUS_REQUEST_FAILED, status_.client_status());
}

TEST_F(DeviceCloudPolicyManagerChromeOSEnrollmentTest, ValidationFailed) {
  device_policy_.policy().set_policy_data_signature("bad");
  policy_fetch_response_.clear_policy_response();
  policy_fetch_response_.mutable_policy_response()->add_response()->CopyFrom(
      device_policy_.policy());
  RunTest();
  ExpectFailedEnrollment(EnrollmentStatus::STATUS_VALIDATION_FAILED);
  EXPECT_EQ(CloudPolicyValidatorBase::VALIDATION_BAD_INITIAL_SIGNATURE,
            status_.validation_status());
}

TEST_F(DeviceCloudPolicyManagerChromeOSEnrollmentTest, StoreError) {
  store_result_ = false;
  RunTest();
  ExpectFailedEnrollment(EnrollmentStatus::STATUS_STORE_ERROR);
  EXPECT_EQ(CloudPolicyStore::STATUS_STORE_ERROR,
            status_.store_status());
}

TEST_F(DeviceCloudPolicyManagerChromeOSEnrollmentTest, LoadError) {
  loaded_blob_.clear();
  RunTest();
  ExpectFailedEnrollment(EnrollmentStatus::STATUS_STORE_ERROR);
  EXPECT_EQ(CloudPolicyStore::STATUS_LOAD_ERROR,
            status_.store_status());
}

}  // namespace
}  // namespace policy
