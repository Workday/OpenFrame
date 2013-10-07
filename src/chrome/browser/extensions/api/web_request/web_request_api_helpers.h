// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper classes and functions used for the WebRequest API.

#ifndef CHROME_BROWSER_EXTENSIONS_API_WEB_REQUEST_WEB_REQUEST_API_HELPERS_H_
#define CHROME_BROWSER_EXTENSIONS_API_WEB_REQUEST_WEB_REQUEST_API_HELPERS_H_

#include <list>
#include <set>
#include <string>

#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/extensions/extension_warning_set.h"
#include "net/base/auth.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "url/gurl.h"
#include "webkit/common/resource_type.h"

namespace base {
class ListValue;
class Value;
}

namespace extensions {
class Extension;
}

namespace net {
class BoundNetLog;
class URLRequest;
}

namespace extension_web_request_api_helpers {

typedef std::pair<std::string, std::string> ResponseHeader;
typedef std::vector<ResponseHeader> ResponseHeaders;

// Data container for RequestCookies as defined in the declarative WebRequest
// API definition.
struct RequestCookie {
  RequestCookie();
  ~RequestCookie();
  scoped_ptr<std::string> name;
  scoped_ptr<std::string> value;
 private:
  DISALLOW_COPY_AND_ASSIGN(RequestCookie);
};

bool NullableEquals(const RequestCookie* a, const RequestCookie* b);

// Data container for ResponseCookies as defined in the declarative WebRequest
// API definition.
struct ResponseCookie {
  ResponseCookie();
  ~ResponseCookie();
  scoped_ptr<std::string> name;
  scoped_ptr<std::string> value;
  scoped_ptr<std::string> expires;
  scoped_ptr<int> max_age;
  scoped_ptr<std::string> domain;
  scoped_ptr<std::string> path;
  scoped_ptr<bool> secure;
  scoped_ptr<bool> http_only;
 private:
  DISALLOW_COPY_AND_ASSIGN(ResponseCookie);
};

bool NullableEquals(const ResponseCookie* a, const ResponseCookie* b);

// Data container for FilterResponseCookies as defined in the declarative
// WebRequest API definition.
struct FilterResponseCookie : ResponseCookie {
  FilterResponseCookie();
  ~FilterResponseCookie();
  scoped_ptr<int> age_lower_bound;
  scoped_ptr<int> age_upper_bound;
  scoped_ptr<bool> session_cookie;
 private:
  DISALLOW_COPY_AND_ASSIGN(FilterResponseCookie);
};

bool NullableEquals(const FilterResponseCookie* a,
                    const FilterResponseCookie* b);

enum CookieModificationType {
  ADD,
  EDIT,
  REMOVE,
};

struct RequestCookieModification {
  RequestCookieModification();
  ~RequestCookieModification();
  CookieModificationType type;
  // Used for EDIT and REMOVE. NULL for ADD.
  scoped_ptr<RequestCookie> filter;
  // Used for ADD and EDIT. NULL for REMOVE.
  scoped_ptr<RequestCookie> modification;
 private:
  DISALLOW_COPY_AND_ASSIGN(RequestCookieModification);
};

bool NullableEquals(const RequestCookieModification* a,
                    const RequestCookieModification* b);

struct ResponseCookieModification {
  ResponseCookieModification();
  ~ResponseCookieModification();
  CookieModificationType type;
  // Used for EDIT and REMOVE.
  scoped_ptr<FilterResponseCookie> filter;
  // Used for ADD and EDIT.
  scoped_ptr<ResponseCookie> modification;
 private:
  DISALLOW_COPY_AND_ASSIGN(ResponseCookieModification);
};

bool NullableEquals(const ResponseCookieModification* a,
                    const ResponseCookieModification* b);

typedef std::vector<linked_ptr<RequestCookieModification> >
    RequestCookieModifications;
typedef std::vector<linked_ptr<ResponseCookieModification> >
    ResponseCookieModifications;

// Contains the modification an extension wants to perform on an event.
struct EventResponseDelta {
  // ID of the extension that sent this response.
  std::string extension_id;

