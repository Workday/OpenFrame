// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/desktop_notification_service.h"

#include "base/bind.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/synchronization/waitable_event.h"
#include "chrome/browser/notifications/desktop_notification_service_factory.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/public/web/WebNotificationPresenter.h"

class DesktopNotificationServiceTest : public ChromeRenderViewHostTestHarness {
 protected:
  virtual void SetUp() {
    ChromeRenderViewHostTestHarness::SetUp();

    // Creates the destop notification service.
    service_ = DesktopNotificationServiceFactory::GetForProfile(profile());
  }

  DesktopNotificationService* service_;
};

TEST_F(DesktopNotificationServiceTest, SettingsForSchemes) {
  GURL url("file:///html/test.html");

  EXPECT_EQ(CONTENT_SETTING_ASK,
            service_->GetDefaultContentSetting(NULL));
  EXPECT_EQ(WebKit::WebNotificationPresenter::PermissionNotAllowed,
            service_->HasPermission(url));

  service_->GrantPermission(url);
  EXPECT_EQ(WebKit::WebNotificationPresenter::PermissionAllowed,
            service_->HasPermission(url));

  service_->DenyPermission(url);
  EXPECT_EQ(WebKit::WebNotificationPresenter::PermissionDenied,
            service_->HasPermission(url));

  GURL https_url("https://testurl");
  GURL http_url("http://testurl");
  EXPECT_EQ(CONTENT_SETTING_ASK,
            service_->GetDefaultContentSetting(NULL));
  EXPECT_EQ(WebKit::WebNotificationPresenter::PermissionNotAllowed,
            service_->HasPermission(http_url));
  EXPECT_EQ(WebKit::WebNotificationPresenter::PermissionNotAllowed,
            service_->HasPermission(https_url));

  service_->GrantPermission(https_url);
  EXPECT_EQ(WebKit::WebNotificationPresenter::PermissionNotAllowed,
            service_->HasPermission(http_url));
  EXPECT_EQ(WebKit::WebNotificationPresenter::PermissionAllowed,
            service_->HasPermission(https_url));

  service_->DenyPermission(http_url);
  EXPECT_EQ(WebKit::WebNotificationPresenter::PermissionDenied,
            service_->HasPermission(http_url));
  EXPECT_EQ(WebKit::WebNotificationPresenter::PermissionAllowed,
            service_->HasPermission(https_url));
}

TEST_F(DesktopNotificationServiceTest, GetNotificationsSettings) {
  service_->GrantPermission(GURL("http://allowed2.com"));
  service_->GrantPermission(GURL("http://allowed.com"));
  service_->DenyPermission(GURL("http://denied2.com"));
  service_->DenyPermission(GURL("http://denied.com"));

  ContentSettingsForOneType settings;
  service_->GetNotificationsSettings(&settings);
  // |settings| contains the default setting and 4 exceptions.
  ASSERT_EQ(5u, settings.size());

  EXPECT_EQ(ContentSettingsPattern::FromURLNoWildcard(
                GURL("http://allowed.com")),
            settings[0].primary_pattern);
  EXPECT_EQ(CONTENT_SETTING_ALLOW,
            settings[0].setting);
  EXPECT_EQ(ContentSettingsPattern::FromURLNoWildcard(
                GURL("http://allowed2.com")),
            settings[1].primary_pattern);
  EXPECT_EQ(CONTENT_SETTING_ALLOW,
            settings[1].setting);
  EXPECT_EQ(ContentSettingsPattern::FromURLNoWildcard(
                GURL("http://denied.com")),
            settings[2].primary_pattern);
  EXPECT_EQ(CONTENT_SETTING_BLOCK,
            settings[2].setting);
  EXPECT_EQ(ContentSettingsPattern::FromURLNoWildcard(
                GURL("http://denied2.com")),
            settings[3].primary_pattern);
  EXPECT_EQ(CONTENT_SETTING_BLOCK,
            settings[3].setting);
  EXPECT_EQ(ContentSettingsPattern::Wildcard(),
            settings[4].primary_pattern);
  EXPECT_EQ(CONTENT_SETTING_ASK,
            settings[4].setting);
}
