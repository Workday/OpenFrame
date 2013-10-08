// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/notifications/balloon.h"
#include "chrome/browser/notifications/balloon_collection.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/notifications/notification_object_proxy.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/cocoa/cocoa_test_helper.h"
#include "chrome/browser/ui/cocoa/notifications/balloon_controller.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/test/base/testing_profile.h"

// Subclass balloon controller and mock out the initialization of the RVH.
@interface TestBalloonController : BalloonController {
}
- (void)initializeHost;
@end

@implementation TestBalloonController
- (void)initializeHost {}
@end

namespace {

// Use a dummy balloon collection for testing.
class MockBalloonCollection : public BalloonCollection {
  virtual void Add(const Notification& notification,
                   Profile* profile) OVERRIDE {}
  virtual const Notification* FindById(const std::string& id) const OVERRIDE {
    return NULL;
  }
  virtual bool RemoveById(const std::string& id) OVERRIDE { return false; }
  virtual bool RemoveBySourceOrigin(const GURL& origin) OVERRIDE {
    return false;
  }
  virtual bool RemoveByProfile(Profile* profile) OVERRIDE { return false; }
  virtual void RemoveAll() OVERRIDE {}
  virtual bool HasSpace() const OVERRIDE { return true; }
  virtual void ResizeBalloon(
      Balloon* balloon,
      const gfx::Size& size) OVERRIDE {
  }
  virtual void DisplayChanged() OVERRIDE {}
  virtual void SetPositionPreference(PositionPreference preference) OVERRIDE {}
  virtual void OnBalloonClosed(Balloon* source) OVERRIDE {};
  virtual const Balloons& GetActiveBalloons() OVERRIDE {
    NOTREACHED();
    return balloons_;
  }
 private:
  Balloons balloons_;
};

class BalloonControllerTest : public ChromeRenderViewHostTestHarness {
  virtual void SetUp() OVERRIDE {
    ChromeRenderViewHostTestHarness::SetUp();
    CocoaTest::BootstrapCocoa();
    Browser::CreateParams native_params(profile(), chrome::GetActiveDesktop());
    browser_.reset(
        chrome::CreateBrowserWithTestWindowForParams(&native_params));
    collection_.reset(new MockBalloonCollection());
  }

  virtual void TearDown() OVERRIDE {
    collection_.reset();
    browser_.reset();
    ChromeRenderViewHostTestHarness::TearDown();
  }

 protected:
  scoped_ptr<Browser> browser_;
  scoped_ptr<BalloonCollection> collection_;
};

TEST_F(BalloonControllerTest, ShowAndCloseTest) {
  Notification n(GURL("http://www.google.com"), GURL("http://www.google.com"),
      ASCIIToUTF16("http://www.google.com"), string16(),
      new NotificationObjectProxy(-1, -1, -1, false));
  scoped_ptr<Balloon> balloon(
      new Balloon(n, profile(), collection_.get()));
  balloon->SetPosition(gfx::Point(1, 1), false);
  balloon->set_content_size(gfx::Size(100, 100));

  BalloonController* controller =
      [[TestBalloonController alloc] initWithBalloon:balloon.get()];

  [controller showWindow:nil];
  [controller closeBalloon:YES];
}

TEST_F(BalloonControllerTest, SizesTest) {
  Notification n(GURL("http://www.google.com"), GURL("http://www.google.com"),
      ASCIIToUTF16("http://www.google.com"), string16(),
      new NotificationObjectProxy(-1, -1, -1, false));
  scoped_ptr<Balloon> balloon(
      new Balloon(n, profile(), collection_.get()));
  balloon->SetPosition(gfx::Point(1, 1), false);
  balloon->set_content_size(gfx::Size(100, 100));

  BalloonController* controller =
      [[TestBalloonController alloc] initWithBalloon:balloon.get()];

  [controller showWindow:nil];

  EXPECT_TRUE([controller desiredTotalWidth] > 100);
  EXPECT_TRUE([controller desiredTotalHeight] > 100);

  [controller closeBalloon:YES];
}

}
