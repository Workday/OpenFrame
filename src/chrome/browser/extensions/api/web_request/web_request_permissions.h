// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_WEB_REQUEST_WEB_REQUEST_PERMISSIONS_H_
#define CHROME_BROWSER_EXTENSIONS_API_WEB_REQUEST_WEB_REQUEST_PERMISSIONS_H_

#include <map>
#include <string>

#include "base/basictypes.h"

class ExtensionInfoMap;
class GURL;

namespace net {
class URLRequest;
}

// This class is used to test whether extensions may modify web requests.
class WebRequestPermissions {
 public:
  // Different host permission checking modes for CanExtensionAccessURL.
  enum HostPermissionsCheck {
    DO_NOT_CHECK_HOST = 0,    // No check.
    REQUIRE_HOST_PERMISSION,  // Permission needed for given URL.
    REQUIRE_ALL_URLS          // Permission needed for <all_urls>.
  };

  // Returns true if the request shall not be reported to extensions.
  static bool HideRequest(const ExtensionInfoMap* extension_info_map,
                          const net::URLRequest* request);

  // |host_permission_check| controls how permissions are checked with regard to
  // |url|.
  static bool CanExtensionAccessURL(
      const ExtensionInfoMap* extension_info_map,
      const std::string& extension_id,
      const GURL& url,
      bool crosses_incognito,
      HostPermissionsCheck host_permissions_check);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(WebRequestPermissions);
};

#endif  // CHROME_BROWSER_EXTENSIONS_API_WEB_REQUEST_WEB_REQUEST_PERMISSIONS_H_
