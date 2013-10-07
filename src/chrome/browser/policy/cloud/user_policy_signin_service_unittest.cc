// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/prefs/pref_service.h"
#include "base/run_loop.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/cloud/cloud_policy_constants.h"
#include "chrome/browser/policy/cloud/mock_device_management_service.h"
#include "chrome/browser/policy/cloud/mock_user_cloud_policy_store.h"
#include "chrome/browser/policy/cloud/user_cloud_policy_manager.h"
#include "chrome/browser/policy/cloud/user_policy_signin_service_factory.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/fake_signin_manager.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_pref_service_syncable.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "google_apis/gaia/gaia_constants.h"
#include "google_apis/gaia/google_service_auth_error.h"
#include "net/http/http_status_code.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_status.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_ANDROID)
#include "chrome/browser/policy/cloud/user_policy_signin_service_android.h"
#include "chrome/browser/signin/android_profile_oauth2_token_service.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#else
#include "chrome/browser/policy/cloud/user_policy_signin_service.h"
#include "chrome/browser/signin/token_service.h"
#include "chrome/browser/signin/token_service_factory.h"
#endif

namespace em = enterprise_management;

using testing::AnyNumber;
using testing::Mock;
using testing::_;

namespace policy {

namespace {

const char kTestUser[] = "testuser@test.com";

const char kValidTokenResponse[] =
    "{"
    "  \"access_token\": \"at1\","
    "  \"expires_in\": 3600,"
    "  \"token_type\": \"Bearer\""
    "}";

const char kHostedDomainResponse[] =
    "{"
    "  \"hd\": \"test.com\""
    "}";

const char kCombinedScopes[] =
    "https://www.googleapis.com/auth/chromeosdevicemanagement "
    "https://www.googleapis.com/auth/userinfo.email";

class SigninManagerFake : public FakeSigninManager {
 public:
  explicit SigninManagerFake(Profile* profile)
      : FakeSigninManager(profile) {
    Initialize(profile, NULL);
  }

  void ForceSignOut() {
    // Allow signing out now.
    prohibit_signout_ = false;
    SignOut();
  }

  static BrowserContextKeyedService* Build(content::BrowserContext* profile) {
    return new SigninManagerFake(static_cast<Profile*>(profile));
  }
};

#if defined(OS_ANDROID)

class FakeProfileOAuth2TokenService : public AndroidProfileOAuth2TokenService {
 public:
  explicit FakeProfileOAuth2TokenService(Profile* profile) {
    Initialize(profile);
  }

  static BrowserContextKeyedService* Build(content::BrowserContext* profile) {
    return new FakeProfileOAuth2TokenService(static_cast<Profile*>(profile));
  }

  // AndroidProfileOAuth2TokenService overrides:
  virtual void FetchOAuth2Token(
      const std::string& username,
      const std::string& scope,
      const FetchOAuth2TokenCallback& callback) OVERRIDE {
    ASSERT_TRUE(!HasPendingRequest());
    ASSERT_EQ(kTestUser, username);
    ASSERT_EQ(kCombinedScopes, scope);
    pending_callback_ = callback;
  }

  void IssueToken(const std::string& token) {
    ASSERT_TRUE(HasPendingRequest());
    GoogleServiceAuthError error = GoogleServiceAuthError::AuthErrorNone();
    if (token.empty())
      error = GoogleServiceAuthError::FromServiceError("fail");
    pending_callback_.Run(
        error, token, base::Time::Now() + base::TimeDelta::FromDays(1));
    pending_callback_.Reset();
  }

  bool HasPendingRequest() const {
    return !pending_callback_.is_null();
  }

 private:
  FetchOAuth2TokenCallback pending_callback_;
};

#endif

class UserPolicySigninServiceTest : public testing::Test {
 public:
  UserPolicySigninServiceTest()
      : thread_bundle_(content::TestBrowserThreadBundle::IO_MAINLOOP),
        register_completed_(false) {}

