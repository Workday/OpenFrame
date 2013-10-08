// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/identity/identity_api.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_function_test_utils.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/api/identity/oauth2_manifest_handler.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/test_switches.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/test/test_utils.h"
#include "extensions/common/id_util.h"
#include "google_apis/gaia/google_service_auth_error.h"
#include "google_apis/gaia/oauth2_mint_token_flow.h"
#include "grit/browser_resources.h"
#include "net/test/spawned_test_server/spawned_test_server.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace extensions {

namespace {

namespace errors = identity_constants;
namespace utils = extension_function_test_utils;

static const char kAccessToken[] = "auth_token";
static const char kExtensionId[] = "ext_id";

// This helps us be able to wait until an AsyncExtensionFunction calls
// SendResponse.
class SendResponseDelegate
    : public UIThreadExtensionFunction::DelegateForTests {
 public:
  SendResponseDelegate() : should_post_quit_(false) {}

  virtual ~SendResponseDelegate() {}

  void set_should_post_quit(bool should_quit) {
    should_post_quit_ = should_quit;
  }

  bool HasResponse() {
    return response_.get() != NULL;
  }

  bool GetResponse() {
    EXPECT_TRUE(HasResponse());
    return *response_.get();
  }

  virtual void OnSendResponse(UIThreadExtensionFunction* function,
                              bool success,
                              bool bad_message) OVERRIDE {
    ASSERT_FALSE(bad_message);
    ASSERT_FALSE(HasResponse());
    response_.reset(new bool);
    *response_ = success;
    if (should_post_quit_) {
      base::MessageLoopForUI::current()->Quit();
    }
  }

 private:
  scoped_ptr<bool> response_;
  bool should_post_quit_;
};

class AsyncExtensionBrowserTest : public ExtensionBrowserTest {
 protected:
  // Asynchronous function runner allows tests to manipulate the browser window
  // after the call happens.
  void RunFunctionAsync(
      UIThreadExtensionFunction* function,
      const std::string& args) {
    response_delegate_.reset(new SendResponseDelegate);
    function->set_test_delegate(response_delegate_.get());
    scoped_ptr<base::ListValue> parsed_args(utils::ParseList(args));
    EXPECT_TRUE(parsed_args.get()) <<
        "Could not parse extension function arguments: " << args;
    function->SetArgs(parsed_args.get());

    if (!function->GetExtension()) {
      scoped_refptr<Extension> empty_extension(
          utils::CreateEmptyExtension());
      function->set_extension(empty_extension.get());
    }

    function->set_profile(browser()->profile());
    function->set_has_callback(true);
    function->Run();
  }

  std::string WaitForError(UIThreadExtensionFunction* function) {
    RunMessageLoopUntilResponse();
    EXPECT_FALSE(function->GetResultList()) << "Did not expect a result";
    return function->GetError();
  }

  base::Value* WaitForSingleResult(UIThreadExtensionFunction* function) {
    RunMessageLoopUntilResponse();
    EXPECT_TRUE(function->GetError().empty()) << "Unexpected error: "
                                              << function->GetError();
    const base::Value* single_result = NULL;
    if (function->GetResultList() != NULL &&
        function->GetResultList()->Get(0, &single_result)) {
      return single_result->DeepCopy();
    }
    return NULL;
  }

 private:
  void RunMessageLoopUntilResponse() {
    // If the RunImpl of |function| didn't already call SendResponse, run the
    // message loop until they do.
    if (!response_delegate_->HasResponse()) {
      response_delegate_->set_should_post_quit(true);
      content::RunMessageLoop();
    }
    EXPECT_TRUE(response_delegate_->HasResponse());
  }

  scoped_ptr<SendResponseDelegate> response_delegate_;
};

class TestOAuth2MintTokenFlow : public OAuth2MintTokenFlow {
 public:
  enum ResultType {
    ISSUE_ADVICE_SUCCESS,
    MINT_TOKEN_SUCCESS,
    MINT_TOKEN_FAILURE,
    MINT_TOKEN_BAD_CREDENTIALS
  };

  TestOAuth2MintTokenFlow(ResultType result,
                          OAuth2MintTokenFlow::Delegate* delegate)
    : OAuth2MintTokenFlow(NULL, delegate, OAuth2MintTokenFlow::Parameters()),
      result_(result),
      delegate_(delegate) {
  }

  virtual void Start() OVERRIDE {
    switch (result_) {
      case ISSUE_ADVICE_SUCCESS: {
        IssueAdviceInfo info;
        delegate_->OnIssueAdviceSuccess(info);
        break;
      }
      case MINT_TOKEN_SUCCESS: {
        delegate_->OnMintTokenSuccess(kAccessToken, 3600);
        break;
      }
      case MINT_TOKEN_FAILURE: {
        GoogleServiceAuthError error(GoogleServiceAuthError::CONNECTION_FAILED);
        delegate_->OnMintTokenFailure(error);
        break;
      }
      case MINT_TOKEN_BAD_CREDENTIALS: {
        GoogleServiceAuthError error(
            GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS);
        delegate_->OnMintTokenFailure(error);
        break;
      }
    }
  }

