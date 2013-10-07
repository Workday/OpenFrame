// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/basictypes.h"
#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/background/background_contents_service.h"
#include "chrome/browser/background/background_contents_service_factory.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/tab_contents/background_contents.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/notification_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"
#include "url/gurl.h"

class BackgroundContentsServiceTest : public testing::Test {
 public:
  BackgroundContentsServiceTest() {}
  virtual ~BackgroundContentsServiceTest() {}
  virtual void SetUp() {
    command_line_.reset(new CommandLine(CommandLine::NO_PROGRAM));
  }

  const DictionaryValue* GetPrefs(Profile* profile) {
    return profile->GetPrefs()->GetDictionary(
        prefs::kRegisteredBackgroundContents);
  }

  // Returns the stored pref URL for the passed app id.
  std::string GetPrefURLForApp(Profile* profile, const string16& appid) {
    const DictionaryValue* pref = GetPrefs(profile);
    EXPECT_TRUE(pref->HasKey(UTF16ToUTF8(appid)));
    const DictionaryValue* value;
    pref->GetDictionaryWithoutPathExpansion(UTF16ToUTF8(appid), &value);
    std::string url;
    value->GetString("url", &url);
    return url;
  }

  scoped_ptr<CommandLine> command_line_;
};

class MockBackgroundContents : public BackgroundContents {
 public:
  explicit MockBackgroundContents(Profile* profile)
      : appid_(ASCIIToUTF16("app_id")),
        profile_(profile) {
  }
  MockBackgroundContents(Profile* profile, const std::string& id)
      : appid_(ASCIIToUTF16(id)),
        profile_(profile) {
  }

  void SendOpenedNotification(BackgroundContentsService* service) {
    string16 frame_name = ASCIIToUTF16("background");
    BackgroundContentsOpenedDetails details = {
        this, frame_name, appid_ };
    service->BackgroundContentsOpened(&details);
  }

  virtual void Navigate(GURL url) {
    url_ = url;
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_BACKGROUND_CONTENTS_NAVIGATED,
        content::Source<Profile>(profile_),
        content::Details<BackgroundContents>(this));
  }
  virtual const GURL& GetURL() const OVERRIDE { return url_; }

  void MockClose(Profile* profile) {
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_BACKGROUND_CONTENTS_CLOSED,
        content::Source<Profile>(profile),
        content::Details<BackgroundContents>(this));
    delete this;
  }

  virtual ~MockBackgroundContents() {
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_BACKGROUND_CONTENTS_DELETED,
        content::Source<Profile>(profile_),
        content::Details<BackgroundContents>(this));
  }

  const string16& appid() { return appid_; }

 private:
  GURL url_;

  // The ID of our parent application
  string16 appid_;

  // Parent profile
  Profile* profile_;
};

TEST_F(BackgroundContentsServiceTest, Create) {
  // Check for creation and leaks.
  TestingProfile profile;
  BackgroundContentsService service(&profile, command_line_.get());
}

TEST_F(BackgroundContentsServiceTest, BackgroundContentsCreateDestroy) {
  TestingProfile profile;
  BackgroundContentsService service(&profile, command_line_.get());
  MockBackgroundContents* contents = new MockBackgroundContents(&profile);
  EXPECT_FALSE(service.IsTracked(contents));
  contents->SendOpenedNotification(&service);
  EXPECT_TRUE(service.IsTracked(contents));
  delete contents;
  EXPECT_FALSE(service.IsTracked(contents));
}

TEST_F(BackgroundContentsServiceTest, BackgroundContentsUrlAdded) {
  TestingProfile profile;
  BackgroundContentsService service(&profile, command_line_.get());
  BackgroundContentsServiceFactory::GetInstance()->
      RegisterUserPrefsOnBrowserContext(&profile);
  GURL orig_url;
  GURL url("http://a/");
  GURL url2("http://a/");
  {
    scoped_ptr<MockBackgroundContents> contents(
        new MockBackgroundContents(&profile));
    EXPECT_EQ(0U, GetPrefs(&profile)->size());
    contents->SendOpenedNotification(&service);

    contents->Navigate(url);
    EXPECT_EQ(1U, GetPrefs(&profile)->size());
    EXPECT_EQ(url.spec(), GetPrefURLForApp(&profile, contents->appid()));

    // Navigate the contents to a new url, should not change url.
    contents->Navigate(url2);
    EXPECT_EQ(1U, GetPrefs(&profile)->size());
    EXPECT_EQ(url.spec(), GetPrefURLForApp(&profile, contents->appid()));
  }
  // Contents are deleted, url should persist.
  EXPECT_EQ(1U, GetPrefs(&profile)->size());
}