  MOCK_METHOD1(OnPolicyRefresh, void(bool));

  void OnRegisterCompleted(scoped_ptr<CloudPolicyClient> client) {
    register_completed_ = true;
    created_client_.swap(client);
  }

  void RegisterPolicyClientWithCallback(UserPolicySigninService* service) {
    service->RegisterPolicyClient(
        kTestUser,
#if !defined(OS_ANDROID)
        "mock_oauth_token",
#endif
        base::Bind(&UserPolicySigninServiceTest::OnRegisterCompleted,
                   base::Unretained(this)));
    ASSERT_TRUE(IsRequestActive());
  }

  virtual void SetUp() OVERRIDE {
    device_management_service_ = new MockDeviceManagementService();
    g_browser_process->browser_policy_connector()->
        SetDeviceManagementServiceForTesting(
            scoped_ptr<DeviceManagementService>(device_management_service_));

    local_state_.reset(new TestingPrefServiceSimple);
    chrome::RegisterLocalState(local_state_->registry());
    TestingBrowserProcess::GetGlobal()->SetLocalState(local_state_.get());

    scoped_refptr<net::URLRequestContextGetter> system_request_context;
    g_browser_process->browser_policy_connector()->Init(
        local_state_.get(), system_request_context);

    // Create a testing profile with cloud-policy-on-signin enabled, and bring
    // up a UserCloudPolicyManager with a MockUserCloudPolicyStore.
    scoped_ptr<TestingPrefServiceSyncable> prefs(
        new TestingPrefServiceSyncable());
    chrome::RegisterUserProfilePrefs(prefs->registry());
    TestingProfile::Builder builder;
    builder.SetPrefService(scoped_ptr<PrefServiceSyncable>(prefs.Pass()));
    profile_ = builder.Build().Pass();

    mock_store_ = new MockUserCloudPolicyStore();
    EXPECT_CALL(*mock_store_, Load()).Times(AnyNumber());
    manager_.reset(new UserCloudPolicyManager(
        profile_.get(), scoped_ptr<UserCloudPolicyStore>(mock_store_)));
    signin_manager_ = static_cast<SigninManagerFake*>(
        SigninManagerFactory::GetInstance()->SetTestingFactoryAndUse(
            profile_.get(), SigninManagerFake::Build));

#if defined(OS_ANDROID)
    ProfileOAuth2TokenServiceFactory* factory =
        ProfileOAuth2TokenServiceFactory::GetInstance();
    token_service_ = static_cast<FakeProfileOAuth2TokenService*>(
        factory->SetTestingFactoryAndUse(profile_.get(),
                                         FakeProfileOAuth2TokenService::Build));
#endif

    // Make sure the UserPolicySigninService is created.
    UserPolicySigninServiceFactory::GetForProfile(profile_.get());
    Mock::VerifyAndClearExpectations(mock_store_);
    url_factory_.set_remove_fetcher_on_delete(true);
  }

  virtual void TearDown() OVERRIDE {
    // Free the profile before we clear out the browser prefs.
    profile_.reset();
    TestingBrowserProcess* testing_browser_process =
        TestingBrowserProcess::GetGlobal();
    testing_browser_process->SetLocalState(NULL);
    local_state_.reset();
    testing_browser_process->SetBrowserPolicyConnector(NULL);
    base::RunLoop run_loop;
    run_loop.RunUntilIdle();
  }

  bool IsRequestActive() {
#if defined(OS_ANDROID)
    if (token_service_->HasPendingRequest())
      return true;
#endif
    return url_factory_.GetFetcherByID(0);
  }

  void MakeOAuthTokenFetchSucceed() {
#if defined(OS_ANDROID)
    ASSERT_TRUE(token_service_->HasPendingRequest());
    token_service_->IssueToken("fake_token");
#else
    ASSERT_TRUE(IsRequestActive());
    net::TestURLFetcher* fetcher = url_factory_.GetFetcherByID(0);
    fetcher->set_response_code(net::HTTP_OK);
    fetcher->SetResponseString(kValidTokenResponse);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
#endif
  }

