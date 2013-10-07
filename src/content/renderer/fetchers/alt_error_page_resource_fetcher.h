// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_FETCHERS_ALT_ERROR_PAGE_RESOURCE_FETCHER_H_
#define CONTENT_RENDERER_FETCHERS_ALT_ERROR_PAGE_RESOURCE_FETCHER_H_

#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "third_party/WebKit/public/platform/WebURLError.h"
#include "third_party/WebKit/public/platform/WebURLRequest.h"
#include "url/gurl.h"

namespace WebKit {
class WebFrame;
class WebURLResponse;
}

namespace content {
class ResourceFetcherWithTimeout;

// Used for downloading alternate dns error pages. Once downloading is done
// (or fails), the webview delegate is notified.
class AltErrorPageResourceFetcher {
 public:
  // This will be called when the alternative error page has been fetched,
  // successfully or not.  If there is a failure, the third parameter (the
  // data) will be empty.
  typedef base::Callback<void(WebKit::WebFrame*,
                              const WebKit::WebURLRequest&,
                              const WebKit::WebURLError&,
                              const std::string&)> Callback;

  AltErrorPageResourceFetcher(
      const GURL& url,
      WebKit::WebFrame* frame,
      const WebKit::WebURLRequest& original_request,
      const WebKit::WebURLError& original_error,
      const Callback& callback);
  ~AltErrorPageResourceFetcher();

  // Stop any pending loads.
  void Cancel();

 private:
  void OnURLFetchComplete(const WebKit::WebURLResponse& response,
                          const std::string& data);

  // Does the actual fetching.
  scoped_ptr<ResourceFetcherWithTimeout> fetcher_;

  WebKit::WebFrame* frame_;
  Callback callback_;

  // The original request.  If loading the alternate error page fails, it's
  // needed to generate the error page.
  WebKit::WebURLRequest original_request_;

  // The error associated with this load.  If there's an error talking with the
  // alt error page server, we need this to complete the original load.
  WebKit::WebURLError original_error_;

  DISALLOW_COPY_AND_ASSIGN(AltErrorPageResourceFetcher);
};

}  // namespace content

#endif  // CONTENT_RENDERER_FETCHERS_ALT_ERROR_PAGE_RESOURCE_FETCHER_H_
