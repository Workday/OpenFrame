// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_RESOURCE_CONTEXT_H_
#define CONTENT_PUBLIC_BROWSER_RESOURCE_CONTEXT_H_

#include "base/basictypes.h"
#include "base/supports_user_data.h"
#include "build/build_config.h"
#include "content/common/content_export.h"

class GURL;

namespace appcache {
class AppCacheService;
}

namespace net {
class HostResolver;
class URLRequestContext;
}

namespace content {

// ResourceContext contains the relevant context information required for
// resource loading. It lives on the IO thread, although it is constructed on
// the UI thread. It must be destructed on the IO thread.
class CONTENT_EXPORT ResourceContext : public base::SupportsUserData {
 public:
#if defined(OS_IOS)
  virtual ~ResourceContext() {}
#else
  ResourceContext();
  virtual ~ResourceContext();
#endif
  virtual net::HostResolver* GetHostResolver() = 0;

  // DEPRECATED: This is no longer a valid given isolated apps/sites and
  // storage partitioning. This getter returns the default context associated
  // with a BrowsingContext.
  virtual net::URLRequestContext* GetRequestContext() = 0;

  // Returns true if microphone access is allowed for |origin|. Used to
  // determine what level of authorization is given to |origin| to access
  // resource metadata.
  virtual bool AllowMicAccess(const GURL& origin) = 0;

  // Returns true if web camera access is allowed for |origin|. Used to
  // determine what level of authorization is given to |origin| to access
  // resource metadata.
  virtual bool AllowCameraAccess(const GURL& origin) = 0;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_RESOURCE_CONTEXT_H_