  void ReportHostedDomainStatus(bool is_hosted_domain) {
    ASSERT_TRUE(IsRequestActive());
    net::TestURLFetcher* fetcher = url_factory_.GetFetcherByID(0);
    fetcher->set_response_code(net::HTTP_OK);
    fetcher->SetResponseString(is_hosted_domain ? kHostedDomainResponse : "{}");
    fetcher->delegate()->OnURLFetchComplete(fetcher);
  }

  void TestSuccessfulSignin() {
    UserPolicySigninService* signin_service =
        UserPolicySigninServiceFactory::GetForProfile(profile_.get());
    EXPECT_CALL(*this, OnPolicyRefresh(true)).Times(0);
    RegisterPolicyClientWithCallback(signin_service);

    // Mimic successful oauth token fetch.
    MakeOAuthTokenFetchSucceed();

    // When the user is from a hosted domain, this should kick off client
    // registration.
    MockDeviceManagementJob* register_request = NULL;
    EXPECT_CALL(*device_management_service_,
                CreateJob(DeviceManagementRequestJob::TYPE_REGISTRATION))
        .WillOnce(device_management_service_->CreateAsyncJob(
            &register_request));
    EXPECT_CALL(*device_management_service_, StartJob(_, _, _, _, _, _, _))
        .Times(1);

    // Now mimic the user being a hosted domain - this should cause a Register()
    // call.
    ReportHostedDomainStatus(true);

    // Should have no more outstanding requests.
    ASSERT_FALSE(IsRequestActive());
    Mock::VerifyAndClearExpectations(this);
    ASSERT_TRUE(register_request);

    // Mimic successful client registration - this should register the client
    // and invoke the callback.
    em::DeviceManagementResponse registration_blob;
    registration_blob.mutable_register_response()->set_device_management_token(
        "dm_token");
    registration_blob.mutable_register_response()->set_enrollment_type(
        em::DeviceRegisterResponse::ENTERPRISE);
    register_request->SendResponse(DM_STATUS_SUCCESS, registration_blob);

    // UserCloudPolicyManager should not be initialized yet.
    ASSERT_FALSE(manager_->core()->service());
    EXPECT_TRUE(register_completed_);
    EXPECT_TRUE(created_client_.get());

    // Now call to fetch policy - this should fire off a fetch request.
    MockDeviceManagementJob* fetch_request = NULL;
    EXPECT_CALL(*device_management_service_,
                CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
        .WillOnce(device_management_service_->CreateAsyncJob(&fetch_request));
    EXPECT_CALL(*device_management_service_, StartJob(_, _, _, _, _, _, _))
        .Times(1);
    signin_service->FetchPolicyForSignedInUser(
        created_client_.Pass(),
        base::Bind(&UserPolicySigninServiceTest::OnPolicyRefresh,
                   base::Unretained(this)));

    Mock::VerifyAndClearExpectations(this);
    ASSERT_TRUE(fetch_request);

    // UserCloudPolicyManager should now be initialized.
    ASSERT_TRUE(manager_->core()->service());

    // Make the policy fetch succeed - this should result in a write to the
    // store and ultimately result in a call to OnPolicyRefresh().
    EXPECT_CALL(*mock_store_, Store(_));
    EXPECT_CALL(*this, OnPolicyRefresh(true)).Times(1);

    // Create a fake policy blob to deliver to the client.
    em::DeviceManagementResponse policy_blob;
    em::PolicyData policy_data;
    policy_data.set_policy_type(dm_protocol::kChromeUserPolicyType);
    em::PolicyFetchResponse* policy_response =
        policy_blob.mutable_policy_response()->add_response();
    ASSERT_TRUE(policy_data.SerializeToString(
        policy_response->mutable_policy_data()));
    fetch_request->SendResponse(DM_STATUS_SUCCESS, policy_blob);

    // Complete the store which should cause the policy fetch callback to be
    // invoked.
    mock_store_->NotifyStoreLoaded();
    Mock::VerifyAndClearExpectations(this);
  }

  scoped_ptr<TestingProfile> profile_;
  // Weak pointer to a MockUserCloudPolicyStore - lifetime is managed by the
  // UserCloudPolicyManager.
  MockUserCloudPolicyStore* mock_store_;
  scoped_ptr<UserCloudPolicyManager> manager_;

  // BrowserPolicyConnector and UrlFetcherFactory want to initialize and free
  // various components asynchronously via tasks, so create fake threads here.
  content::TestBrowserThreadBundle thread_bundle_;

  net::TestURLFetcherFactory url_factory_;

  SigninManagerFake* signin_manager_;
#if defined(OS_ANDROID)
  FakeProfileOAuth2TokenService* token_service_;  // Not owned.
#endif

  // Used in conjunction with OnRegisterCompleted() to test client registration
  // callbacks.
  scoped_ptr<CloudPolicyClient> created_client_;

  // True if OnRegisterCompleted() was called.
  bool register_completed_;

  // Weak ptr to the MockDeviceManagementService (object is owned by the
  // BrowserPolicyConnector).
  MockDeviceManagementService* device_management_service_;

  scoped_ptr<TestingPrefServiceSimple> local_state_;
};

TEST_F(UserPolicySigninServiceTest, InitWhileSignedOut) {
  EXPECT_CALL(*mock_store_, Clear());
  // Make sure user is not signed in.
  ASSERT_TRUE(SigninManagerFactory::GetForProfile(profile_.get())->
      GetAuthenticatedUsername().empty());

  // Let the SigninService know that the profile has been created.
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PROFILE_ADDED,
      content::Source<Profile>(profile_.get()),
      content::NotificationService::NoDetails());

