// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/login_utils.h"

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/scoped_temp_dir.h"
#include "base/message_loop/message_loop.h"
#include "base/path_service.h"
#include "base/prefs/pref_registry_simple.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/threading/thread.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/input_method/input_method_configuration.h"
#include "chrome/browser/chromeos/input_method/mock_input_method_manager.h"
#include "chrome/browser/chromeos/login/authenticator.h"
#include "chrome/browser/chromeos/login/login_status_consumer.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/policy/enterprise_install_attributes.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#include "chrome/browser/chromeos/settings/device_settings_test_helper.h"
#include "chrome/browser/chromeos/settings/mock_owner_key_util.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/net/predictor.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/cloud/device_management_service.h"
#include "chrome/browser/policy/policy_service.h"
#include "chrome/browser/policy/proto/cloud/device_management_backend.pb.h"
#include "chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/rlz/rlz.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/scoped_testing_local_state.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/cryptohome/mock_async_method_caller.h"
#include "chromeos/cryptohome/mock_cryptohome_library.h"
#include "chromeos/dbus/mock_dbus_thread_manager_without_gmock.h"
#include "chromeos/disks/disk_mount_manager.h"
#include "chromeos/disks/mock_disk_mount_manager.h"
#include "chromeos/login/login_state.h"
#include "chromeos/network/network_handler.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/test_browser_thread.h"
#include "content/public/test/test_utils.h"
#include "google_apis/gaia/gaia_auth_consumer.h"
#include "google_apis/gaia/gaia_urls.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_status.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(ENABLE_RLZ)
#include "rlz/lib/rlz_value_store.h"
#endif

using ::testing::AnyNumber;