 private:
  ResultType result_;
  OAuth2MintTokenFlow::Delegate* delegate_;
};

BrowserContextKeyedService* IdentityAPITestFactory(
    content::BrowserContext* profile) {
  return new IdentityAPI(static_cast<Profile*>(profile));
}

// Waits for a specific GURL to generate a NOTIFICATION_LOAD_STOP event and
// saves a pointer to the window embedding the WebContents, which can be later
// closed.
class WaitForGURLAndCloseWindow : public content::WindowedNotificationObserver {
 public:
  explicit WaitForGURLAndCloseWindow(GURL url)
      : WindowedNotificationObserver(
            content::NOTIFICATION_LOAD_STOP,
            content::NotificationService::AllSources()),
        url_(url) {}

  // NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    content::NavigationController* web_auth_flow_controller =
        content::Source<content::NavigationController>(source).ptr();
    content::WebContents* web_contents =
        web_auth_flow_controller->GetWebContents();

    if (web_contents->GetURL() == url_) {
      // It is safe to keep the pointer here, because we know in a test, that
      // the WebContents won't go away before CloseEmbedderWebContents is
      // called. Don't copy this code to production.
      embedder_web_contents_ = web_contents->GetEmbedderWebContents();
      // Condtionally invoke parent class so that Wait will not exit
      // until the target URL arrives.
      content::WindowedNotificationObserver::Observe(type, source, details);
    }
  }

  // Closes the window embedding the WebContents. The action is separated from
  // the Observe method to make sure the list of observers is not deleted,
  // while some event is already being processed. (That causes ASAN failures.)
  void CloseEmbedderWebContents() {
    if (embedder_web_contents_)
      embedder_web_contents_->Close();
  }

 private:
  GURL url_;
  content::WebContents* embedder_web_contents_;
};

}  // namespace

class MockGetAuthTokenFunction : public IdentityGetAuthTokenFunction {
 public:
  MockGetAuthTokenFunction() : login_access_token_result_(true),
                               login_ui_result_(true),
                               scope_ui_result_(true),
                               login_ui_shown_(false),
                               scope_ui_shown_(false) {
  }

  void set_login_access_token_result(bool result) {
    login_access_token_result_ = result;
  }

  void set_login_ui_result(bool result) {
    login_ui_result_ = result;
  }

  void set_scope_ui_failure(GaiaWebAuthFlow::Failure failure) {
    scope_ui_result_ = false;
    scope_ui_failure_ = failure;
  }

  void set_scope_ui_oauth_error(const std::string& oauth_error) {
    scope_ui_result_ = false;
    scope_ui_failure_ = GaiaWebAuthFlow::OAUTH_ERROR;
    scope_ui_oauth_error_ = oauth_error;
  }

  bool login_ui_shown() const {
    return login_ui_shown_;
  }

  bool scope_ui_shown() const {
    return scope_ui_shown_;
  }

  virtual void StartLoginAccessTokenRequest() OVERRIDE {
    if (login_access_token_result_) {
      OnGetTokenSuccess(login_token_request_.get(), "access_token",
          base::Time::Now() + base::TimeDelta::FromHours(1LL));
    } else {
      GoogleServiceAuthError error(
          GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS);
      OnGetTokenFailure(login_token_request_.get(), error);
    }
  }

  virtual void ShowLoginPopup() OVERRIDE {
    EXPECT_FALSE(login_ui_shown_);
    login_ui_shown_ = true;
    if (login_ui_result_)
      SigninSuccess();
    else
      SigninFailed();
  }

  virtual void ShowOAuthApprovalDialog(
      const IssueAdviceInfo& issue_advice) OVERRIDE {
    scope_ui_shown_ = true;

    if (scope_ui_result_) {
      OnGaiaFlowCompleted(kAccessToken, "3600");
    } else if (scope_ui_failure_ == GaiaWebAuthFlow::SERVICE_AUTH_ERROR) {
      GoogleServiceAuthError error(GoogleServiceAuthError::CONNECTION_FAILED);
      OnGaiaFlowFailure(scope_ui_failure_, error, "");
    } else {
      GoogleServiceAuthError error(GoogleServiceAuthError::NONE);
      OnGaiaFlowFailure(scope_ui_failure_, error, scope_ui_oauth_error_);
    }
  }

  MOCK_CONST_METHOD0(HasLoginToken, bool());
  MOCK_METHOD1(CreateMintTokenFlow,
               OAuth2MintTokenFlow* (const std::string& login_access_token));

 private:
  ~MockGetAuthTokenFunction() {}
  bool login_access_token_result_;
  bool login_ui_result_;
  bool scope_ui_result_;
  GaiaWebAuthFlow::Failure scope_ui_failure_;
  std::string scope_ui_oauth_error_;
  bool login_ui_shown_;
  bool scope_ui_shown_;
};

class MockQueuedMintRequest : public IdentityMintRequestQueue::Request {
 public:
  MOCK_METHOD1(StartMintToken, void(IdentityMintRequestQueue::MintType));
};

class GetAuthTokenFunctionTest : public AsyncExtensionBrowserTest {
 protected:
  enum OAuth2Fields {
    NONE = 0,
    CLIENT_ID = 1,
    SCOPES = 2,
    AS_COMPONENT = 4
  };

  virtual ~GetAuthTokenFunctionTest() {}