  // UserCloudPolicyManager should not be initialized.
  ASSERT_FALSE(manager_->core()->service());
}

  // TODO(joaodasilva): these tests rely on issuing the OAuth2 login refresh
  // token after signin. Revisit this after figuring how to handle that on
  // Android.
#if !defined(OS_ANDROID)

TEST_F(UserPolicySigninServiceTest, InitWhileSignedIn) {
  // Set the user as signed in.
  SigninManagerFactory::GetForProfile(profile_.get())->SetAuthenticatedUsername(
      kTestUser);

  // Let the SigninService know that the profile has been created.
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PROFILE_ADDED,
      content::Source<Profile>(profile_.get()),
      content::NotificationService::NoDetails());

  // UserCloudPolicyManager should be initialized.
  ASSERT_TRUE(manager_->core()->service());

  // Complete initialization of the store.
  mock_store_->NotifyStoreLoaded();

  // No oauth access token yet, so client registration should be deferred.
  ASSERT_FALSE(IsRequestActive());

  // Make oauth token available.
  TokenServiceFactory::GetForProfile(profile_.get())->IssueAuthTokenForTest(
      GaiaConstants::kGaiaOAuth2LoginRefreshToken, "oauth_login_refresh_token");

  // Client registration should be in progress since we now have an oauth token.
  ASSERT_TRUE(IsRequestActive());
}

TEST_F(UserPolicySigninServiceTest, SignInAfterInit) {
  EXPECT_CALL(*mock_store_, Clear());
  // Let the SigninService know that the profile has been created.
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PROFILE_ADDED,
      content::Source<Profile>(profile_.get()),
      content::NotificationService::NoDetails());

  // UserCloudPolicyManager should not be initialized since there is no
  // signed-in user.
  ASSERT_FALSE(manager_->core()->service());

  // Now sign in the user.
  SigninManagerFactory::GetForProfile(profile_.get())->SetAuthenticatedUsername(
      kTestUser);