TEST_F(BackgroundContentsServiceTest, BackgroundContentsUrlAddedAndClosed) {
  TestingProfile profile;
  BackgroundContentsService service(&profile, command_line_.get());
  BackgroundContentsServiceFactory::GetInstance()->
      RegisterUserPrefsOnBrowserContext(&profile);

  GURL url("http://a/");
  MockBackgroundContents* contents = new MockBackgroundContents(&profile);
  EXPECT_EQ(0U, GetPrefs(&profile)->size());
  contents->SendOpenedNotification(&service);
  contents->Navigate(url);
  EXPECT_EQ(1U, GetPrefs(&profile)->size());
  EXPECT_EQ(url.spec(), GetPrefURLForApp(&profile, contents->appid()));

  // Fake a window closed by script.
  contents->MockClose(&profile);
  EXPECT_EQ(0U, GetPrefs(&profile)->size());
}

// Test what happens if a BackgroundContents shuts down (say, due to a renderer
// crash) then is restarted. Should not persist URL twice.
TEST_F(BackgroundContentsServiceTest, RestartBackgroundContents) {
  TestingProfile profile;
  BackgroundContentsService service(&profile, command_line_.get());
  BackgroundContentsServiceFactory::GetInstance()->
      RegisterUserPrefsOnBrowserContext(&profile);

  GURL url("http://a/");
  {
    scoped_ptr<MockBackgroundContents> contents(new MockBackgroundContents(
        &profile, "appid"));
    contents->SendOpenedNotification(&service);
    contents->Navigate(url);
    EXPECT_EQ(1U, GetPrefs(&profile)->size());
    EXPECT_EQ(url.spec(), GetPrefURLForApp(&profile, contents->appid()));
  }
  // Contents deleted, url should be persisted.
  EXPECT_EQ(1U, GetPrefs(&profile)->size());

  {
    // Reopen the BackgroundContents to the same URL, we should not register the
    // URL again.
    scoped_ptr<MockBackgroundContents> contents(new MockBackgroundContents(
        &profile, "appid"));
    contents->SendOpenedNotification(&service);
    contents->Navigate(url);
    EXPECT_EQ(1U, GetPrefs(&profile)->size());
  }
}

// Ensures that BackgroundContentsService properly tracks the association
// between a BackgroundContents and its parent extension, including
// unregistering the BC when the extension is uninstalled.
TEST_F(BackgroundContentsServiceTest, TestApplicationIDLinkage) {
  TestingProfile profile;
  BackgroundContentsService service(&profile, command_line_.get());
  BackgroundContentsServiceFactory::GetInstance()->
      RegisterUserPrefsOnBrowserContext(&profile);

  EXPECT_EQ(NULL, service.GetAppBackgroundContents(ASCIIToUTF16("appid")));
  MockBackgroundContents* contents = new MockBackgroundContents(&profile,
                                                                "appid");
  scoped_ptr<MockBackgroundContents> contents2(
      new MockBackgroundContents(&profile, "appid2"));
  contents->SendOpenedNotification(&service);
  EXPECT_EQ(contents, service.GetAppBackgroundContents(contents->appid()));
  contents2->SendOpenedNotification(&service);
  EXPECT_EQ(contents2.get(), service.GetAppBackgroundContents(
      contents2->appid()));
  EXPECT_EQ(0U, GetPrefs(&profile)->size());

  // Navigate the contents, then make sure the one associated with the extension
  // is unregistered.
  GURL url("http://a/");
  GURL url2("http://b/");
  contents->Navigate(url);
  EXPECT_EQ(1U, GetPrefs(&profile)->size());
  contents2->Navigate(url2);
  EXPECT_EQ(2U, GetPrefs(&profile)->size());
  service.ShutdownAssociatedBackgroundContents(ASCIIToUTF16("appid"));
  EXPECT_FALSE(service.IsTracked(contents));
  EXPECT_EQ(NULL, service.GetAppBackgroundContents(ASCIIToUTF16("appid")));
  EXPECT_EQ(1U, GetPrefs(&profile)->size());
  EXPECT_EQ(url2.spec(), GetPrefURLForApp(&profile, contents2->appid()));
}
