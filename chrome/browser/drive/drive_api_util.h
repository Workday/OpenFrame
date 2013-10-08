// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DRIVE_DRIVE_API_UTIL_H_
#define CHROME_BROWSER_DRIVE_DRIVE_API_UTIL_H_

#include <string>

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/google_apis/drive_common_callbacks.h"
#include "chrome/browser/google_apis/gdata_errorcode.h"

class GURL;

namespace base {
class Value;
}  // namespace base

namespace drive {
namespace util {

// Returns true if Drive v2 API is enabled via commandline switch.
bool IsDriveV2ApiEnabled();

// Escapes ' to \' in the |str|. This is designed to use for string value of
// search parameter on Drive API v2.
// See also: https://developers.google.com/drive/search-parameters
std::string EscapeQueryStringValue(const std::string& str);

// Parses the query, and builds a search query for Drive API v2.
// This only supports:
//   Regular query (e.g. dog => fullText contains 'dog')
//   Conjunctions
//     (e.g. dog cat => fullText contains 'dog' and fullText contains 'cat')
//   Exclusion query (e.g. -cat => not fullText contains 'cat').
//   Quoted query (e.g. "dog cat" => fullText contains 'dog cat').
// See also: https://developers.google.com/drive/search-parameters
std::string TranslateQuery(const std::string& original_query);

// Extracts resource_id out of edit url.
std::string ExtractResourceIdFromUrl(const GURL& url);

// If |resource_id| is in the old resource ID format used by WAPI, converts it
// into the new format.
std::string CanonicalizeResourceId(const std::string& resource_id);

// Note: Following constants and a function are used to support GetShareUrl on
// Drive API v2. Unfortunately, there is no support on Drive API v2, so we need
// to fall back to GData WAPI for the GetShareUrl. Thus, these are shared by
// both GDataWapiService and DriveAPIService.
// TODO(hidehiko): Remove these from here, when Drive API v2 supports
// GetShareUrl.

// OAuth2 scopes for the GData WAPI.
extern const char kDocsListScope[];
extern const char kDriveAppsScope[];

// Extracts an url to the sharing dialog and returns it via |callback|. If
// the share url doesn't exist, then an empty url is returned.
void ParseShareUrlAndRun(const google_apis::GetShareUrlCallback& callback,
                         google_apis::GDataErrorCode error,
                         scoped_ptr<base::Value> value);

}  // namespace util
}  // namespace drive

#endif  // CHROME_BROWSER_DRIVE_DRIVE_API_UTIL_H_