  // Complete initialization of the store.
  mock_store_->NotifyStoreLoaded();

  // Make oauth token available.
  TokenServiceFactory::GetForProfile(profile_.get())->IssueAuthTokenForTest(
      GaiaConstants::kGaiaOAuth2LoginRefreshToken, "oauth_login_refresh_token");

  // UserCloudPolicyManager should be initialized.
  ASSERT_TRUE(manager_->core()->service());

  // Client registration should be in progress since we have an oauth token.
  ASSERT_TRUE(IsRequestActive());
}

TEST_F(UserPolicySigninServiceTest, SignInWithNonEnterpriseUser) {
  EXPECT_CALL(*mock_store_, Clear());
  // Let the SigninService know that the profile has been created.
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PROFILE_ADDED,
      content::Source<Profile>(profile_.get()),
      content::NotificationService::NoDetails());

  // UserCloudPolicyManager should not be initialized since there is no
  // signed-in user.
  ASSERT_FALSE(manager_->core()->service());

  // Now sign in a non-enterprise user (blacklisted gmail.com domain).
  SigninManagerFactory::GetForProfile(profile_.get())->SetAuthenticatedUsername(
      "non_enterprise_user@gmail.com");

  // Complete initialization of the store.
  mock_store_->NotifyStoreLoaded();

  // Make oauth token available.
  TokenServiceFactory::GetForProfile(profile_.get())->IssueAuthTokenForTest(
      GaiaConstants::kGaiaOAuth2LoginRefreshToken, "oauth_login_refresh_token");

  // UserCloudPolicyManager should not be initialized and there should be no
  // DMToken request active.
  ASSERT_TRUE(!manager_->core()->service());
  ASSERT_FALSE(IsRequestActive());
}

TEST_F(UserPolicySigninServiceTest, UnregisteredClient) {
  EXPECT_CALL(*mock_store_, Clear());
  // Let the SigninService know that the profile has been created.
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PROFILE_ADDED,
      content::Source<Profile>(profile_.get()),
      content::NotificationService::NoDetails());

  // UserCloudPolicyManager should not be initialized since there is no
  // signed-in user.
  ASSERT_FALSE(manager_->core()->service());

  // Now sign in the user.
  SigninManagerFactory::GetForProfile(profile_.get())->SetAuthenticatedUsername(
      kTestUser);

  // Make oauth token available.
  TokenServiceFactory::GetForProfile(profile_.get())->IssueAuthTokenForTest(
      GaiaConstants::kGaiaOAuth2LoginRefreshToken, "oauth_login_refresh_token");

  // UserCloudPolicyManager should be initialized.
  ASSERT_TRUE(manager_->core()->service());

  // Client registration should not be in progress since the store is not
  // yet initialized.
  ASSERT_FALSE(IsRequestActive());

  // Complete initialization of the store with no policy (unregistered client).
  mock_store_->NotifyStoreLoaded();

  // Client registration should be in progress since we have an oauth token.
  ASSERT_TRUE(IsRequestActive());
}

TEST_F(UserPolicySigninServiceTest, RegisteredClient) {
  EXPECT_CALL(*mock_store_, Clear());
  // Let the SigninService know that the profile has been created.
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PROFILE_ADDED,
      content::Source<Profile>(profile_.get()),
      content::NotificationService::NoDetails());

  // UserCloudPolicyManager should not be initialized since there is no
  // signed-in user.
  ASSERT_FALSE(manager_->core()->service());

  // Now sign in the user.
  SigninManagerFactory::GetForProfile(profile_.get())->SetAuthenticatedUsername(
      kTestUser);

  // Make oauth token available.
  TokenServiceFactory::GetForProfile(profile_.get())->IssueAuthTokenForTest(
      GaiaConstants::kGaiaOAuth2LoginRefreshToken, "oauth_login_refresh_token");

  // UserCloudPolicyManager should be initialized.
  ASSERT_TRUE(manager_->core()->service());

  // Client registration should not be in progress since the store is not
  // yet initialized.
  ASSERT_FALSE(manager_->IsClientRegistered());
  ASSERT_FALSE(IsRequestActive());

  mock_store_->policy_.reset(new enterprise_management::PolicyData());
  mock_store_->policy_->set_request_token("fake token");
  mock_store_->policy_->set_device_id("fake client id");

  // Complete initialization of the store.
  mock_store_->NotifyStoreLoaded();

  // Client registration should not be in progress since the client should be
  // already registered.
  ASSERT_TRUE(manager_->IsClientRegistered());
  ASSERT_FALSE(IsRequestActive());
}