namespace chromeos {

namespace {

namespace em = enterprise_management;

using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::_;
using content::BrowserThread;

const char kTrue[] = "true";
const char kFalse[] = "false";
const char kDomain[] = "domain.com";
const char kUsername[] = "user@domain.com";
const char kMode[] = "enterprise";
const char kDeviceId[] = "100200300";
const char kUsernameOtherDomain[] = "user@other.com";
const char kAttributeOwned[] = "enterprise.owned";
const char kAttributeOwner[] = "enterprise.user";
const char kAttributeConsumerKiosk[] = "consumer.app_kiosk_enabled";
const char kAttrEnterpriseDomain[] = "enterprise.domain";
const char kAttrEnterpriseMode[] = "enterprise.mode";
const char kAttrEnterpriseDeviceId[] = "enterprise.device_id";

const char kOAuthTokenCookie[] = "oauth_token=1234";

const char kGaiaAccountDisabledResponse[] = "Error=AccountDeleted";

const char kOAuth2TokenPairData[] =
    "{"
    "  \"refresh_token\": \"1234\","
    "  \"access_token\": \"5678\","
    "  \"expires_in\": 3600"
    "}";

const char kOAuth2AccessTokenData[] =
    "{"
    "  \"access_token\": \"5678\","
    "  \"expires_in\": 3600"
    "}";

const char kDMServer[] = "http://server/device_management";
const char kDMRegisterRequest[] =
    "http://server/device_management?request=register";
const char kDMPolicyRequest[] =
    "http://server/device_management?request=policy";

const char kDMToken[] = "1234";

// Used to mark |flag|, indicating that RefreshPolicies() has executed its
// callback.
void SetFlag(bool* flag) {
  *flag = true;
}

// Single task of the fake IO loop used in the test, that just waits until
// it is signaled to quit or perform some work.
// |completion| is the event to wait for, and |work| is the task to invoke
// when signaled. If the task returns false then this quits the IO loop.
void BlockLoop(base::WaitableEvent* completion, base::Callback<bool()> work) {
  do {
    completion->Wait();
  } while (work.Run());
  base::MessageLoop::current()->QuitNow();
}

void CopyLockResult(base::RunLoop* loop,
                    policy::EnterpriseInstallAttributes::LockResult* out,
                    policy::EnterpriseInstallAttributes::LockResult result) {
  *out = result;
  loop->Quit();
}

class LoginUtilsTest : public testing::Test,
                       public LoginUtils::Delegate,
                       public LoginStatusConsumer {
 public:
  // Initialization here is important. The UI thread gets the test's
  // message loop, as does the file thread (which never actually gets
  // started - so this is a way to fake multiple threads on a single
  // test thread).  The IO thread does not get the message loop set,
  // and is never started.  This is necessary so that we skip various
  // bits of initialization that get posted to the IO thread.  We do
  // however, at one point in the test, temporarily set the message
  // loop for the IO thread.
  LoginUtilsTest()
      : fake_io_thread_completion_(false, false),
        fake_io_thread_("fake_io_thread"),
        loop_(base::MessageLoop::TYPE_IO),
        browser_process_(TestingBrowserProcess::GetGlobal()),
        local_state_(browser_process_),
        ui_thread_(BrowserThread::UI, &loop_),
        db_thread_(BrowserThread::DB, &loop_),
        file_thread_(BrowserThread::FILE, &loop_),
        mock_input_method_manager_(NULL),
        mock_async_method_caller_(NULL),
        connector_(NULL),
        prepared_profile_(NULL) {}

  virtual void SetUp() OVERRIDE {
    // This test is not a full blown InProcessBrowserTest, and doesn't have
    // all the usual threads running. However a lot of subsystems pulled from
    // ProfileImpl post to IO (usually from ProfileIOData), and DCHECK that
    // those tasks were posted. Those tasks in turn depend on a lot of other
    // components that aren't there during this test, so this kludge is used to
    // have a running IO loop that doesn't really execute any tasks.
    //
    // See InvokeOnIO() below for a way to perform specific tasks on IO, when
    // that's necessary.

    // A thread is needed to create a new MessageLoop, since there can be only
    // one loop per thread.
    fake_io_thread_.StartWithOptions(
        base::Thread::Options(base::MessageLoop::TYPE_IO, 0));
    base::MessageLoop* fake_io_loop = fake_io_thread_.message_loop();
    // Make this loop enter the single task, BlockLoop(). Pass in the completion
    // event and the work callback.
    fake_io_thread_.StopSoon();
    fake_io_loop->PostTask(
        FROM_HERE,
        base::Bind(
          BlockLoop,
          &fake_io_thread_completion_,
          base::Bind(&LoginUtilsTest::DoIOWork, base::Unretained(this))));
    // Map BrowserThread::IO to this loop. This allows posting to IO but nothing
    // will be executed.
    io_thread_.reset(
        new content::TestBrowserThread(BrowserThread::IO, fake_io_loop));

    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());

    CommandLine* command_line = CommandLine::ForCurrentProcess();
    command_line->AppendSwitchASCII(
        ::switches::kDeviceManagementUrl, kDMServer);
    command_line->AppendSwitchASCII(switches::kLoginProfile, "user");

    // DBusThreadManager should be initialized before io_thread_state_, as
    // DBusThreadManager is used from chromeos::ProxyConfigServiceImpl,
    // which is part of io_thread_state_.
    DBusThreadManager::InitializeForTesting(&mock_dbus_thread_manager_);

    CryptohomeLibrary::Initialize();
    LoginState::Initialize();

    mock_input_method_manager_ = new input_method::MockInputMethodManager();
    input_method::InitializeForTesting(mock_input_method_manager_);
    disks::DiskMountManager::InitializeForTesting(&mock_disk_mount_manager_);
    mock_disk_mount_manager_.SetupDefaultReplies();

    mock_async_method_caller_ = new cryptohome::MockAsyncMethodCaller;
    cryptohome::AsyncMethodCaller::InitializeForTesting(
        mock_async_method_caller_);

    cryptohome_.reset(new MockCryptohomeLibrary());
    EXPECT_CALL(*cryptohome_, InstallAttributesIsInvalid())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*cryptohome_, InstallAttributesIsFirstInstall())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, TpmIsEnabled())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*cryptohome_, InstallAttributesSet(kAttributeOwned, kTrue))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, InstallAttributesSet(kAttributeOwner,
                                                   kUsername))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, InstallAttributesSet(kAttrEnterpriseDomain,
                                                   kDomain))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, InstallAttributesSet(kAttrEnterpriseMode,
                                                   kMode))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, InstallAttributesSet(kAttrEnterpriseDeviceId,
                                                   kDeviceId))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, InstallAttributesFinalize())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, InstallAttributesGet(kAttributeOwned, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(kTrue),
                              Return(true)));
    EXPECT_CALL(*cryptohome_, InstallAttributesGet(kAttributeConsumerKiosk, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(kFalse),
                              Return(true)));
    EXPECT_CALL(*cryptohome_, InstallAttributesGet(kAttributeOwner, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(kUsername),
                              Return(true)));
    EXPECT_CALL(*cryptohome_, InstallAttributesGet(kAttrEnterpriseDomain, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(kDomain),
                              Return(true)));
    EXPECT_CALL(*cryptohome_, InstallAttributesGet(kAttrEnterpriseMode, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(kMode),
                              Return(true)));
    EXPECT_CALL(*cryptohome_, InstallAttributesGet(kAttrEnterpriseDeviceId, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(kDeviceId),
                              Return(true)));
    CryptohomeLibrary::SetForTest(cryptohome_.get());

    test_device_settings_service_.reset(new ScopedTestDeviceSettingsService);
    test_cros_settings_.reset(new ScopedTestCrosSettings);
    test_user_manager_.reset(new ScopedTestUserManager);

    browser_process_->SetProfileManager(
        new ProfileManagerWithoutInit(scoped_temp_dir_.path()));
    connector_ = browser_process_->browser_policy_connector();
    connector_->Init(local_state_.Get(),
                     browser_process_->system_request_context());

    // IOThread creates ProxyConfigServiceImpl which in turn needs
    // NetworkHandler. Thus initialize it here before creating IOThread.
    NetworkHandler::Initialize();

    io_thread_state_.reset(new IOThread(local_state_.Get(),
                                        browser_process_->policy_service(),
                                        NULL, NULL));
    browser_process_->SetIOThread(io_thread_state_.get());