  // The time that the extension was installed. Used for deciding order of
  // precedence in case multiple extensions respond with conflicting
  // decisions.
  base::Time extension_install_time;

  // Response values. These are mutually exclusive.
  bool cancel;
  GURL new_url;

  // Newly introduced or overridden request headers.
  net::HttpRequestHeaders modified_request_headers;

  // Keys of request headers to be deleted.
  std::vector<std::string> deleted_request_headers;

  // Headers that were added to the response. A modification of a header
  // corresponds to a deletion and subsequent addition of the new header.
  ResponseHeaders added_response_headers;

  // Headers that were deleted from the response.
  ResponseHeaders deleted_response_headers;

  // Authentication Credentials to use.
  scoped_ptr<net::AuthCredentials> auth_credentials;

  // Modifications to cookies in request headers.
  RequestCookieModifications request_cookie_modifications;

  // Modifications to cookies in response headers.
  ResponseCookieModifications response_cookie_modifications;

  // Messages that shall be sent to the background/event/... pages of the
  // extension.
  std::set<std::string> messages_to_extension;

  EventResponseDelta(const std::string& extension_id,
                     const base::Time& extension_install_time);
  ~EventResponseDelta();

  DISALLOW_COPY_AND_ASSIGN(EventResponseDelta);
};

typedef std::list<linked_ptr<EventResponseDelta> > EventResponseDeltas;

// Comparison operator that returns true if the extension that caused
// |a| was installed after the extension that caused |b|.
bool InDecreasingExtensionInstallationTimeOrder(
    const linked_ptr<EventResponseDelta>& a,
    const linked_ptr<EventResponseDelta>& b);

// Converts a string to a list of integers, each in 0..255. Ownership
// of the created list is passed to the caller.
base::ListValue* StringToCharList(const std::string& s);

// Converts a list of integer values between 0 and 255 into a string |*out|.
// Returns true if the conversion was successful.
bool CharListToString(const base::ListValue* list, std::string* out);

// The following functions calculate and return the modifications to requests
// commanded by extension handlers. All functions take the id of the extension
// that commanded a modification, the installation time of this extension (used
// for defining a precedence in conflicting modifications) and whether the
// extension requested to |cancel| the request. Other parameters depend on a
// the signal handler. Ownership of the returned object is passed to the caller.

EventResponseDelta* CalculateOnBeforeRequestDelta(
    const std::string& extension_id,
    const base::Time& extension_install_time,
    bool cancel,
    const GURL& new_url);
EventResponseDelta* CalculateOnBeforeSendHeadersDelta(
    const std::string& extension_id,
    const base::Time& extension_install_time,
    bool cancel,
    net::HttpRequestHeaders* old_headers,
    net::HttpRequestHeaders* new_headers);
EventResponseDelta* CalculateOnHeadersReceivedDelta(
    const std::string& extension_id,
    const base::Time& extension_install_time,
    bool cancel,
    const net::HttpResponseHeaders* old_response_headers,
    ResponseHeaders* new_response_headers);
// Destructively moves the auth credentials from |auth_credentials| to the
// returned EventResponseDelta.
EventResponseDelta* CalculateOnAuthRequiredDelta(
    const std::string& extension_id,
    const base::Time& extension_install_time,
    bool cancel,
    scoped_ptr<net::AuthCredentials>* auth_credentials);

// These functions merge the responses (the |deltas|) of request handlers.
// The |deltas| need to be sorted in decreasing order of precedence of
// extensions. In case extensions had |deltas| that could not be honored, their
// IDs are reported in |conflicting_extensions|. NetLog events that shall be
// reported will be stored in |event_log_entries|.

// Stores in |canceled| whether any extension wanted to cancel the request.
void MergeCancelOfResponses(
    const EventResponseDeltas& deltas,
    bool* canceled,
    const net::BoundNetLog* net_log);
// Stores in |*new_url| the redirect request of the extension with highest
// precedence. Extensions that did not command to redirect the request are
// ignored in this logic.
void MergeOnBeforeRequestResponses(
    const EventResponseDeltas& deltas,
    GURL* new_url,
    extensions::ExtensionWarningSet* conflicting_extensions,
    const net::BoundNetLog* net_log);
// Modifies the "Cookie" header in |request_headers| according to
// |deltas.request_cookie_modifications|. Conflicts are currently ignored
// silently.
void MergeCookiesInOnBeforeSendHeadersResponses(
    const EventResponseDeltas& deltas,
    net::HttpRequestHeaders* request_headers,
    extensions::ExtensionWarningSet* conflicting_extensions,
    const net::BoundNetLog* net_log);
// Modifies the headers in |request_headers| according to |deltas|. Conflicts
// are tried to be resolved.
void MergeOnBeforeSendHeadersResponses(
    const EventResponseDeltas& deltas,
    net::HttpRequestHeaders* request_headers,
    extensions::ExtensionWarningSet* conflicting_extensions,
    const net::BoundNetLog* net_log);
// Modifies the "Set-Cookie" headers in |override_response_headers| according to
// |deltas.response_cookie_modifications|. If |override_response_headers| is
// NULL, a copy of |original_response_headers| is created. Conflicts are
// currently ignored silently.
void MergeCookiesInOnHeadersReceivedResponses(
    const EventResponseDeltas& deltas,
    const net::HttpResponseHeaders* original_response_headers,
    scoped_refptr<net::HttpResponseHeaders>* override_response_headers,
    extensions::ExtensionWarningSet* conflicting_extensions,
    const net::BoundNetLog* net_log);
// Stores a copy of |original_response_header| into |override_response_headers|
// that is modified according to |deltas|. If |deltas| does not instruct to
// modify the response headers, |override_response_headers| remains empty.
void MergeOnHeadersReceivedResponses(
    const EventResponseDeltas& deltas,
    const net::HttpResponseHeaders* original_response_headers,
    scoped_refptr<net::HttpResponseHeaders>* override_response_headers,
    extensions::ExtensionWarningSet* conflicting_extensions,
    const net::BoundNetLog* net_log);
// Merge the responses of blocked onAuthRequired handlers. The first
// registered listener that supplies authentication credentials in a response,
// if any, will have its authentication credentials used. |request| must be
// non-NULL, and contain |deltas| that are sorted in decreasing order of
// precedence.
// Returns whether authentication credentials are set.
bool MergeOnAuthRequiredResponses(
    const EventResponseDeltas& deltas,
    net::AuthCredentials* auth_credentials,
    extensions::ExtensionWarningSet* conflicting_extensions,
    const net::BoundNetLog* net_log);

// Returns whether |type| is a ResourceType that is handled by the web request
// API.
bool IsRelevantResourceType(ResourceType::Type type);

// Returns a string representation of |type| or |other| if |type| is not handled
// by the web request API.
const char* ResourceTypeToString(ResourceType::Type type);

// Stores a |ResourceType::Type| representation in |type| if |type_str| is
// a resource type handled by the web request API. Returns true in case of
// success.
bool ParseResourceType(const std::string& type_str,
                       ResourceType::Type* type);

// Triggers clearing each renderer's in-memory cache the next time it navigates.
void ClearCacheOnNavigation();

// Tells renderer processes that the web request or declarative web request
// API has been used by |extension| in profile |profile_id| to collect
// UMA statistics on Page Load Times. Needs to be called on the UI thread.
void NotifyWebRequestAPIUsed(
    void* profile_id,
    scoped_refptr<const extensions::Extension> extension);

}  // namespace extension_web_request_api_helpers

#endif  // CHROME_BROWSER_EXTENSIONS_API_WEB_REQUEST_WEB_REQUEST_API_HELPERS_H_