#endif  // !defined(OS_ANDROID)

TEST_F(UserPolicySigninServiceTest, SignOutAfterInit) {
  EXPECT_CALL(*mock_store_, Clear());
  // Set the user as signed in.
  SigninManagerFactory::GetForProfile(profile_.get())->SetAuthenticatedUsername(
      kTestUser);

  // Let the SigninService know that the profile has been created.
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PROFILE_ADDED,
      content::Source<Profile>(profile_.get()),
      content::NotificationService::NoDetails());

  // UserCloudPolicyManager should be initialized.
  ASSERT_TRUE(manager_->core()->service());

  // Now sign out.
  SigninManagerFactory::GetForProfile(profile_.get())->SignOut();

  // UserCloudPolicyManager should be shut down.
  ASSERT_FALSE(manager_->core()->service());
}

TEST_F(UserPolicySigninServiceTest, RegisterPolicyClientOAuthFailure) {
  UserPolicySigninService* signin_service =
      UserPolicySigninServiceFactory::GetForProfile(profile_.get());
  RegisterPolicyClientWithCallback(signin_service);
  Mock::VerifyAndClearExpectations(this);

  // UserCloudPolicyManager should not be initialized.
  ASSERT_FALSE(manager_->core()->service());
  ASSERT_TRUE(IsRequestActive());
  EXPECT_FALSE(register_completed_);

  // Cause the access token fetch to fail - callback should be invoked.
#if defined(OS_ANDROID)
  ASSERT_TRUE(token_service_->HasPendingRequest());
  token_service_->IssueToken("");
#else
  net::TestURLFetcher* fetcher = url_factory_.GetFetcherByID(0);
  fetcher->set_status(net::URLRequestStatus(net::URLRequestStatus::FAILED, -1));
  fetcher->delegate()->OnURLFetchComplete(fetcher);
#endif

  EXPECT_TRUE(register_completed_);
  EXPECT_FALSE(created_client_.get());
  EXPECT_FALSE(IsRequestActive());
}

TEST_F(UserPolicySigninServiceTest, RegisterPolicyClientNonHostedDomain) {
  UserPolicySigninService* signin_service =
      UserPolicySigninServiceFactory::GetForProfile(profile_.get());
  RegisterPolicyClientWithCallback(signin_service);

  // UserCloudPolicyManager should not be initialized.
  ASSERT_FALSE(manager_->core()->service());
  ASSERT_TRUE(IsRequestActive());

  // Cause the access token request to succeed.
  MakeOAuthTokenFetchSucceed();

  // Should be a follow-up fetch to check the hosted-domain status.
  ASSERT_TRUE(IsRequestActive());
  Mock::VerifyAndClearExpectations(this);

  EXPECT_FALSE(register_completed_);

  // Report that the user is not on a hosted domain - callback should be
  // invoked reporting a failed fetch.
  ReportHostedDomainStatus(false);

  // Since this is not a hosted domain, we should not issue a request for a
  // DMToken.
  EXPECT_TRUE(register_completed_);
  EXPECT_FALSE(created_client_.get());
  ASSERT_FALSE(IsRequestActive());
}

