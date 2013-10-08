// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ANDROID_WEBVIEW_BROWSER_AW_COOKIE_ACCESS_POLICY_H_
#define ANDROID_WEBVIEW_BROWSER_AW_COOKIE_ACCESS_POLICY_H_

#include "base/basictypes.h"
#include "base/lazy_instance.h"
#include "base/synchronization/lock.h"
#include "net/cookies/canonical_cookie.h"

namespace content {
class ResourceContext;
}

namespace net {
class CookieOptions;
class URLRequest;
}

class GURL;

namespace android_webview {

// Manages the cookie access (both setting and getting) policy for WebView.
class AwCookieAccessPolicy {
 public:
  static AwCookieAccessPolicy* GetInstance();

  // These manage the global access state shared across requests regardless of
  // source (i.e. network or JavaScript).
  bool GetGlobalAllowAccess();
  void SetGlobalAllowAccess(bool allow);

  // These are the functions called when operating over cookies from the
  // network. See NetworkDelegate for further descriptions.
  bool OnCanGetCookies(const net::URLRequest& request,
                       const net::CookieList& cookie_list);
  bool OnCanSetCookie(const net::URLRequest& request,
                      const std::string& cookie_line,
                      net::CookieOptions* options);

  // These are the functions called when operating over cookies from the
  // renderer. See ContentBrowserClient for further descriptions.
  bool AllowGetCookie(const GURL& url,
                      const GURL& first_party,
                      const net::CookieList& cookie_list,
                      content::ResourceContext* context,
                      int render_process_id,
                      int render_view_id);
  bool AllowSetCookie(const GURL& url,
                      const GURL& first_party,
                      const std::string& cookie_line,
                      content::ResourceContext* context,
                      int render_process_id,
                      int render_view_id,
                      net::CookieOptions* options);

 private:
  friend struct base::DefaultLazyInstanceTraits<AwCookieAccessPolicy>;

  AwCookieAccessPolicy();
  ~AwCookieAccessPolicy();
  bool allow_access_;
  base::Lock lock_;

  DISALLOW_COPY_AND_ASSIGN(AwCookieAccessPolicy);
};

}  // namespace android_webview

#endif  // ANDROID_WEBVIEW_BROWSER_AW_COOKIE_ACCESS_POLICY_H_
