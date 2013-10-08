// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_SUPPORT_WEBURL_LOADER_MOCK_FACTORY_H_
#define WEBKIT_SUPPORT_WEBURL_LOADER_MOCK_FACTORY_H_

#include <map>

#include "base/files/file_path.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "third_party/WebKit/public/platform/WebURLRequest.h"
#include "third_party/WebKit/public/platform/WebURLResponse.h"

namespace WebKit {
class WebData;
struct WebURLError;
class WebURLLoader;
}

class WebURLLoaderMock;

// A factory that creates WebURLLoaderMock to simulate resource loading in
// tests.
// You register files for specific URLs, the content of the file is then served
// when these URLs are loaded.
// In order to serve the asynchronous requests, you need to invoke
// ServeAsynchronousRequest.
class WebURLLoaderMockFactory {
 public:
  WebURLLoaderMockFactory();
  virtual ~WebURLLoaderMockFactory();

  // Called by TestWebKitPlatformSupport to create a WebURLLoader.
  // Non-mocked request are forwarded to |default_loader| which should not be
  // NULL.
  virtual WebKit::WebURLLoader* CreateURLLoader(
      WebKit::WebURLLoader* default_loader);

  // Registers a response and the contents to be served when the specified URL
  // is loaded.
  void RegisterURL(const WebKit::WebURL& url,
                   const WebKit::WebURLResponse& response,
                   const WebKit::WebString& filePath);

  // Registers an error to be served when the specified URL is requested.
  void RegisterErrorURL(const WebKit::WebURL& url,
                        const WebKit::WebURLResponse& response,
                        const WebKit::WebURLError& error);

  // Unregisters |url| so it will no longer be mocked.
  void UnregisterURL(const WebKit::WebURL& url);

  // Unregister all URLs so no URL will be mocked anymore.
  void UnregisterAllURLs();

  // Serves all the pending asynchronous requests.
  void ServeAsynchronousRequests();

  // Returns the last request handled by |ServeAsynchronousRequests()|.
  WebKit::WebURLRequest GetLastHandledAsynchronousRequest();

  // Returns true if |url| was registered for being mocked.
  bool IsMockedURL(const WebKit::WebURL& url);

  // Called by the loader to load a resource.
  void LoadSynchronously(const WebKit::WebURLRequest& request,
                         WebKit::WebURLResponse* response,
                         WebKit::WebURLError* error,
                         WebKit::WebData* data);
  void LoadAsynchronouly(const WebKit::WebURLRequest& request,
                         WebURLLoaderMock* loader);

  // Removes the loader from the list of pending loaders.
  void CancelLoad(WebURLLoaderMock* loader);

 private:
  struct ResponseInfo;

  // Loads the specified request and populates the response, error and data
  // accordingly.
  void LoadRequest(const WebKit::WebURLRequest& request,
                   WebKit::WebURLResponse* response,
                   WebKit::WebURLError* error,
                   WebKit::WebData* data);

  // Checks if the loader is pending. Otherwise, it may have been deleted.
  bool IsPending(WebURLLoaderMock* loader);

  // Reads |m_filePath| and puts its content in |data|.
  // Returns true if it successfully read the file.
  static bool ReadFile(const base::FilePath& file_path, WebKit::WebData* data);

  // The loaders that have not being served data yet.
  typedef std::map<WebURLLoaderMock*, WebKit::WebURLRequest> LoaderToRequestMap;
  LoaderToRequestMap pending_loaders_;

  typedef std::map<GURL, WebKit::WebURLError> URLToErrorMap;
  URLToErrorMap url_to_error_info_;

  // Table of the registered URLs and the responses that they should receive.
  typedef std::map<GURL, ResponseInfo> URLToResponseMap;
  URLToResponseMap url_to_reponse_info_;

  WebKit::WebURLRequest last_handled_asynchronous_request_;

  DISALLOW_COPY_AND_ASSIGN(WebURLLoaderMockFactory);
};

#endif  // WEBKIT_SUPPORT_WEBURL_LOADER_MOCK_FACTORY_H_