#if defined(ENABLE_RLZ)
    rlz_initialized_cb_ = base::Bind(&base::DoNothing);
    rlz_lib::testing::SetRlzStoreDirectory(scoped_temp_dir_.path());
    RLZTracker::EnableZeroDelayForTesting();
#endif

    RunUntilIdle();
  }

  virtual void TearDown() OVERRIDE {
    cryptohome::AsyncMethodCaller::Shutdown();
    mock_async_method_caller_ = NULL;

    test_user_manager_.reset();

    InvokeOnIO(
        base::Bind(&LoginUtilsTest::TearDownOnIO, base::Unretained(this)));

    // LoginUtils instance must not outlive Profile instances.
    LoginUtils::Set(NULL);

    input_method::Shutdown();
    LoginState::Shutdown();
    CryptohomeLibrary::Shutdown();

    // These trigger some tasks that have to run while BrowserThread::UI
    // exists. Delete all the profiles before deleting the connector.
    browser_process_->SetProfileManager(NULL);
    connector_ = NULL;
    browser_process_->SetBrowserPolicyConnector(NULL);
    QuitIOLoop();
    RunUntilIdle();

    CryptohomeLibrary::SetForTest(NULL);
  }

  void TearDownOnIO() {
    // chrome_browser_net::Predictor usually skips its shutdown routines on
    // unit_tests, but does the full thing when
    // g_browser_process->profile_manager() is valid during initialization.
    // That includes a WaitableEvent on UI waiting for a task on IO, so that
    // task must execute. Do it directly from here now.
    std::vector<Profile*> profiles =
        browser_process_->profile_manager()->GetLoadedProfiles();
    for (size_t i = 0; i < profiles.size(); ++i) {
      chrome_browser_net::Predictor* predictor =
          profiles[i]->GetNetworkPredictor();
      if (predictor) {
        predictor->EnablePredictorOnIOThread(false);
        predictor->Shutdown();
      }
    }
  }

  void RunUntilIdle() {
    loop_.RunUntilIdle();
    BrowserThread::GetBlockingPool()->FlushForTesting();
    loop_.RunUntilIdle();
  }

  // Invokes |task| on the IO loop and returns after it has executed.
  void InvokeOnIO(const base::Closure& task) {
    fake_io_thread_work_ = task;
    fake_io_thread_completion_.Signal();
    content::RunMessageLoop();
  }

  // Makes the fake IO loop return.
  void QuitIOLoop() {
    fake_io_thread_completion_.Signal();
    content::RunMessageLoop();
  }

  // Helper for BlockLoop, InvokeOnIO and QuitIOLoop.
  bool DoIOWork() {
    bool has_work = !fake_io_thread_work_.is_null();
    if (has_work)
      fake_io_thread_work_.Run();
    fake_io_thread_work_.Reset();
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::MessageLoop::QuitWhenIdleClosure());
    // If there was work then keep waiting for more work.
    // If there was no work then quit the fake IO loop.
    return has_work;
  }

  virtual void OnProfilePrepared(Profile* profile) OVERRIDE {
    EXPECT_FALSE(prepared_profile_);
    prepared_profile_ = profile;
  }

