// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/base/browser_with_test_window_test.h"

#include "base/run_loop.h"
#include "chrome/browser/profiles/profile_destroyer.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/render_messages.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/page_transition_types.h"
#include "content/public/test/test_renderer_host.h"

#if defined(USE_AURA)
#include "ui/aura/test/aura_test_helper.h"
#endif

#if defined(USE_ASH)
#include "ash/test/ash_test_helper.h"
#endif

using content::NavigationController;
using content::RenderViewHost;
using content::RenderViewHostTester;
using content::WebContents;

BrowserWithTestWindowTest::BrowserWithTestWindowTest()
    : host_desktop_type_(chrome::HOST_DESKTOP_TYPE_NATIVE) {
}

BrowserWithTestWindowTest::~BrowserWithTestWindowTest() {
}

void BrowserWithTestWindowTest::SetHostDesktopType(
    chrome::HostDesktopType host_desktop_type) {
  DCHECK(!window_);
  host_desktop_type_ = host_desktop_type;
}

void BrowserWithTestWindowTest::SetUp() {
  testing::Test::SetUp();
#if defined(OS_CHROMEOS)
  // TODO(jamescook): Windows Ash support. This will require refactoring
  // AshTestHelper and AuraTestHelper so they can be used at the same time,
  // perhaps by AshTestHelper owning an AuraTestHelper.
  ash_test_helper_.reset(new ash::test::AshTestHelper(
      base::MessageLoopForUI::current()));
  ash_test_helper_->SetUp(true);
#elif defined(USE_AURA)
  aura_test_helper_.reset(new aura::test::AuraTestHelper(
      base::MessageLoopForUI::current()));
  aura_test_helper_->SetUp();
#endif  // USE_AURA

  // Subclasses can provide their own Profile.
  profile_.reset(CreateProfile());
  // Subclasses can provide their own test BrowserWindow. If they return NULL
  // then Browser will create the a production BrowserWindow and the subclass
  // is responsible for cleaning it up (usually by NativeWidget destruction).
  window_.reset(CreateBrowserWindow());

  Browser::CreateParams params(profile(), host_desktop_type_);
  params.window = window_.get();
  browser_.reset(new Browser(params));
}

void BrowserWithTestWindowTest::TearDown() {
  // Some tests end up posting tasks to the DB thread that must be completed
  // before the profile can be destroyed and the test safely shut down.
  base::RunLoop().RunUntilIdle();

  // Reset the profile here because some profile keyed services (like the
  // audio service) depend on test stubs that the helpers below will remove.
  DestroyBrowserAndProfile();

#if defined(OS_CHROMEOS)
  ash_test_helper_->TearDown();
#elif defined(USE_AURA)
  aura_test_helper_->TearDown();
#endif
  testing::Test::TearDown();

  // A Task is leaked if we don't destroy everything, then run the message
  // loop.
  base::MessageLoop::current()->PostTask(FROM_HERE,
                                         base::MessageLoop::QuitClosure());
  base::MessageLoop::current()->Run();
}

void BrowserWithTestWindowTest::AddTab(Browser* browser, const GURL& url) {
  chrome::NavigateParams params(browser, url, content::PAGE_TRANSITION_TYPED);
  params.tabstrip_index = 0;
  params.disposition = NEW_FOREGROUND_TAB;
  chrome::Navigate(&params);
  CommitPendingLoad(&params.target_contents->GetController());
}

void BrowserWithTestWindowTest::CommitPendingLoad(
  NavigationController* controller) {
  if (!controller->GetPendingEntry())
    return;  // Nothing to commit.

  RenderViewHost* old_rvh =
      controller->GetWebContents()->GetRenderViewHost();

  RenderViewHost* pending_rvh = RenderViewHostTester::GetPendingForController(
      controller);
  if (pending_rvh) {
    // Simulate the ShouldClose_ACK that is received from the current renderer
    // for a cross-site navigation.
    DCHECK_NE(old_rvh, pending_rvh);
    RenderViewHostTester::For(old_rvh)->SendShouldCloseACK(true);
  }
  // Commit on the pending_rvh, if one exists.
  RenderViewHost* test_rvh = pending_rvh ? pending_rvh : old_rvh;
  RenderViewHostTester* test_rvh_tester = RenderViewHostTester::For(test_rvh);

  // Simulate a SwapOut_ACK before the navigation commits.
  if (pending_rvh)
    RenderViewHostTester::For(old_rvh)->SimulateSwapOutACK();

  // For new navigations, we need to send a larger page ID. For renavigations,
  // we need to send the preexisting page ID. We can tell these apart because
  // renavigations will have a pending_entry_index while new ones won't (they'll
  // just have a standalong pending_entry that isn't in the list already).
  if (controller->GetPendingEntryIndex() >= 0) {
    test_rvh_tester->SendNavigateWithTransition(
        controller->GetPendingEntry()->GetPageID(),
        controller->GetPendingEntry()->GetURL(),
        controller->GetPendingEntry()->GetTransitionType());
  } else {
    test_rvh_tester->SendNavigateWithTransition(
        controller->GetWebContents()->
            GetMaxPageIDForSiteInstance(test_rvh->GetSiteInstance()) + 1,
        controller->GetPendingEntry()->GetURL(),
        controller->GetPendingEntry()->GetTransitionType());
  }
}

void BrowserWithTestWindowTest::NavigateAndCommit(
    NavigationController* controller,
    const GURL& url) {
  controller->LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_LINK, std::string());
  CommitPendingLoad(controller);
}

void BrowserWithTestWindowTest::NavigateAndCommitActiveTab(const GURL& url) {
  NavigateAndCommit(&browser()->tab_strip_model()->GetActiveWebContents()->
                        GetController(),
                    url);
}

void BrowserWithTestWindowTest::NavigateAndCommitActiveTabWithTitle(
    Browser* navigating_browser,
    const GURL& url,
    const string16& title) {
  NavigationController* controller = &navigating_browser->tab_strip_model()->
      GetActiveWebContents()->GetController();
  NavigateAndCommit(controller, url);
  controller->GetActiveEntry()->SetTitle(title);
}

void BrowserWithTestWindowTest::DestroyBrowserAndProfile() {
  if (browser_.get()) {
    // Make sure we close all tabs, otherwise Browser isn't happy in its
    // destructor.
    browser()->tab_strip_model()->CloseAllTabs();
    browser_.reset(NULL);
  }
  window_.reset(NULL);
  // Destroy the profile here - otherwise, if the profile is freed in the
  // destructor, and a test subclass owns a resource that the profile depends
  // on (such as g_browser_process()->local_state()) there's no way for the
  // subclass to free it after the profile.
  profile_.reset(NULL);
}

TestingProfile* BrowserWithTestWindowTest::CreateProfile() {
  return new TestingProfile();
}

BrowserWindow* BrowserWithTestWindowTest::CreateBrowserWindow() {
  return new TestBrowserWindow();
}
