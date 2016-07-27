// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SUGGESTIONS_IMAGE_FETCHER_H_
#define COMPONENTS_SUGGESTIONS_IMAGE_FETCHER_H_

#include "base/callback.h"
#include "components/suggestions/image_fetcher_delegate.h"
#include "url/gurl.h"

class SkBitmap;

namespace suggestions {

// A class used to fetch server images.
class ImageFetcher {
 public:
  ImageFetcher() {}
  virtual ~ImageFetcher() {}

  virtual void SetImageFetcherDelegate(ImageFetcherDelegate* delegate) = 0;

  virtual void StartOrQueueNetworkRequest(
      const GURL& url, const GURL& image_url,
      base::Callback<void(const GURL&, const SkBitmap*)> callback) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ImageFetcher);
};

}  // namespace suggestions

#endif  // COMPONENTS_SUGGESTIONS_IMAGE_FETCHER_H_