  // Helper to create an extension with specific OAuth2Info fields set.
  // |fields_to_set| should be computed by using fields of Oauth2Fields enum.
  const Extension* CreateExtension(int fields_to_set) {
    const Extension* ext;
    base::FilePath manifest_path =
        test_data_dir_.AppendASCII("platform_apps/oauth2");
    base::FilePath component_manifest_path =
        test_data_dir_.AppendASCII("packaged_app/component_oauth2");
    if ((fields_to_set & AS_COMPONENT) == 0)
      ext = LoadExtension(manifest_path);
    else
      ext = LoadExtensionAsComponent(component_manifest_path);
    OAuth2Info& oauth2_info =
        const_cast<OAuth2Info&>(OAuth2Info::GetOAuth2Info(ext));
    if ((fields_to_set & CLIENT_ID) != 0)
      oauth2_info.client_id = "client1";
    if ((fields_to_set & SCOPES) != 0) {
      oauth2_info.scopes.push_back("scope1");
      oauth2_info.scopes.push_back("scope2");
    }
    return ext;
  }

  IdentityAPI* id_api() {
    return IdentityAPI::GetFactoryInstance()->GetForProfile(
        browser()->profile());
  }
};

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       NoClientId) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(SCOPES));
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{}]", browser());
  EXPECT_EQ(std::string(errors::kInvalidClientId), error);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       NoScopes) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID));
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{}]", browser());
  EXPECT_EQ(std::string(errors::kInvalidScopes), error);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       NonInteractiveNotSignedIn) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken()).WillOnce(Return(false));
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{}]", browser());
  EXPECT_EQ(std::string(errors::kUserNotSignedIn), error);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       NonInteractiveMintFailure) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken())
      .WillOnce(Return(true));
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::MINT_TOKEN_FAILURE, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow));
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{}]", browser());
  EXPECT_TRUE(StartsWithASCII(error, errors::kAuthFailure, false));
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       NonInteractiveLoginAccessTokenFailure) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken())
      .WillOnce(Return(true));
  func->set_login_access_token_result(false);
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{}]", browser());
  EXPECT_TRUE(StartsWithASCII(error, errors::kAuthFailure, false));
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       NonInteractiveMintAdviceSuccess) {
  scoped_refptr<const Extension> extension(CreateExtension(CLIENT_ID | SCOPES));
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(extension.get());
  EXPECT_CALL(*func.get(), HasLoginToken()).WillOnce(Return(true));
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::ISSUE_ADVICE_SUCCESS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow));
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{}]", browser());
  EXPECT_EQ(std::string(errors::kNoGrant), error);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());

  const OAuth2Info& oauth2_info = OAuth2Info::GetOAuth2Info(extension.get());
  EXPECT_EQ(
      IdentityTokenCacheValue::CACHE_STATUS_ADVICE,
      id_api()->GetCachedToken(extension->id(), oauth2_info.scopes).status());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       NonInteractiveMintBadCredentials) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken())
      .WillOnce(Return(true));
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::MINT_TOKEN_BAD_CREDENTIALS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow));
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{}]", browser());
  EXPECT_TRUE(StartsWithASCII(error, errors::kAuthFailure, false));
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       NonInteractiveSuccess) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  scoped_refptr<const Extension> extension(CreateExtension(CLIENT_ID | SCOPES));
  func->set_extension(extension.get());
  const OAuth2Info& oauth2_info = OAuth2Info::GetOAuth2Info(extension.get());
  EXPECT_CALL(*func.get(), HasLoginToken()).WillOnce(Return(true));
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::MINT_TOKEN_SUCCESS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow));
  scoped_ptr<base::Value> value(utils::RunFunctionAndReturnSingleResult(
      func.get(), "[{}]", browser()));
  std::string access_token;
  EXPECT_TRUE(value->GetAsString(&access_token));
  EXPECT_EQ(std::string(kAccessToken), access_token);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
  EXPECT_EQ(IdentityTokenCacheValue::CACHE_STATUS_TOKEN,
            id_api()->GetCachedToken(extension->id(),
                                     oauth2_info.scopes).status());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveLoginCanceled) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken()).WillOnce(Return(false));
  func->set_login_ui_result(false);
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{\"interactive\": true}]", browser());
  EXPECT_EQ(std::string(errors::kUserNotSignedIn), error);
  EXPECT_TRUE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveMintBadCredentialsLoginCanceled) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken())
      .WillOnce(Return(true));
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::MINT_TOKEN_BAD_CREDENTIALS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow));
  func->set_login_ui_result(false);
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{\"interactive\": true}]", browser());
  EXPECT_EQ(std::string(errors::kUserNotSignedIn), error);
  EXPECT_TRUE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveLoginSuccessNoToken) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken()).WillOnce(Return(false));
  func->set_login_ui_result(false);
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{\"interactive\": true}]", browser());
  EXPECT_EQ(std::string(errors::kUserNotSignedIn), error);
  EXPECT_TRUE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveLoginSuccessMintFailure) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken())
      .WillOnce(Return(false));
  func->set_login_ui_result(true);
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::MINT_TOKEN_FAILURE, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow));
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{\"interactive\": true}]", browser());
  EXPECT_TRUE(StartsWithASCII(error, errors::kAuthFailure, false));
  EXPECT_TRUE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveLoginSuccessLoginAccessTokenFailure) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken()).WillOnce(Return(false));
  func->set_login_ui_result(true);
  func->set_login_access_token_result(false);
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{\"interactive\": true}]", browser());
  EXPECT_TRUE(StartsWithASCII(error, errors::kAuthFailure, false));
  EXPECT_TRUE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveLoginSuccessMintSuccess) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken())
      .WillOnce(Return(false));
  func->set_login_ui_result(true);
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::MINT_TOKEN_SUCCESS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow));
  scoped_ptr<base::Value> value(utils::RunFunctionAndReturnSingleResult(
      func.get(), "[{\"interactive\": true}]", browser()));
  std::string access_token;
  EXPECT_TRUE(value->GetAsString(&access_token));
  EXPECT_EQ(std::string(kAccessToken), access_token);
  EXPECT_TRUE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveLoginSuccessApprovalAborted) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken())
      .WillOnce(Return(false));
  func->set_login_ui_result(true);
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::ISSUE_ADVICE_SUCCESS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow));
  func->set_scope_ui_failure(GaiaWebAuthFlow::WINDOW_CLOSED);
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{\"interactive\": true}]", browser());
  EXPECT_EQ(std::string(errors::kUserRejected), error);
  EXPECT_TRUE(func->login_ui_shown());
  EXPECT_TRUE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveLoginSuccessApprovalSuccess) {
  scoped_refptr<const Extension> extension(CreateExtension(CLIENT_ID | SCOPES));
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(extension.get());
  EXPECT_CALL(*func.get(), HasLoginToken()).WillOnce(Return(false));
  func->set_login_ui_result(true);
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::ISSUE_ADVICE_SUCCESS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_))
      .WillOnce(Return(flow));

  scoped_ptr<base::Value> value(utils::RunFunctionAndReturnSingleResult(
      func.get(), "[{\"interactive\": true}]", browser()));
  std::string access_token;
  EXPECT_TRUE(value->GetAsString(&access_token));
  EXPECT_EQ(std::string(kAccessToken), access_token);
  EXPECT_TRUE(func->login_ui_shown());
  EXPECT_TRUE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveApprovalAborted) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken())
      .WillOnce(Return(true));
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::ISSUE_ADVICE_SUCCESS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow));
  func->set_scope_ui_failure(GaiaWebAuthFlow::WINDOW_CLOSED);
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{\"interactive\": true}]", browser());
  EXPECT_EQ(std::string(errors::kUserRejected), error);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_TRUE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveApprovalLoadFailed) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken())
      .WillOnce(Return(true));
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::ISSUE_ADVICE_SUCCESS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow));
  func->set_scope_ui_failure(GaiaWebAuthFlow::LOAD_FAILED);
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{\"interactive\": true}]", browser());
  EXPECT_EQ(std::string(errors::kPageLoadFailure), error);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_TRUE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveApprovalInvalidRedirect) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken())
      .WillOnce(Return(true));
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::ISSUE_ADVICE_SUCCESS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow));
  func->set_scope_ui_failure(GaiaWebAuthFlow::INVALID_REDIRECT);
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{\"interactive\": true}]", browser());
  EXPECT_EQ(std::string(errors::kInvalidRedirect), error);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_TRUE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveApprovalConnectionFailure) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(CreateExtension(CLIENT_ID | SCOPES));
  EXPECT_CALL(*func.get(), HasLoginToken())
      .WillOnce(Return(true));
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::ISSUE_ADVICE_SUCCESS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow));
  func->set_scope_ui_failure(GaiaWebAuthFlow::SERVICE_AUTH_ERROR);
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{\"interactive\": true}]", browser());
  EXPECT_TRUE(StartsWithASCII(error, errors::kAuthFailure, false));
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_TRUE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveApprovalOAuthErrors) {
  scoped_refptr<const Extension> extension(CreateExtension(CLIENT_ID | SCOPES));

  std::map<std::string, std::string> error_map;
  error_map.insert(std::make_pair("access_denied", errors::kUserRejected));
  error_map.insert(std::make_pair("invalid_scope", errors::kInvalidScopes));
  error_map.insert(std::make_pair(
      "unmapped_error", std::string(errors::kAuthFailure) + "unmapped_error"));

  for (std::map<std::string, std::string>::const_iterator
           it = error_map.begin();
       it != error_map.end();
       ++it) {
    scoped_refptr<MockGetAuthTokenFunction> func(
        new MockGetAuthTokenFunction());
    func->set_extension(extension.get());
    EXPECT_CALL(*func.get(), HasLoginToken()).WillOnce(Return(true));
    TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
        TestOAuth2MintTokenFlow::ISSUE_ADVICE_SUCCESS, func.get());
    ON_CALL(*func.get(), CreateMintTokenFlow(_)).WillByDefault(Return(flow));
    func->set_scope_ui_oauth_error(it->first);
    std::string error = utils::RunFunctionAndReturnError(
        func.get(), "[{\"interactive\": true}]", browser());
    EXPECT_EQ(it->second, error);
    EXPECT_FALSE(func->login_ui_shown());
    EXPECT_TRUE(func->scope_ui_shown());
  }
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveApprovalSuccess) {
  scoped_refptr<const Extension> extension(CreateExtension(CLIENT_ID | SCOPES));
  const OAuth2Info& oauth2_info = OAuth2Info::GetOAuth2Info(extension.get());
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(extension.get());
  EXPECT_CALL(*func.get(), HasLoginToken()).WillOnce(Return(true));
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::ISSUE_ADVICE_SUCCESS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_))
      .WillOnce(Return(flow));

  scoped_ptr<base::Value> value(utils::RunFunctionAndReturnSingleResult(
      func.get(), "[{\"interactive\": true}]", browser()));
  std::string access_token;
  EXPECT_TRUE(value->GetAsString(&access_token));
  EXPECT_EQ(std::string(kAccessToken), access_token);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_TRUE(func->scope_ui_shown());

  EXPECT_EQ(IdentityTokenCacheValue::CACHE_STATUS_TOKEN,
            id_api()->GetCachedToken(extension->id(),
                                     oauth2_info.scopes).status());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest, NoninteractiveQueue) {
  scoped_refptr<const Extension> extension(CreateExtension(CLIENT_ID | SCOPES));
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(extension.get());

  // Create a fake request to block the queue.
  const OAuth2Info& oauth2_info = OAuth2Info::GetOAuth2Info(extension.get());
  std::set<std::string> scopes(oauth2_info.scopes.begin(),
                               oauth2_info.scopes.end());
  IdentityAPI* id_api =
      extensions::IdentityAPI::GetFactoryInstance()->GetForProfile(
          browser()->profile());
  IdentityMintRequestQueue* queue = id_api->mint_queue();
  MockQueuedMintRequest queued_request;
  IdentityMintRequestQueue::MintType type =
      IdentityMintRequestQueue::MINT_TYPE_NONINTERACTIVE;

  EXPECT_CALL(queued_request, StartMintToken(type)).Times(1);
  queue->RequestStart(type, extension->id(), scopes, &queued_request);

  // The real request will start processing, but wait in the queue behind
  // the blocker.
  EXPECT_CALL(*func.get(), HasLoginToken()).WillOnce(Return(true));
  RunFunctionAsync(func.get(), "[{}]");
  // Verify that we have fetched the login token at this point.
  testing::Mock::VerifyAndClearExpectations(func.get());

  // The flow will be created after the first queued request clears.
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::MINT_TOKEN_SUCCESS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow));

  queue->RequestComplete(type, extension->id(), scopes, &queued_request);

  scoped_ptr<base::Value> value(WaitForSingleResult(func.get()));
  std::string access_token;
  EXPECT_TRUE(value->GetAsString(&access_token));
  EXPECT_EQ(std::string(kAccessToken), access_token);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest, InteractiveQueue) {
  scoped_refptr<const Extension> extension(CreateExtension(CLIENT_ID | SCOPES));
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(extension.get());

  // Create a fake request to block the queue.
  const OAuth2Info& oauth2_info = OAuth2Info::GetOAuth2Info(extension.get());
  std::set<std::string> scopes(oauth2_info.scopes.begin(),
                               oauth2_info.scopes.end());
  IdentityAPI* id_api =
      extensions::IdentityAPI::GetFactoryInstance()->GetForProfile(
          browser()->profile());
  IdentityMintRequestQueue* queue = id_api->mint_queue();
  MockQueuedMintRequest queued_request;
  IdentityMintRequestQueue::MintType type =
      IdentityMintRequestQueue::MINT_TYPE_INTERACTIVE;

  EXPECT_CALL(queued_request, StartMintToken(type)).Times(1);
  queue->RequestStart(type, extension->id(), scopes, &queued_request);

  // The real request will start processing, but wait in the queue behind
  // the blocker.
  EXPECT_CALL(*func.get(), HasLoginToken()).WillOnce(Return(true));
  TestOAuth2MintTokenFlow* flow1 = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::ISSUE_ADVICE_SUCCESS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow1));
  RunFunctionAsync(func.get(), "[{\"interactive\": true}]");
  // Verify that we have fetched the login token and run the first flow.
  testing::Mock::VerifyAndClearExpectations(func.get());
  EXPECT_FALSE(func->scope_ui_shown());

  // The UI will be displayed and a token retrieved after the first
  // queued request clears.
  queue->RequestComplete(type, extension->id(), scopes, &queued_request);

  scoped_ptr<base::Value> value(WaitForSingleResult(func.get()));
  std::string access_token;
  EXPECT_TRUE(value->GetAsString(&access_token));
  EXPECT_EQ(std::string(kAccessToken), access_token);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_TRUE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveQueuedNoninteractiveFails) {
  scoped_refptr<const Extension> extension(CreateExtension(CLIENT_ID | SCOPES));
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(extension.get());

  // Create a fake request to block the interactive queue.
  const OAuth2Info& oauth2_info = OAuth2Info::GetOAuth2Info(extension.get());
  std::set<std::string> scopes(oauth2_info.scopes.begin(),
                               oauth2_info.scopes.end());
  IdentityAPI* id_api =
      extensions::IdentityAPI::GetFactoryInstance()->GetForProfile(
          browser()->profile());
  IdentityMintRequestQueue* queue = id_api->mint_queue();
  MockQueuedMintRequest queued_request;
  IdentityMintRequestQueue::MintType type =
      IdentityMintRequestQueue::MINT_TYPE_INTERACTIVE;

  EXPECT_CALL(queued_request, StartMintToken(type)).Times(1);
  queue->RequestStart(type, extension->id(), scopes, &queued_request);

  // Non-interactive requests fail without hitting GAIA, because a
  // consent UI is known to be up.
  EXPECT_CALL(*func.get(), HasLoginToken()).WillOnce(Return(true));
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{}]", browser());
  EXPECT_EQ(std::string(errors::kNoGrant), error);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());

  queue->RequestComplete(type, extension->id(), scopes, &queued_request);
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       NonInteractiveCacheHit) {
  scoped_refptr<const Extension> extension(CreateExtension(CLIENT_ID | SCOPES));
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(extension.get());

  // pre-populate the cache with a token
  const OAuth2Info& oauth2_info = OAuth2Info::GetOAuth2Info(extension.get());
  IdentityTokenCacheValue token(kAccessToken,
                                base::TimeDelta::FromSeconds(3600));
  id_api()->SetCachedToken(extension->id(), oauth2_info.scopes, token);

  // Get a token. Should not require a GAIA request.
  EXPECT_CALL(*func.get(), HasLoginToken())
      .WillOnce(Return(true));
  scoped_ptr<base::Value> value(utils::RunFunctionAndReturnSingleResult(
      func.get(), "[{}]", browser()));
  std::string access_token;
  EXPECT_TRUE(value->GetAsString(&access_token));
  EXPECT_EQ(std::string(kAccessToken), access_token);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       NonInteractiveIssueAdviceCacheHit) {
  scoped_refptr<const Extension> extension(CreateExtension(CLIENT_ID | SCOPES));
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(extension.get());

  // pre-populate the cache with advice
  const OAuth2Info& oauth2_info = OAuth2Info::GetOAuth2Info(extension.get());
  IssueAdviceInfo info;
  IdentityTokenCacheValue token(info);
  id_api()->SetCachedToken(extension->id(), oauth2_info.scopes, token);

  // Should return an error without a GAIA request.
  EXPECT_CALL(*func.get(), HasLoginToken())
      .WillOnce(Return(true));
  std::string error = utils::RunFunctionAndReturnError(
      func.get(), "[{}]", browser());
  EXPECT_EQ(std::string(errors::kNoGrant), error);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       InteractiveCacheHit) {
  scoped_refptr<const Extension> extension(CreateExtension(CLIENT_ID | SCOPES));
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  func->set_extension(extension.get());

  // Create a fake request to block the queue.
  const OAuth2Info& oauth2_info = OAuth2Info::GetOAuth2Info(extension.get());
  std::set<std::string> scopes(oauth2_info.scopes.begin(),
                               oauth2_info.scopes.end());
  IdentityMintRequestQueue* queue = id_api()->mint_queue();
  MockQueuedMintRequest queued_request;
  IdentityMintRequestQueue::MintType type =
      IdentityMintRequestQueue::MINT_TYPE_INTERACTIVE;

  EXPECT_CALL(queued_request, StartMintToken(type)).Times(1);
  queue->RequestStart(type, extension->id(), scopes, &queued_request);

  // The real request will start processing, but wait in the queue behind
  // the blocker.
  EXPECT_CALL(*func.get(), HasLoginToken()).WillOnce(Return(true));
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::ISSUE_ADVICE_SUCCESS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_)).WillOnce(Return(flow));
  RunFunctionAsync(func.get(), "[{\"interactive\": true}]");

  // Populate the cache with a token while the request is blocked.
  IdentityTokenCacheValue token(kAccessToken,
                                base::TimeDelta::FromSeconds(3600));
  id_api()->SetCachedToken(extension->id(), oauth2_info.scopes, token);

  // When we wake up the request, it returns the cached token without
  // displaying a UI, or hitting GAIA.

  queue->RequestComplete(type, extension->id(), scopes, &queued_request);

  scoped_ptr<base::Value> value(WaitForSingleResult(func.get()));
  std::string access_token;
  EXPECT_TRUE(value->GetAsString(&access_token));
  EXPECT_EQ(std::string(kAccessToken), access_token);
  EXPECT_FALSE(func->login_ui_shown());
  EXPECT_FALSE(func->scope_ui_shown());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest,
                       LoginInvalidatesTokenCache) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  scoped_refptr<const Extension> extension(CreateExtension(CLIENT_ID | SCOPES));
  func->set_extension(extension.get());

  // pre-populate the cache with a token
  const OAuth2Info& oauth2_info = OAuth2Info::GetOAuth2Info(extension.get());
  IdentityTokenCacheValue token(kAccessToken,
                                base::TimeDelta::FromSeconds(3600));
  id_api()->SetCachedToken(extension->id(), oauth2_info.scopes, token);

  // Because the user is not signed in, the token will be removed,
  // and we'll hit GAIA for new tokens.
  EXPECT_CALL(*func.get(), HasLoginToken())
      .WillOnce(Return(false));
  func->set_login_ui_result(true);
  TestOAuth2MintTokenFlow* flow = new TestOAuth2MintTokenFlow(
      TestOAuth2MintTokenFlow::ISSUE_ADVICE_SUCCESS, func.get());
  EXPECT_CALL(*func.get(), CreateMintTokenFlow(_))
      .WillOnce(Return(flow));

  scoped_ptr<base::Value> value(utils::RunFunctionAndReturnSingleResult(
      func.get(), "[{\"interactive\": true}]", browser()));
  std::string access_token;
  EXPECT_TRUE(value->GetAsString(&access_token));
  EXPECT_EQ(std::string(kAccessToken), access_token);
  EXPECT_TRUE(func->login_ui_shown());
  EXPECT_TRUE(func->scope_ui_shown());
  EXPECT_EQ(IdentityTokenCacheValue::CACHE_STATUS_TOKEN,
            id_api()->GetCachedToken(extension->id(),
                                     oauth2_info.scopes).status());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest, ComponentWithChromeClientId) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  scoped_refptr<const Extension> extension(
      CreateExtension(SCOPES | AS_COMPONENT));
  func->set_extension(extension.get());
  const OAuth2Info& oauth2_info = OAuth2Info::GetOAuth2Info(extension.get());
  EXPECT_TRUE(oauth2_info.client_id.empty());
  EXPECT_FALSE(func->GetOAuth2ClientId().empty());
  EXPECT_NE("client1", func->GetOAuth2ClientId());
}