TEST_F(UserPolicySigninServiceTest, RegisterPolicyClientFailedRegistration) {
  UserPolicySigninService* signin_service =
      UserPolicySigninServiceFactory::GetForProfile(profile_.get());
  RegisterPolicyClientWithCallback(signin_service);

  // UserCloudPolicyManager should not be initialized.
  ASSERT_FALSE(manager_->core()->service());

  // Mimic successful oauth token fetch.
  MakeOAuthTokenFetchSucceed();

  EXPECT_FALSE(register_completed_);

  // When the user is from a hosted domain, this should kick off client
  // registration.
  MockDeviceManagementJob* register_request = NULL;
  EXPECT_CALL(*device_management_service_,
              CreateJob(DeviceManagementRequestJob::TYPE_REGISTRATION))
      .WillOnce(device_management_service_->CreateAsyncJob(&register_request));
  EXPECT_CALL(*device_management_service_, StartJob(_, _, _, _, _, _, _))
        .Times(1);

  // Now mimic the user being a hosted domain - this should cause a Register()
  // call.
  ReportHostedDomainStatus(true);

  // Should have no more outstanding requests.
  ASSERT_FALSE(IsRequestActive());
  Mock::VerifyAndClearExpectations(this);
  ASSERT_TRUE(register_request);
  EXPECT_FALSE(register_completed_);

  // Make client registration fail (hosted domain user that is not managed).
  register_request->SendResponse(DM_STATUS_SERVICE_MANAGEMENT_NOT_SUPPORTED,
                                 em::DeviceManagementResponse());
  EXPECT_TRUE(register_completed_);
  EXPECT_FALSE(created_client_.get());
}

TEST_F(UserPolicySigninServiceTest, RegisterPolicyClientSucceeded) {
  UserPolicySigninService* signin_service =
      UserPolicySigninServiceFactory::GetForProfile(profile_.get());
  RegisterPolicyClientWithCallback(signin_service);

  // Mimic successful oauth token fetch.
  MakeOAuthTokenFetchSucceed();

  // When the user is from a hosted domain, this should kick off client
  // registration.
  MockDeviceManagementJob* register_request = NULL;
  EXPECT_CALL(*device_management_service_,
              CreateJob(DeviceManagementRequestJob::TYPE_REGISTRATION))
      .WillOnce(device_management_service_->CreateAsyncJob(&register_request));
  EXPECT_CALL(*device_management_service_, StartJob(_, _, _, _, _, _, _))
      .Times(1);

  // Now mimic the user being a hosted domain - this should cause a Register()
  // call.
  ReportHostedDomainStatus(true);

  // Should have no more outstanding requests.
  ASSERT_FALSE(IsRequestActive());
  Mock::VerifyAndClearExpectations(this);
  ASSERT_TRUE(register_request);
  EXPECT_FALSE(register_completed_);

  em::DeviceManagementResponse registration_blob;
  registration_blob.mutable_register_response()->set_device_management_token(
      "dm_token");
  registration_blob.mutable_register_response()->set_enrollment_type(
      em::DeviceRegisterResponse::ENTERPRISE);
  register_request->SendResponse(DM_STATUS_SUCCESS, registration_blob);
  Mock::VerifyAndClearExpectations(this);
  EXPECT_TRUE(register_completed_);
  EXPECT_TRUE(created_client_.get());
  // UserCloudPolicyManager should not be initialized.
  ASSERT_FALSE(manager_->core()->service());
}

