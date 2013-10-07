// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/ui/android/tab_model/tab_model.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

typedef testing::Test TabModelTest;

namespace {
class TabModelAndroidProfileMock : public TestingProfile {
 public:
  TabModelAndroidProfileMock() {}
  virtual ~TabModelAndroidProfileMock() {}

  MOCK_METHOD0(GetOffTheRecordProfile, Profile*());
  MOCK_METHOD0(HasOffTheRecordProfile, bool());
};
}  // namespace

class TestTabModel : public TabModel {
 public:
  explicit TestTabModel(Profile* profile)
    : TabModel(profile) {}

  virtual int GetTabCount() const OVERRIDE { return 0; }
  virtual int GetActiveIndex() const OVERRIDE { return 0; }
  virtual content::WebContents* GetWebContentsAt(int index) const OVERRIDE {
    return NULL;
  }
  virtual void CreateTab(content::WebContents* web_contents) OVERRIDE {}
  virtual content::WebContents* CreateTabForTesting(const GURL& url) OVERRIDE {
    return NULL;
  }
  virtual bool IsSessionRestoreInProgress() const OVERRIDE { return false; }
  virtual void OpenClearBrowsingData() const OVERRIDE {}
  virtual browser_sync::SyncedTabDelegate* GetTabAt(int index) const OVERRIDE {
    return NULL;
  }

};

TEST_F(TabModelTest, TestProfileHandling) {
  // Construct TabModel with standard Profile.
  TestingProfile testing_profile;
  TestTabModel tab_model(&testing_profile);

  // Verify TabModel has the correct profile and profile type.
  EXPECT_EQ(&testing_profile, tab_model.GetProfile());
  EXPECT_FALSE(tab_model.IsOffTheRecord());

  // Notify profile is being destroyed and verify pointer is cleared.
  content::NotificationService::current()->Notify(
    chrome::NOTIFICATION_PROFILE_DESTROYED,
    content::Source<Profile>(&testing_profile),
    content::NotificationService::NoDetails());
  EXPECT_EQ(NULL, tab_model.GetProfile());
}

TEST_F(TabModelTest, TestProfileHandlingOffTheRecord) {
  // Construct TabModel with off-the-record Profile.
  TabModelAndroidProfileMock testing_profile;
  EXPECT_CALL(testing_profile, HasOffTheRecordProfile())
    .WillOnce(testing::Return(true));
  EXPECT_CALL(testing_profile, GetOffTheRecordProfile())
    .WillOnce(testing::Return(&testing_profile));
  TestTabModel tab_model(&testing_profile);

  // Verify TabModel has the correct profile and profile type.
  EXPECT_EQ(&testing_profile, tab_model.GetProfile());
  EXPECT_TRUE(tab_model.IsOffTheRecord());

  // Notify profile is being destroyed and verify pointer is cleared.
  content::NotificationService::current()->Notify(
    chrome::NOTIFICATION_PROFILE_DESTROYED,
    content::Source<Profile>(&testing_profile),
    content::NotificationService::NoDetails());
  EXPECT_EQ(NULL, tab_model.GetProfile());
}