IN_PROC_BROWSER_TEST_F(GetAuthTokenFunctionTest, ComponentWithNormalClientId) {
  scoped_refptr<MockGetAuthTokenFunction> func(new MockGetAuthTokenFunction());
  scoped_refptr<const Extension> extension(
      CreateExtension(CLIENT_ID | SCOPES | AS_COMPONENT));
  func->set_extension(extension.get());
  EXPECT_EQ("client1", func->GetOAuth2ClientId());
}

class RemoveCachedAuthTokenFunctionTest : public ExtensionBrowserTest {
 protected:
  bool InvalidateDefaultToken() {
    scoped_refptr<IdentityRemoveCachedAuthTokenFunction> func(
        new IdentityRemoveCachedAuthTokenFunction);
    func->set_extension(utils::CreateEmptyExtension(kExtensionId).get());
    return utils::RunFunction(
        func.get(),
        std::string("[{\"token\": \"") + kAccessToken + "\"}]",
        browser(),
        extension_function_test_utils::NONE);
  }

  IdentityAPI* id_api() {
    return IdentityAPI::GetFactoryInstance()->GetForProfile(
        browser()->profile());
  }

  void SetCachedToken(IdentityTokenCacheValue& token_data) {
    id_api()->SetCachedToken(extensions::id_util::GenerateId(kExtensionId),
                             std::vector<std::string>(), token_data);
  }