#if defined(ENABLE_RLZ)
  virtual void OnRlzInitialized(Profile* profile) OVERRIDE {
    rlz_initialized_cb_.Run();
  }
#endif

  virtual void OnLoginFailure(const LoginFailure& error) OVERRIDE {
    FAIL() << "OnLoginFailure not expected";
  }

  virtual void OnLoginSuccess(const UserContext& user_context,
                              bool pending_requests,
                              bool using_oauth) OVERRIDE {
    FAIL() << "OnLoginSuccess not expected";
  }

  void EnrollDevice(const std::string& username) {
    EXPECT_CALL(*cryptohome_, InstallAttributesIsFirstInstall())
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));

    base::RunLoop loop;
    policy::EnterpriseInstallAttributes::LockResult result;
    connector_->GetInstallAttributes()->LockDevice(
        username, policy::DEVICE_MODE_ENTERPRISE, kDeviceId,
        base::Bind(&CopyLockResult, &loop, &result));
    loop.Run();
    EXPECT_EQ(policy::EnterpriseInstallAttributes::LOCK_SUCCESS, result);
    RunUntilIdle();
  }

  void PrepareProfile(const std::string& username) {
    // Normally this would happen during browser startup, but for tests
    // we need to trigger creation of Profile-related services.
    ChromeBrowserMainExtraPartsProfiles::
        EnsureBrowserContextKeyedServiceFactoriesBuilt();
    ProfileManager::AllowGetDefaultProfile();

    DeviceSettingsTestHelper device_settings_test_helper;
    DeviceSettingsService::Get()->SetSessionManager(
        &device_settings_test_helper, new MockOwnerKeyUtil());

    EXPECT_CALL(*cryptohome_, GetSystemSalt())
        .WillRepeatedly(Return(std::string("stub_system_salt")));
    EXPECT_CALL(*mock_async_method_caller_, AsyncMount(_, _, _, _))
        .WillRepeatedly(Return());
    EXPECT_CALL(*mock_async_method_caller_, AsyncGetSanitizedUsername(_, _))
        .WillRepeatedly(Return());

    scoped_refptr<Authenticator> authenticator =
        LoginUtils::Get()->CreateAuthenticator(this);
    authenticator->CompleteLogin(ProfileManager::GetDefaultProfile(),
                                 UserContext(username,
                                             "password",
                                             std::string(),
                                             username));   // username_hash

    const bool kUsingOAuth = true;
    // Setting |kHasCookies| to false prevents ProfileAuthData::Transfer from
    // waiting for an IO task before proceeding.
    const bool kHasCookies = false;
    const bool kHasActiveSession = false;
    LoginUtils::Get()->PrepareProfile(
        UserContext(username, "password", std::string(), username),
        std::string(), kUsingOAuth, kHasCookies, kHasActiveSession, this);
    device_settings_test_helper.Flush();
    RunUntilIdle();

    DeviceSettingsService::Get()->UnsetSessionManager();
  }

  net::TestURLFetcher* PrepareOAuthFetcher(const std::string& expected_url) {
    net::TestURLFetcher* fetcher = test_url_fetcher_factory_.GetFetcherByID(0);
    EXPECT_TRUE(fetcher);
    if (!fetcher)
      return NULL;
    EXPECT_TRUE(fetcher->delegate());
    EXPECT_TRUE(StartsWithASCII(fetcher->GetOriginalURL().spec(),
                                expected_url,
                                true));
    fetcher->set_url(fetcher->GetOriginalURL());
    fetcher->set_response_code(200);
    fetcher->set_status(net::URLRequestStatus());
    return fetcher;
  }

  net::TestURLFetcher* PrepareDMServiceFetcher(
      const std::string& expected_url,
      const em::DeviceManagementResponse& response) {
    net::TestURLFetcher* fetcher = test_url_fetcher_factory_.GetFetcherByID(
        policy::DeviceManagementService::kURLFetcherID);
    EXPECT_TRUE(fetcher);
    if (!fetcher)
      return NULL;
    EXPECT_TRUE(fetcher->delegate());
    EXPECT_TRUE(StartsWithASCII(fetcher->GetOriginalURL().spec(),
                                expected_url,
                                true));
    fetcher->set_url(fetcher->GetOriginalURL());
    fetcher->set_response_code(200);
    fetcher->set_status(net::URLRequestStatus());
    std::string data;
    EXPECT_TRUE(response.SerializeToString(&data));
    fetcher->SetResponseString(data);
    return fetcher;
  }

  net::TestURLFetcher* PrepareDMRegisterFetcher() {
    em::DeviceManagementResponse response;
    em::DeviceRegisterResponse* register_response =
        response.mutable_register_response();
    register_response->set_device_management_token(kDMToken);
    register_response->set_enrollment_type(
        em::DeviceRegisterResponse::ENTERPRISE);
    return PrepareDMServiceFetcher(kDMRegisterRequest, response);
  }

  net::TestURLFetcher* PrepareDMPolicyFetcher() {
    em::DeviceManagementResponse response;
    response.mutable_policy_response()->add_response();
    return PrepareDMServiceFetcher(kDMPolicyRequest, response);
  }

 protected:
  ScopedStubNetworkLibraryEnabler stub_network_library_enabler_;

  base::Closure fake_io_thread_work_;
  base::WaitableEvent fake_io_thread_completion_;
  base::Thread fake_io_thread_;

  base::MessageLoop loop_;
  TestingBrowserProcess* browser_process_;
  ScopedTestingLocalState local_state_;

  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread db_thread_;
  content::TestBrowserThread file_thread_;
  scoped_ptr<content::TestBrowserThread> io_thread_;
  scoped_ptr<IOThread> io_thread_state_;

  MockDBusThreadManagerWithoutGMock mock_dbus_thread_manager_;
  input_method::MockInputMethodManager* mock_input_method_manager_;
  disks::MockDiskMountManager mock_disk_mount_manager_;
  net::TestURLFetcherFactory test_url_fetcher_factory_;

  cryptohome::MockAsyncMethodCaller* mock_async_method_caller_;

  policy::BrowserPolicyConnector* connector_;
  scoped_ptr<MockCryptohomeLibrary> cryptohome_;

  // Initialized after |mock_dbus_thread_manager_| and |cryptohome_| are set up.
  scoped_ptr<ScopedTestDeviceSettingsService> test_device_settings_service_;
  scoped_ptr<ScopedTestCrosSettings> test_cros_settings_;
  scoped_ptr<ScopedTestUserManager> test_user_manager_;

  Profile* prepared_profile_;

  base::Closure rlz_initialized_cb_;

 private:
  base::ScopedTempDir scoped_temp_dir_;

  std::string device_policy_;
  std::string user_policy_;

  DISALLOW_COPY_AND_ASSIGN(LoginUtilsTest);
};

