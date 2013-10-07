// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_loop.h"
#include "chrome/browser/extensions/api/identity/experimental_web_auth_flow.h"
#include "chrome/browser/ui/host_desktop.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::WebContents;
using content::WebContentsTester;
using extensions::ExperimentalWebAuthFlow;
using testing::Return;
using testing::ReturnRef;

namespace {

class MockDelegate : public ExperimentalWebAuthFlow::Delegate {
 public:
  MOCK_METHOD1(OnAuthFlowFailure,
               void(ExperimentalWebAuthFlow::Failure failure));
  MOCK_METHOD1(OnAuthFlowURLChange, void(const GURL& redirect_url));
};

class MockExperimentalWebAuthFlow : public ExperimentalWebAuthFlow {
 public:
  MockExperimentalWebAuthFlow(ExperimentalWebAuthFlow::Delegate* delegate,
                              Profile* profile,
                              const GURL& provider_url,
                              bool interactive)
      : ExperimentalWebAuthFlow(
            delegate,
            profile,
            provider_url,
            interactive ? ExperimentalWebAuthFlow::INTERACTIVE
                        : ExperimentalWebAuthFlow::SILENT,
            gfx::Rect(),
            chrome::GetActiveDesktop()),
        profile_(profile),
        web_contents_(NULL),
        window_shown_(false) {}

  virtual WebContents* CreateWebContents() OVERRIDE {
    CHECK(!web_contents_);
    web_contents_ = WebContentsTester::CreateTestWebContents(profile_, NULL);
    return web_contents_;
  }

  virtual void ShowAuthFlowPopup() OVERRIDE {
    window_shown_ = true;
  }

  bool HasWindow() const {
    return window_shown_;
  }

  void DestroyWebContents() {
    CHECK(web_contents_);
    delete web_contents_;
  }

  virtual ~MockExperimentalWebAuthFlow() { }

 private:
  Profile* profile_;
  WebContents* web_contents_;
  bool window_shown_;
};

}  // namespace

class ExperimentalWebAuthFlowTest : public ChromeRenderViewHostTestHarness {
 protected:
  virtual void TearDown() {
    // DetachDelegateAndDelete posts a task to clean up |flow_|, so it
    // has to be called before
    // ChromeRenderViewHostTestHarness::TearDown().
    flow_.release()->DetachDelegateAndDelete();
    ChromeRenderViewHostTestHarness::TearDown();
  }

  void CreateAuthFlow(const GURL& url,
                      bool interactive) {
    flow_.reset(new MockExperimentalWebAuthFlow(
        &delegate_, profile(), url, interactive));
  }

  ExperimentalWebAuthFlow* flow_base() {
    return flow_.get();
  }

  void CallBeforeUrlLoaded(const GURL& url) {
    flow_base()->BeforeUrlLoaded(url);
  }

  void CallAfterUrlLoaded() {
    flow_base()->AfterUrlLoaded();
  }

  MockDelegate delegate_;
  scoped_ptr<MockExperimentalWebAuthFlow> flow_;
};

TEST_F(ExperimentalWebAuthFlowTest,
       SilentRedirectToChromiumAppUrlNonInteractive) {
  GURL url("https://accounts.google.com/o/oauth2/auth");
  GURL result("https://abcdefghij.chromiumapp.org/google_cb");

  CreateAuthFlow(url, false);
  EXPECT_CALL(delegate_, OnAuthFlowURLChange(result)).Times(1);
  flow_->Start();
  CallBeforeUrlLoaded(result);
}

TEST_F(ExperimentalWebAuthFlowTest, SilentRedirectToChromiumAppUrlInteractive) {
  GURL url("https://accounts.google.com/o/oauth2/auth");
  GURL result("https://abcdefghij.chromiumapp.org/google_cb");

  CreateAuthFlow(url, true);
  EXPECT_CALL(delegate_, OnAuthFlowURLChange(result)).Times(1);
  flow_->Start();
  CallBeforeUrlLoaded(result);
}

TEST_F(ExperimentalWebAuthFlowTest, SilentRedirectToChromeExtensionSchemeUrl) {
  GURL url("https://accounts.google.com/o/oauth2/auth");
  GURL result("chrome-extension://abcdefghij/google_cb");

  CreateAuthFlow(url, true);
  EXPECT_CALL(delegate_, OnAuthFlowURLChange(result)).Times(1);
  flow_->Start();
  CallBeforeUrlLoaded(result);
}

TEST_F(ExperimentalWebAuthFlowTest, NeedsUIButNonInteractive) {
  GURL url("https://accounts.google.com/o/oauth2/auth");

  CreateAuthFlow(url, false);
  EXPECT_CALL(delegate_,
              OnAuthFlowFailure(ExperimentalWebAuthFlow::INTERACTION_REQUIRED))
      .Times(1);
  flow_->Start();
  CallAfterUrlLoaded();
}

TEST_F(ExperimentalWebAuthFlowTest, UIResultsInSuccess) {
  GURL url("https://accounts.google.com/o/oauth2/auth");
  GURL result("chrome-extension://abcdefghij/google_cb");

  CreateAuthFlow(url, true);
  EXPECT_CALL(delegate_, OnAuthFlowURLChange(result)).Times(1);
  flow_->Start();
  CallAfterUrlLoaded();
  EXPECT_TRUE(flow_->HasWindow());
  CallBeforeUrlLoaded(result);
}

TEST_F(ExperimentalWebAuthFlowTest, UIClosedByUser) {
  GURL url("https://accounts.google.com/o/oauth2/auth");
  GURL result("chrome-extension://abcdefghij/google_cb");

  CreateAuthFlow(url, true);
  EXPECT_CALL(delegate_,
              OnAuthFlowFailure(ExperimentalWebAuthFlow::WINDOW_CLOSED))
      .Times(1);
  flow_->Start();
  CallAfterUrlLoaded();
  EXPECT_TRUE(flow_->HasWindow());
  flow_->DestroyWebContents();
}