  const IdentityTokenCacheValue& GetCachedToken() {
    return id_api()->GetCachedToken(
        extensions::id_util::GenerateId(kExtensionId),
        std::vector<std::string>());
  }
};

IN_PROC_BROWSER_TEST_F(RemoveCachedAuthTokenFunctionTest, NotFound) {
  EXPECT_TRUE(InvalidateDefaultToken());
  EXPECT_EQ(IdentityTokenCacheValue::CACHE_STATUS_NOTFOUND,
            GetCachedToken().status());
}

IN_PROC_BROWSER_TEST_F(RemoveCachedAuthTokenFunctionTest, Advice) {
  IssueAdviceInfo info;
  IdentityTokenCacheValue advice(info);
  SetCachedToken(advice);
  EXPECT_TRUE(InvalidateDefaultToken());
  EXPECT_EQ(IdentityTokenCacheValue::CACHE_STATUS_ADVICE,
            GetCachedToken().status());
}

IN_PROC_BROWSER_TEST_F(RemoveCachedAuthTokenFunctionTest, NonMatchingToken) {
  IdentityTokenCacheValue token("non_matching_token",
                                base::TimeDelta::FromSeconds(3600));
  SetCachedToken(token);
  EXPECT_TRUE(InvalidateDefaultToken());
  EXPECT_EQ(IdentityTokenCacheValue::CACHE_STATUS_TOKEN,
            GetCachedToken().status());
  EXPECT_EQ("non_matching_token", GetCachedToken().token());
}