TEST_F(UserPolicySigninServiceTest, FetchPolicyFailed) {
  scoped_ptr<CloudPolicyClient> client =
      UserCloudPolicyManager::CreateCloudPolicyClient(
          device_management_service_);
  client->SetupRegistration("mock_dm_token", "mock_client_id");

  // Initiate a policy fetch request.
  MockDeviceManagementJob* fetch_request = NULL;
  EXPECT_CALL(*device_management_service_,
              CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
      .WillOnce(device_management_service_->CreateAsyncJob(&fetch_request));
  EXPECT_CALL(*device_management_service_, StartJob(_, _, _, _, _, _, _))
      .Times(1);
  UserPolicySigninService* signin_service =
      UserPolicySigninServiceFactory::GetForProfile(profile_.get());
  signin_service->FetchPolicyForSignedInUser(
      client.Pass(),
      base::Bind(&UserPolicySigninServiceTest::OnPolicyRefresh,
                 base::Unretained(this)));
  ASSERT_TRUE(fetch_request);

  // Make the policy fetch fail.
  EXPECT_CALL(*this, OnPolicyRefresh(false)).Times(1);
  fetch_request->SendResponse(DM_STATUS_REQUEST_FAILED,
                              em::DeviceManagementResponse());
  // UserCloudPolicyManager should be initialized.
  ASSERT_TRUE(manager_->core()->service());
}

TEST_F(UserPolicySigninServiceTest, FetchPolicySuccess) {
  ASSERT_NO_FATAL_FAILURE(TestSuccessfulSignin());
}

TEST_F(UserPolicySigninServiceTest, SignOutThenSignInAgain) {
  ASSERT_NO_FATAL_FAILURE(TestSuccessfulSignin());

  EXPECT_CALL(*mock_store_, Clear());
  signin_manager_->ForceSignOut();
  ASSERT_FALSE(manager_->core()->service());

  // Now sign in again.
  ASSERT_NO_FATAL_FAILURE(TestSuccessfulSignin());
}

TEST_F(UserPolicySigninServiceTest, PolicyFetchFailureTemporary) {
  ASSERT_NO_FATAL_FAILURE(TestSuccessfulSignin());

  ASSERT_TRUE(manager_->IsClientRegistered());

  // Kick off another policy fetch.
  MockDeviceManagementJob* fetch_request = NULL;
  EXPECT_CALL(*device_management_service_,
              CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
      .WillOnce(device_management_service_->CreateAsyncJob(&fetch_request));
  EXPECT_CALL(*device_management_service_, StartJob(_, _, _, _, _, _, _))
      .Times(1);
  manager_->RefreshPolicies();
  Mock::VerifyAndClearExpectations(this);

  // Now, fake a transient error from the server on this policy fetch. This
  // should have no impact on the cached policy.
  fetch_request->SendResponse(DM_STATUS_REQUEST_FAILED,
                              em::DeviceManagementResponse());
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(manager_->IsClientRegistered());
}

TEST_F(UserPolicySigninServiceTest, PolicyFetchFailureDisableManagement) {
  ASSERT_NO_FATAL_FAILURE(TestSuccessfulSignin());

  EXPECT_TRUE(manager_->IsClientRegistered());
#if !defined(OS_ANDROID)
  EXPECT_TRUE(signin_manager_->IsSignoutProhibited());
#endif

  // Kick off another policy fetch.
  MockDeviceManagementJob* fetch_request = NULL;
  EXPECT_CALL(*device_management_service_,
              CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
      .WillOnce(device_management_service_->CreateAsyncJob(&fetch_request));
  EXPECT_CALL(*device_management_service_, StartJob(_, _, _, _, _, _, _))
      .Times(1);
  manager_->RefreshPolicies();
  Mock::VerifyAndClearExpectations(this);

  // Now, fake a SC_FORBIDDEN error from the server on this policy fetch. This
  // indicates that chrome management is disabled and will result in the cached
  // policy being removed and the manager shut down.
  EXPECT_CALL(*mock_store_, Clear());
  fetch_request->SendResponse(DM_STATUS_SERVICE_MANAGEMENT_NOT_SUPPORTED,
                              em::DeviceManagementResponse());
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(manager_->IsClientRegistered());
#if !defined(OS_ANDROID)
  EXPECT_FALSE(signin_manager_->IsSignoutProhibited());
#endif
}

}  // namespace

}  // namespace policy
