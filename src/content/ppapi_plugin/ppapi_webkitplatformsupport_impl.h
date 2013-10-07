// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PPAPI_PLUGIN_PPAPI_WEBKITPLATFORMSUPPORT_IMPL_H_
#define CONTENT_PPAPI_PLUGIN_PPAPI_WEBKITPLATFORMSUPPORT_IMPL_H_

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "content/child/webkitplatformsupport_impl.h"

namespace content {

class PpapiWebKitPlatformSupportImpl : public WebKitPlatformSupportImpl {
 public:
  PpapiWebKitPlatformSupportImpl();
  virtual ~PpapiWebKitPlatformSupportImpl();

  // WebKitPlatformSupport methods:
  virtual WebKit::WebClipboard* clipboard();
  virtual WebKit::WebMimeRegistry* mimeRegistry();
  virtual WebKit::WebFileUtilities* fileUtilities();
  virtual WebKit::WebSandboxSupport* sandboxSupport();
  virtual bool sandboxEnabled();
  virtual unsigned long long visitedLinkHash(const char* canonicalURL,
                                             size_t length);
  virtual bool isLinkVisited(unsigned long long linkHash);
  virtual WebKit::WebMessagePortChannel* createMessagePortChannel();
  virtual void setCookies(const WebKit::WebURL& url,
                          const WebKit::WebURL& first_party_for_cookies,
                          const WebKit::WebString& value);
  virtual WebKit::WebString cookies(
      const WebKit::WebURL& url,
      const WebKit::WebURL& first_party_for_cookies);
  virtual WebKit::WebString defaultLocale();
  virtual WebKit::WebThemeEngine* themeEngine();
  virtual WebKit::WebURLLoader* createURLLoader();
  virtual WebKit::WebSocketStreamHandle* createSocketStreamHandle();
  virtual void getPluginList(bool refresh, WebKit::WebPluginListBuilder*);
  virtual WebKit::WebData loadResource(const char* name);
  virtual WebKit::WebStorageNamespace* createLocalStorageNamespace();
  virtual void dispatchStorageEvent(const WebKit::WebString& key,
      const WebKit::WebString& oldValue, const WebKit::WebString& newValue,
      const WebKit::WebString& origin, const WebKit::WebURL& url,
      bool isLocalStorage);
  virtual int databaseDeleteFile(const WebKit::WebString& vfs_file_name,
                                 bool sync_dir);

 private:
  class SandboxSupport;
  scoped_ptr<SandboxSupport> sandbox_support_;

  DISALLOW_COPY_AND_ASSIGN(PpapiWebKitPlatformSupportImpl);
};

}  // namespace content

#endif  // CONTENT_PPAPI_PLUGIN_PPAPI_WEBKITPLATFORMSUPPORT_IMPL_H_
