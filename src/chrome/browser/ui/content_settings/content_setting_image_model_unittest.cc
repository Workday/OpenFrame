// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/content_settings/tab_specific_content_settings.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/content_settings/content_setting_image_model.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/test_renderer_host.h"
#include "net/cookies/cookie_options.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// Forward all NOTIFICATION_WEB_CONTENT_SETTINGS_CHANGED to the specified
// ContentSettingImageModel.
class NotificationForwarder : public content::NotificationObserver {
 public:
  explicit NotificationForwarder(ContentSettingImageModel* model)
      : model_(model) {
    registrar_.Add(this,
                   chrome::NOTIFICATION_WEB_CONTENT_SETTINGS_CHANGED,
                   content::NotificationService::AllSources());
  }
  virtual ~NotificationForwarder() {}

  void clear() {
    registrar_.RemoveAll();
  }

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    if (type == chrome::NOTIFICATION_WEB_CONTENT_SETTINGS_CHANGED) {
      model_->UpdateFromWebContents(
          content::Source<content::WebContents>(source).ptr());
    }
  }

 private:
  content::NotificationRegistrar registrar_;
  ContentSettingImageModel* model_;

  DISALLOW_COPY_AND_ASSIGN(NotificationForwarder);
};

class ContentSettingImageModelTest : public ChromeRenderViewHostTestHarness {
};

TEST_F(ContentSettingImageModelTest, UpdateFromWebContents) {
  TabSpecificContentSettings::CreateForWebContents(web_contents());
  TabSpecificContentSettings* content_settings =
      TabSpecificContentSettings::FromWebContents(web_contents());
  scoped_ptr<ContentSettingImageModel> content_setting_image_model(
     ContentSettingImageModel::CreateContentSettingImageModel(
         CONTENT_SETTINGS_TYPE_IMAGES));
  EXPECT_FALSE(content_setting_image_model->is_visible());
  EXPECT_EQ(0, content_setting_image_model->get_icon());
  EXPECT_TRUE(content_setting_image_model->get_tooltip().empty());

  content_settings->OnContentBlocked(CONTENT_SETTINGS_TYPE_IMAGES,
                                     std::string());
  content_setting_image_model->UpdateFromWebContents(web_contents());

  EXPECT_TRUE(content_setting_image_model->is_visible());
  EXPECT_NE(0, content_setting_image_model->get_icon());
  EXPECT_FALSE(content_setting_image_model->get_tooltip().empty());
}

TEST_F(ContentSettingImageModelTest, RPHUpdateFromWebContents) {
  TabSpecificContentSettings::CreateForWebContents(web_contents());
  scoped_ptr<ContentSettingImageModel> content_setting_image_model(
     ContentSettingImageModel::CreateContentSettingImageModel(
         CONTENT_SETTINGS_TYPE_PROTOCOL_HANDLERS));
  content_setting_image_model->UpdateFromWebContents(web_contents());
  EXPECT_FALSE(content_setting_image_model->is_visible());

  TabSpecificContentSettings* content_settings =
      TabSpecificContentSettings::FromWebContents(web_contents());
  content_settings->set_pending_protocol_handler(
      ProtocolHandler::CreateProtocolHandler(
          "mailto", GURL("http://www.google.com/"), ASCIIToUTF16("Handler")));
  content_setting_image_model->UpdateFromWebContents(web_contents());
  EXPECT_TRUE(content_setting_image_model->is_visible());
}

TEST_F(ContentSettingImageModelTest, CookieAccessed) {
  TabSpecificContentSettings::CreateForWebContents(web_contents());
  TabSpecificContentSettings* content_settings =
      TabSpecificContentSettings::FromWebContents(web_contents());
  profile()->GetHostContentSettingsMap()->SetDefaultContentSetting(
      CONTENT_SETTINGS_TYPE_COOKIES, CONTENT_SETTING_BLOCK);
  scoped_ptr<ContentSettingImageModel> content_setting_image_model(
     ContentSettingImageModel::CreateContentSettingImageModel(
         CONTENT_SETTINGS_TYPE_COOKIES));
  EXPECT_FALSE(content_setting_image_model->is_visible());
  EXPECT_EQ(0, content_setting_image_model->get_icon());
  EXPECT_TRUE(content_setting_image_model->get_tooltip().empty());

  net::CookieOptions options;
  content_settings->OnCookieChanged(GURL("http://google.com"),
                                    GURL("http://google.com"),
                                    "A=B",
                                    options,
                                    false);
  content_setting_image_model->UpdateFromWebContents(web_contents());
  EXPECT_TRUE(content_setting_image_model->is_visible());
  EXPECT_NE(0, content_setting_image_model->get_icon());
  EXPECT_FALSE(content_setting_image_model->get_tooltip().empty());
}

// Regression test for http://crbug.com/161854.
TEST_F(ContentSettingImageModelTest, NULLTabSpecificContentSettings) {
  scoped_ptr<ContentSettingImageModel> content_setting_image_model(
     ContentSettingImageModel::CreateContentSettingImageModel(
         CONTENT_SETTINGS_TYPE_IMAGES));
  NotificationForwarder forwarder(content_setting_image_model.get());
  // Should not crash.
  TabSpecificContentSettings::CreateForWebContents(web_contents());
  forwarder.clear();
}

}  // namespace