IN_PROC_BROWSER_TEST_F(RemoveCachedAuthTokenFunctionTest, MatchingToken) {
  IdentityTokenCacheValue token(kAccessToken,
                                base::TimeDelta::FromSeconds(3600));
  SetCachedToken(token);
  EXPECT_TRUE(InvalidateDefaultToken());
  EXPECT_EQ(IdentityTokenCacheValue::CACHE_STATUS_NOTFOUND,
            GetCachedToken().status());
}

class LaunchWebAuthFlowFunctionTest : public AsyncExtensionBrowserTest {
 public:
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    // Reduce performance test variance by disabling background networking.
    command_line->AppendSwitch(switches::kDisableBackgroundNetworking);
  }
};

IN_PROC_BROWSER_TEST_F(LaunchWebAuthFlowFunctionTest, UserCloseWindow) {
  net::SpawnedTestServer https_server(
      net::SpawnedTestServer::TYPE_HTTPS,
      net::SpawnedTestServer::kLocalhost,
      base::FilePath(FILE_PATH_LITERAL(
          "chrome/test/data/extensions/api_test/identity")));
  ASSERT_TRUE(https_server.Start());
  GURL auth_url(https_server.GetURL("files/interaction_required.html"));

  scoped_refptr<IdentityLaunchWebAuthFlowFunction> function(
      new IdentityLaunchWebAuthFlowFunction());
  scoped_refptr<Extension> empty_extension(
      utils::CreateEmptyExtension());
  function->set_extension(empty_extension.get());

  WaitForGURLAndCloseWindow popup_observer(auth_url);

  std::string args = "[{\"interactive\": true, \"url\": \"" +
      auth_url.spec() + "\"}]";
  RunFunctionAsync(function.get(), args);

  popup_observer.Wait();
  popup_observer.CloseEmbedderWebContents();

  EXPECT_EQ(std::string(errors::kUserRejected), WaitForError(function.get()));
}