class LoginUtilsBlockingLoginTest
    : public LoginUtilsTest,
      public testing::WithParamInterface<int> {};

TEST_F(LoginUtilsTest, NormalLoginDoesntBlock) {
  UserManager* user_manager = UserManager::Get();
  EXPECT_FALSE(user_manager->IsUserLoggedIn());
  EXPECT_FALSE(connector_->IsEnterpriseManaged());
  EXPECT_FALSE(prepared_profile_);
  EXPECT_EQ(policy::USER_AFFILIATION_NONE,
            connector_->GetUserAffiliation(kUsername));

  // The profile will be created without waiting for a policy response.
  PrepareProfile(kUsername);

  EXPECT_TRUE(prepared_profile_);
  ASSERT_TRUE(user_manager->IsUserLoggedIn());
  EXPECT_EQ(kUsername, user_manager->GetLoggedInUser()->email());
}

TEST_F(LoginUtilsTest, EnterpriseLoginDoesntBlockForNormalUser) {
  UserManager* user_manager = UserManager::Get();
  EXPECT_FALSE(user_manager->IsUserLoggedIn());
  EXPECT_FALSE(connector_->IsEnterpriseManaged());
  EXPECT_FALSE(prepared_profile_);

  // Enroll the device.
  EnrollDevice(kUsername);

  EXPECT_FALSE(user_manager->IsUserLoggedIn());
  EXPECT_TRUE(connector_->IsEnterpriseManaged());
  EXPECT_EQ(kDomain, connector_->GetEnterpriseDomain());
  EXPECT_FALSE(prepared_profile_);
  EXPECT_EQ(policy::USER_AFFILIATION_NONE,
            connector_->GetUserAffiliation(kUsernameOtherDomain));

  // Login with a non-enterprise user shouldn't block.
  PrepareProfile(kUsernameOtherDomain);

  EXPECT_TRUE(prepared_profile_);
  ASSERT_TRUE(user_manager->IsUserLoggedIn());
  EXPECT_EQ(kUsernameOtherDomain, user_manager->GetLoggedInUser()->email());
}

