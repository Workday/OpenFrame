// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NOTIFICATIONS_DESKTOP_NOTIFICATIONS_UNITTEST_H_
#define CHROME_BROWSER_NOTIFICATIONS_DESKTOP_NOTIFICATIONS_UNITTEST_H_

#include <deque>
#include <string>

#include "base/message_loop/message_loop.h"
#include "base/prefs/testing_pref_service.h"
#include "chrome/browser/notifications/balloon_collection_impl.h"
#include "chrome/browser/notifications/balloon_notification_ui_manager.h"
#include "chrome/browser/notifications/desktop_notification_service.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/notifications/notification_test_util.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/render_view_test.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

class ActiveDesktopMonitor;
class DesktopNotificationsTest;
typedef LoggingNotificationDelegate<DesktopNotificationsTest>
    LoggingNotificationProxy;

// Test version of the balloon collection which counts the number
// of notifications that are added to it.
class MockBalloonCollection : public BalloonCollectionImpl {
 public:
  MockBalloonCollection();
  virtual ~MockBalloonCollection();

  // Our mock collection has an area large enough for a fixed number
  // of balloons.
  static const int kMockBalloonSpace;
  int max_balloon_count() const { return kMockBalloonSpace; }

  // BalloonCollectionImpl overrides
  virtual void Add(const Notification& notification,
                   Profile* profile) OVERRIDE;
  virtual bool HasSpace() const OVERRIDE;
  virtual Balloon* MakeBalloon(const Notification& notification,
                               Profile* profile) OVERRIDE;
  virtual void DisplayChanged() OVERRIDE {}
  virtual void OnBalloonClosed(Balloon* source) OVERRIDE;
  virtual const BalloonCollection::Balloons& GetActiveBalloons() OVERRIDE;

  // Number of balloons being shown.
  std::deque<Balloon*>& balloons() { return balloons_; }
  int count() const { return balloons_.size(); }

  // Returns the highest y-coordinate of all the balloons in the collection.
  int UppermostVerticalPosition();

  // Returns the height bounds of a balloon.
  int MinHeight() { return Layout::min_balloon_height(); }
  int MaxHeight() { return Layout::max_balloon_height(); }

  // Returns the bounding box.
  gfx::Rect GetBalloonsBoundingBox() {
    return BalloonCollectionImpl::GetBalloonsBoundingBox();
  }

 private:
  std::deque<Balloon*> balloons_;
};

class DesktopNotificationsTest : public testing::Test {
 public:
  DesktopNotificationsTest();
  virtual ~DesktopNotificationsTest();

  static void log(const std::string& message) {
    log_output_.append(message);
  }

  Profile* profile() { return profile_.get(); }

 protected:
  // testing::Test overrides
  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

  void AllowOrigin(const GURL& origin) {
    service_->GrantPermission(origin);
  }

  void DenyOrigin(const GURL& origin) {
    service_->DenyPermission(origin);
  }

  // Constructs a notification parameter structure for use in tests.
  content::ShowDesktopNotificationHostMsgParams StandardTestNotification();

  // Create a message loop to allow notifications code to post tasks,
  // and a thread so that notifications code runs on the expected thread.
  base::MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;

  // Local state mock.
  TestingPrefServiceSimple local_state_;

  // Test profile.
  scoped_ptr<TestingProfile> profile_;

  // Mock balloon collection -- owned by the NotificationUIManager
  MockBalloonCollection* balloon_collection_;

  // Real UI manager.
  scoped_ptr<BalloonNotificationUIManager> ui_manager_;

  // Real DesktopNotificationService
  scoped_ptr<DesktopNotificationService> service_;

  // Contains the cumulative output of the unit test.
  static std::string log_output_;
};

#endif  // CHROME_BROWSER_NOTIFICATIONS_DESKTOP_NOTIFICATIONS_UNITTEST_H_
