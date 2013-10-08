// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_RENDERER_WEBCOOKIEJAR_IMPL_H_
#define CONTENT_RENDERER_RENDERER_WEBCOOKIEJAR_IMPL_H_

// TODO(darin): WebCookieJar.h is missing a WebString.h include!
#include "third_party/WebKit/public/platform/WebCookieJar.h"
#include "third_party/WebKit/public/platform/WebString.h"

namespace content {
class RenderViewImpl;

class RendererWebCookieJarImpl : public WebKit::WebCookieJar {
 public:
  explicit RendererWebCookieJarImpl(RenderViewImpl* sender)
      : sender_(sender) {
  }
  virtual ~RendererWebCookieJarImpl() {}

 private:
  // WebKit::WebCookieJar methods:
  virtual void setCookie(
      const WebKit::WebURL& url, const WebKit::WebURL& first_party_for_cookies,
      const WebKit::WebString& value);
  virtual WebKit::WebString cookies(
      const WebKit::WebURL& url, const WebKit::WebURL& first_party_for_cookies);
  virtual WebKit::WebString cookieRequestHeaderFieldValue(
      const WebKit::WebURL& url, const WebKit::WebURL& first_party_for_cookies);
  virtual void rawCookies(
      const WebKit::WebURL& url, const WebKit::WebURL& first_party_for_cookies,
      WebKit::WebVector<WebKit::WebCookie>& cookies);
  virtual void deleteCookie(
      const WebKit::WebURL& url, const WebKit::WebString& cookie_name);
  virtual bool cookiesEnabled(
      const WebKit::WebURL& url, const WebKit::WebURL& first_party_for_cookies);

  RenderViewImpl* sender_;
};

}  // namespace content

#endif  // CONTENT_RENDERER_RENDERER_WEBCOOKIEJAR_IMPL_H_
