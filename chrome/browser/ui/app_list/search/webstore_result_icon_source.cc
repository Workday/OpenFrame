// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/search/webstore_result_icon_source.h"

#include <string>

#include "content/public/browser/browser_thread.h"
#include "grit/theme_resources.h"
#include "net/base/load_flags.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_request_status.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image_skia_operations.h"

using content::BrowserThread;

namespace app_list {

WebstoreResultIconSource::WebstoreResultIconSource(
    const IconLoadedCallback& icon_loaded_callback,
    net::URLRequestContextGetter* context_getter,
    const GURL& icon_url,
    int icon_size)
    : icon_loaded_callback_(icon_loaded_callback),
      context_getter_(context_getter),
      icon_url_(icon_url),
      icon_size_(icon_size),
      icon_fetch_attempted_(false) {
  DCHECK(!icon_loaded_callback_.is_null());
}

WebstoreResultIconSource::~WebstoreResultIconSource() {
  if (image_decoder_.get())
    image_decoder_->set_delegate(NULL);
}

void WebstoreResultIconSource::StartIconFetch() {
  icon_fetch_attempted_ = true;

  icon_fetcher_.reset(
      net::URLFetcher::Create(icon_url_, net::URLFetcher::GET, this));
  icon_fetcher_->SetRequestContext(context_getter_);
  icon_fetcher_->SetLoadFlags(net::LOAD_DO_NOT_SAVE_COOKIES);
  icon_fetcher_->Start();
}

gfx::ImageSkiaRep WebstoreResultIconSource::CreateBadgedIcon(
    ui::ScaleFactor scale_factor) {
  gfx::Canvas canvas(gfx::Size(icon_size_, icon_size_), scale_factor, false);

  canvas.DrawImageInt(icon_, 0, 0);

  const gfx::ImageSkia& badge = *ui::ResourceBundle::GetSharedInstance().
       GetImageSkiaNamed(IDR_WEBSTORE_ICON_16);
  canvas.DrawImageInt(
      badge, icon_.width() - badge.width(), icon_.height() - badge.height());

  return canvas.ExtractImageRep();
}

gfx::ImageSkiaRep WebstoreResultIconSource::GetImageForScale(
    ui::ScaleFactor scale_factor) {
  if (!icon_fetch_attempted_)
    StartIconFetch();

  if (!icon_.isNull())
    return CreateBadgedIcon(scale_factor);

  return ui::ResourceBundle::GetSharedInstance()
      .GetImageSkiaNamed(IDR_WEBSTORE_ICON_32)->GetRepresentation(scale_factor);
}

void WebstoreResultIconSource::OnURLFetchComplete(
    const net::URLFetcher* source) {
  CHECK_EQ(icon_fetcher_.get(), source);

  scoped_ptr<net::URLFetcher> fetcher(icon_fetcher_.Pass());

  if (!fetcher->GetStatus().is_success() ||
      fetcher->GetResponseCode() != 200) {
    return;
  }

  std::string unsafe_icon_data;
  fetcher->GetResponseAsString(&unsafe_icon_data);

  image_decoder_ =
      new ImageDecoder(this, unsafe_icon_data, ImageDecoder::DEFAULT_CODEC);
  image_decoder_->Start(
      BrowserThread::GetMessageLoopProxyForThread(BrowserThread::UI));
}

void WebstoreResultIconSource::OnImageDecoded(const ImageDecoder* decoder,
                                              const SkBitmap& decoded_image) {
  icon_ = gfx::ImageSkiaOperations::CreateResizedImage(
      gfx::ImageSkia::CreateFrom1xBitmap(decoded_image),
      skia::ImageOperations::RESIZE_BEST,
      gfx::Size(icon_size_, icon_size_));
  icon_loaded_callback_.Run();
}

void WebstoreResultIconSource::OnDecodeImageFailed(
    const ImageDecoder* decoder) {
  // Failed to decode image. Do nothing and just use web store icon.
}

}  // namespace app_list
