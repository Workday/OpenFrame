// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/banners/app_banner_data_fetcher_android.h"

#include "chrome/browser/android/banners/app_banner_infobar_delegate_android.h"
#include "chrome/browser/android/shortcut_helper.h"
#include "chrome/browser/banners/app_banner_metrics.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/manifest/manifest_icon_selector.h"
#include "chrome/browser/ui/android/infobars/app_banner_infobar_android.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/screen.h"

namespace banners {

AppBannerDataFetcherAndroid::AppBannerDataFetcherAndroid(
    content::WebContents* web_contents,
    base::WeakPtr<Delegate> weak_delegate,
    int ideal_icon_size_in_dp,
    int minimum_icon_size_in_dp,
    int ideal_splash_image_size_in_dp,
    int minimum_splash_image_size_in_dp)
    : AppBannerDataFetcher(web_contents,
                           weak_delegate,
                           ideal_icon_size_in_dp,
                           minimum_icon_size_in_dp),
      ideal_splash_image_size_in_dp_(ideal_splash_image_size_in_dp),
      minimum_splash_image_size_in_dp_(minimum_splash_image_size_in_dp) {
}

AppBannerDataFetcherAndroid::~AppBannerDataFetcherAndroid() {
}

std::string AppBannerDataFetcherAndroid::GetBannerType() {
  return native_app_data_.is_null()
      ? AppBannerDataFetcher::GetBannerType() : "android";
}

bool AppBannerDataFetcherAndroid::ContinueFetching(
    const base::string16& app_title,
    const std::string& app_package,
    const base::android::JavaRef<jobject>& app_data,
    const GURL& image_url) {
  set_app_title(app_title);
  native_app_package_ = app_package;
  native_app_data_.Reset(app_data);
  return FetchAppIcon(GetWebContents(), image_url);
}

std::string AppBannerDataFetcherAndroid::GetAppIdentifier() {
  return native_app_data_.is_null()
      ? AppBannerDataFetcher::GetAppIdentifier() : native_app_package_;
}

void AppBannerDataFetcherAndroid::FetchWebappSplashScreenImage(
    const std::string& webapp_id) {
  content::WebContents* web_contents = GetWebContents();
  DCHECK(web_contents);

  GURL image_url = ManifestIconSelector::FindBestMatchingIcon(
      web_app_data().icons,
      ideal_splash_image_size_in_dp_,
      minimum_splash_image_size_in_dp_,
      gfx::Screen::GetScreenFor(web_contents->GetNativeView()));

  ShortcutHelper::FetchSplashScreenImage(
      web_contents,
      image_url,
      ideal_splash_image_size_in_dp_,
      minimum_splash_image_size_in_dp_,
      webapp_id);
}

void AppBannerDataFetcherAndroid::ShowBanner(const SkBitmap* icon,
                                             const base::string16& title,
                                             const std::string& referrer) {
  content::WebContents* web_contents = GetWebContents();
  DCHECK(web_contents);

  infobars::InfoBar* infobar = nullptr;
  if (native_app_data_.is_null()) {
    scoped_ptr<AppBannerInfoBarDelegateAndroid> delegate(
        new AppBannerInfoBarDelegateAndroid(
            event_request_id(), this, title, new SkBitmap(*icon),
            web_app_data()));

    infobar =
        new AppBannerInfoBarAndroid(delegate.Pass(), web_app_data().start_url);
    if (infobar) {
      RecordDidShowBanner("AppBanner.WebApp.Shown");
      TrackDisplayEvent(DISPLAY_EVENT_WEB_APP_BANNER_CREATED);
    }
  } else {
    scoped_ptr<AppBannerInfoBarDelegateAndroid> delegate(
        new AppBannerInfoBarDelegateAndroid(
            event_request_id(), title, new SkBitmap(*icon), native_app_data_,
            native_app_package_, referrer));
    infobar = new AppBannerInfoBarAndroid(delegate.Pass(), native_app_data_);
    if (infobar) {
      RecordDidShowBanner("AppBanner.NativeApp.Shown");
      TrackDisplayEvent(DISPLAY_EVENT_NATIVE_APP_BANNER_CREATED);
    }
  }
  InfoBarService::FromWebContents(web_contents)
      ->AddInfoBar(make_scoped_ptr(infobar));
}

}  // namespace banners