IN_PROC_BROWSER_TEST_F(LaunchWebAuthFlowFunctionTest, InteractionRequired) {
  net::SpawnedTestServer https_server(
      net::SpawnedTestServer::TYPE_HTTPS,
      net::SpawnedTestServer::kLocalhost,
      base::FilePath(FILE_PATH_LITERAL(
          "chrome/test/data/extensions/api_test/identity")));
  ASSERT_TRUE(https_server.Start());
  GURL auth_url(https_server.GetURL("files/interaction_required.html"));

  scoped_refptr<IdentityLaunchWebAuthFlowFunction> function(
      new IdentityLaunchWebAuthFlowFunction());
  scoped_refptr<Extension> empty_extension(
      utils::CreateEmptyExtension());
  function->set_extension(empty_extension.get());

  std::string args = "[{\"interactive\": false, \"url\": \"" +
      auth_url.spec() + "\"}]";
  std::string error =
      utils::RunFunctionAndReturnError(function.get(), args, browser());

  EXPECT_EQ(std::string(errors::kInteractionRequired), error);
}

IN_PROC_BROWSER_TEST_F(LaunchWebAuthFlowFunctionTest, LoadFailed) {
  net::SpawnedTestServer https_server(
      net::SpawnedTestServer::TYPE_HTTPS,
      net::SpawnedTestServer::kLocalhost,
      base::FilePath(FILE_PATH_LITERAL(
          "chrome/test/data/extensions/api_test/identity")));
  ASSERT_TRUE(https_server.Start());
  GURL auth_url(https_server.GetURL("files/five_hundred.html"));

  scoped_refptr<IdentityLaunchWebAuthFlowFunction> function(
      new IdentityLaunchWebAuthFlowFunction());
  scoped_refptr<Extension> empty_extension(
      utils::CreateEmptyExtension());
  function->set_extension(empty_extension.get());

  std::string args = "[{\"interactive\": true, \"url\": \"" +
      auth_url.spec() + "\"}]";
  std::string error =
      utils::RunFunctionAndReturnError(function.get(), args, browser());

  EXPECT_EQ(std::string(errors::kPageLoadFailure), error);
}