#if defined(ENABLE_RLZ)
TEST_F(LoginUtilsTest, RlzInitialized) {
  // No RLZ brand code set initially.
  EXPECT_FALSE(local_state_.Get()->HasPrefPath(prefs::kRLZBrand));

  base::RunLoop wait_for_rlz_init;
  rlz_initialized_cb_ = wait_for_rlz_init.QuitClosure();

  PrepareProfile(kUsername);

  wait_for_rlz_init.Run();
  // Wait for blocking RLZ tasks to complete.
  RunUntilIdle();

  // RLZ brand code has been set to empty string.
  EXPECT_TRUE(local_state_.Get()->HasPrefPath(prefs::kRLZBrand));
  EXPECT_EQ(std::string(), local_state_.Get()->GetString(prefs::kRLZBrand));

  // RLZ value for homepage access point should have been initialized.
  string16 rlz_string;
  EXPECT_TRUE(RLZTracker::GetAccessPointRlz(
      RLZTracker::CHROME_HOME_PAGE, &rlz_string));
  EXPECT_EQ(string16(), rlz_string);
}
#endif

TEST_P(LoginUtilsBlockingLoginTest, EnterpriseLoginBlocksForEnterpriseUser) {
  UserManager* user_manager = UserManager::Get();
  EXPECT_FALSE(user_manager->IsUserLoggedIn());
  EXPECT_FALSE(connector_->IsEnterpriseManaged());
  EXPECT_FALSE(prepared_profile_);

  // Enroll the device.
  EnrollDevice(kUsername);

  EXPECT_FALSE(user_manager->IsUserLoggedIn());
  EXPECT_TRUE(connector_->IsEnterpriseManaged());
  EXPECT_EQ(kDomain, connector_->GetEnterpriseDomain());
  EXPECT_FALSE(prepared_profile_);
  EXPECT_EQ(policy::USER_AFFILIATION_MANAGED,
            connector_->GetUserAffiliation(kUsername));
  EXPECT_FALSE(user_manager->IsKnownUser(kUsername));

  // Login with a user of the enterprise domain waits for policy.
  PrepareProfile(kUsername);

  EXPECT_FALSE(prepared_profile_);
  ASSERT_TRUE(user_manager->IsUserLoggedIn());
  EXPECT_TRUE(user_manager->IsCurrentUserNew());

  GaiaUrls* gaia_urls = GaiaUrls::GetInstance();
  net::TestURLFetcher* fetcher;

  // |steps| is the test parameter, and is the number of successful fetches.
  // The first incomplete fetch will fail. In any case, the profile creation
  // should resume.
  int steps = GetParam();

  // The next expected fetcher ID. This is used to make it fail.
  int next_expected_fetcher_id = 0;

  do {
    if (steps < 1) break;

    // Fake refresh token retrieval:
    fetcher = PrepareOAuthFetcher(gaia_urls->client_login_to_oauth2_url());
    ASSERT_TRUE(fetcher);
    net::ResponseCookies cookies;
    cookies.push_back(kOAuthTokenCookie);
    fetcher->set_cookies(cookies);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    if (steps < 2) break;

    // Fake OAuth2 token pair retrieval:
    fetcher = PrepareOAuthFetcher(gaia_urls->oauth2_token_url());
    ASSERT_TRUE(fetcher);
    fetcher->SetResponseString(kOAuth2TokenPairData);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    if (steps < 3) break;

    // Fake OAuth2 access token retrieval:
    fetcher = PrepareOAuthFetcher(gaia_urls->oauth2_token_url());
    ASSERT_TRUE(fetcher);
    fetcher->SetResponseString(kOAuth2AccessTokenData);
    fetcher->delegate()->OnURLFetchComplete(fetcher);

    // The cloud policy subsystem is now ready to fetch the dmtoken and the user
    // policy.
    next_expected_fetcher_id = policy::DeviceManagementService::kURLFetcherID;
    RunUntilIdle();
    if (steps < 4) break;

    fetcher = PrepareDMRegisterFetcher();
    ASSERT_TRUE(fetcher);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    // The policy fetch job has now been scheduled, run it:
    RunUntilIdle();
    if (steps < 5) break;

    // Verify that there is no profile prepared just before the policy fetch.
    EXPECT_FALSE(prepared_profile_);

    fetcher = PrepareDMPolicyFetcher();
    ASSERT_TRUE(fetcher);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    RunUntilIdle();
  } while (0);

  if (steps < 5) {
    // Verify that the profile hasn't been created yet.
    EXPECT_FALSE(prepared_profile_);

    // Make the current fetcher fail with a Gaia error.
    net::TestURLFetcher* fetcher = test_url_fetcher_factory_.GetFetcherByID(
        next_expected_fetcher_id);
    ASSERT_TRUE(fetcher);
    EXPECT_TRUE(fetcher->delegate());
    fetcher->set_url(fetcher->GetOriginalURL());
    fetcher->set_response_code(401);
    // This response body is important to make the gaia fetcher skip its delayed
    // retry behavior, which makes testing harder. If this is sent to the policy
    // fetchers then it will make them fail too.
    fetcher->SetResponseString(kGaiaAccountDisabledResponse);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    RunUntilIdle();
  }

  // The profile is finally ready:
  EXPECT_TRUE(prepared_profile_);
}

INSTANTIATE_TEST_CASE_P(
    LoginUtilsBlockingLoginTestInstance,
    LoginUtilsBlockingLoginTest,
    testing::Values(0, 1, 2, 3, 4, 5));

}  // namespace

}  // namespace chromeos
