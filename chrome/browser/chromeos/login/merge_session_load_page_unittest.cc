// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/run_loop.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/login/merge_session_load_page.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "content/public/browser/interstitial_page.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"

using content::InterstitialPage;
using content::WebContents;
using content::WebContentsTester;

namespace {

const char kURL1[] = "http://www.google.com/";
const char kURL2[] = "http://mail.google.com/";

}  // namespace

namespace chromeos {

class MergeSessionLoadPageTest;

// An MergeSessionLoadPage class that does not create windows.
class TestMergeSessionLoadPage :  public MergeSessionLoadPage {
 public:
  TestMergeSessionLoadPage(WebContents* web_contents,
                           const GURL& url,
                           MergeSessionLoadPageTest* test_page)
    : MergeSessionLoadPage(web_contents, url, CompletionCallback()),
      test_page_(test_page) {
    interstitial_page_->DontCreateViewForTesting();
  }

 private:
  MergeSessionLoadPageTest* test_page_;

  DISALLOW_COPY_AND_ASSIGN(TestMergeSessionLoadPage);
};

class MergeSessionLoadPageTest : public ChromeRenderViewHostTestHarness {
 protected:
  virtual void SetUp() OVERRIDE {
    ChromeRenderViewHostTestHarness::SetUp();
#if defined OS_CHROMEOS
  test_user_manager_.reset(new chromeos::ScopedTestUserManager());
#endif
  }

  virtual void TearDown() OVERRIDE {
#if defined OS_CHROMEOS
    // Clean up pending tasks that might depend on the user manager.
    base::RunLoop().RunUntilIdle();
    test_user_manager_.reset();
#endif
    ChromeRenderViewHostTestHarness::TearDown();
  }

  void Navigate(const char* url, int page_id) {
    WebContentsTester::For(web_contents())->TestDidNavigate(
        web_contents()->GetRenderViewHost(), page_id, GURL(url),
        content::PAGE_TRANSITION_TYPED);
  }

  void ShowInterstitial(const char* url) {
    (new TestMergeSessionLoadPage(web_contents(), GURL(url), this))->Show();
  }

  // Returns the MergeSessionLoadPage currently showing or NULL if none is
  // showing.
  InterstitialPage* GetMergeSessionLoadPage() {
    return InterstitialPage::GetInterstitialPage(web_contents());
  }

 private:
  ScopedStubNetworkLibraryEnabler stub_network_library_enabler_;
  ScopedTestDeviceSettingsService test_device_settings_service_;
  ScopedTestCrosSettings test_cros_settings_;
  scoped_ptr<chromeos::ScopedTestUserManager> test_user_manager_;
};

TEST_F(MergeSessionLoadPageTest, MergeSessionPageNotShown) {
  UserManager::Get()->SetMergeSessionState(
      UserManager::MERGE_STATUS_DONE);
  // Start a load.
  Navigate(kURL1, 1);
  // Load next page.
  controller().LoadURL(GURL(kURL2), content::Referrer(),
                       content::PAGE_TRANSITION_TYPED, std::string());

  // Simulate the load causing an merge session interstitial page
  // to be shown.
  InterstitialPage* interstitial = GetMergeSessionLoadPage();
  EXPECT_FALSE(interstitial);
}

TEST_F(MergeSessionLoadPageTest, MergeSessionPageShown) {
  UserManager::Get()->SetMergeSessionState(
      UserManager::MERGE_STATUS_IN_PROCESS);
  // Start a load.
  Navigate(kURL1, 1);
  // Load next page.
  controller().LoadURL(GURL(kURL2), content::Referrer(),
                       content::PAGE_TRANSITION_TYPED, std::string());

  // Simulate the load causing an merge session interstitial page
  // to be shown.
  ShowInterstitial(kURL2);
  InterstitialPage* interstitial = GetMergeSessionLoadPage();
  ASSERT_TRUE(interstitial);
  base::RunLoop().RunUntilIdle();

  // Simulate merge session completion.
  UserManager::Get()->SetMergeSessionState(
      UserManager::MERGE_STATUS_DONE);
  base::RunLoop().RunUntilIdle();

  // The URL remains to be URL2.
  EXPECT_EQ(kURL2, web_contents()->GetVisibleURL().spec());

  // Commit navigation and the interstitial page is gone.
  Navigate(kURL2, 2);
  EXPECT_FALSE(GetMergeSessionLoadPage());
}

}  // namespace chromeos