IN_PROC_BROWSER_TEST_F(LaunchWebAuthFlowFunctionTest, NonInteractiveSuccess) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  scoped_refptr<IdentityLaunchWebAuthFlowFunction> function(
      new IdentityLaunchWebAuthFlowFunction());
  scoped_refptr<Extension> empty_extension(
      utils::CreateEmptyExtension());
  function->set_extension(empty_extension.get());

  function->InitFinalRedirectURLPrefixForTest("abcdefghij");
  scoped_ptr<base::Value> value(utils::RunFunctionAndReturnSingleResult(
      function.get(),
      "[{\"interactive\": false,"
      "\"url\": \"https://abcdefghij.chromiumapp.org/callback#test\"}]",
      browser()));

  std::string url;
  EXPECT_TRUE(value->GetAsString(&url));
  EXPECT_EQ(std::string("https://abcdefghij.chromiumapp.org/callback#test"),
            url);
}

IN_PROC_BROWSER_TEST_F(
    LaunchWebAuthFlowFunctionTest, InteractiveFirstNavigationSuccess) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  scoped_refptr<IdentityLaunchWebAuthFlowFunction> function(
      new IdentityLaunchWebAuthFlowFunction());
  scoped_refptr<Extension> empty_extension(
      utils::CreateEmptyExtension());
  function->set_extension(empty_extension.get());

  function->InitFinalRedirectURLPrefixForTest("abcdefghij");
  scoped_ptr<base::Value> value(utils::RunFunctionAndReturnSingleResult(
      function.get(),
      "[{\"interactive\": true,"
      "\"url\": \"https://abcdefghij.chromiumapp.org/callback#test\"}]",
      browser()));

  std::string url;
  EXPECT_TRUE(value->GetAsString(&url));
  EXPECT_EQ(std::string("https://abcdefghij.chromiumapp.org/callback#test"),
            url);
}

IN_PROC_BROWSER_TEST_F(
    LaunchWebAuthFlowFunctionTest, InteractiveSecondNavigationSuccess) {
  net::SpawnedTestServer https_server(
      net::SpawnedTestServer::TYPE_HTTPS,
      net::SpawnedTestServer::kLocalhost,
      base::FilePath(FILE_PATH_LITERAL(
          "chrome/test/data/extensions/api_test/identity")));
  ASSERT_TRUE(https_server.Start());
  GURL auth_url(https_server.GetURL("files/redirect_to_chromiumapp.html"));

  scoped_refptr<IdentityLaunchWebAuthFlowFunction> function(
      new IdentityLaunchWebAuthFlowFunction());
  scoped_refptr<Extension> empty_extension(
      utils::CreateEmptyExtension());
  function->set_extension(empty_extension.get());

  function->InitFinalRedirectURLPrefixForTest("abcdefghij");
  std::string args = "[{\"interactive\": true, \"url\": \"" +
      auth_url.spec() + "\"}]";
  scoped_ptr<base::Value> value(
      utils::RunFunctionAndReturnSingleResult(function.get(), args, browser()));

  std::string url;
  EXPECT_TRUE(value->GetAsString(&url));
  EXPECT_EQ(std::string("https://abcdefghij.chromiumapp.org/callback#test"),
            url);
}

}  // namespace extensions
