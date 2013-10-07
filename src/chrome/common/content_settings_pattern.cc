// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/content_settings_pattern.h"

#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "chrome/common/content_settings_pattern_parser.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "extensions/common/constants.h"
#include "ipc/ipc_message_utils.h"
#include "net/base/dns_util.h"
#include "net/base/net_util.h"
#include "url/gurl.h"
#include "url/url_canon.h"

namespace {

std::string GetDefaultPort(const std::string& scheme) {
  if (scheme == chrome::kHttpScheme)
    return "80";
  if (scheme == chrome::kHttpsScheme)
    return "443";
  return std::string();
}

// Returns true if |sub_domain| is a sub domain or equls |domain|.  E.g.
// "mail.google.com" is a sub domain of "google.com" but "evilhost.com" is not a
// subdomain of "host.com".
bool IsSubDomainOrEqual(const std::string& sub_domain,
                        const std::string& domain) {
  // The empty string serves as wildcard. Each domain is a subdomain of the
  // wildcard.
  if (domain.empty())
    return true;
  const size_t match = sub_domain.rfind(domain);
  if (match == std::string::npos ||
      (match > 0 && sub_domain[match - 1] != '.') ||
      (match + domain.length() != sub_domain.length())) {
    return false;
  }
  return true;
}

// Compares two domain names.
int CompareDomainNames(const std::string& str1, const std::string& str2) {
  std::vector<std::string> domain_name1;
  std::vector<std::string> domain_name2;

  base::SplitString(str1, '.', &domain_name1);
  base::SplitString(str2, '.', &domain_name2);

  int i1 = domain_name1.size() - 1;
  int i2 = domain_name2.size() - 1;
  int rv;
  while (i1 >= 0 && i2 >= 0) {
    // domain names are stored in puny code. So it's fine to use the compare
    // method.
    rv = domain_name1[i1].compare(domain_name2[i2]);
    if (rv != 0)
      return rv;
    --i1;
    --i2;
  }

  if (i1 > i2)
    return 1;

  if (i1 < i2)
    return -1;

  // The domain names are identical.
  return 0;
}

typedef ContentSettingsPattern::BuilderInterface BuilderInterface;

}  // namespace

// ////////////////////////////////////////////////////////////////////////////
// ContentSettingsPattern::Builder
//
ContentSettingsPattern::Builder::Builder(bool use_legacy_validate)
    : is_valid_(true),
      use_legacy_validate_(use_legacy_validate) {}

ContentSettingsPattern::Builder::~Builder() {}

BuilderInterface* ContentSettingsPattern::Builder::WithPort(
    const std::string& port) {
  parts_.port = port;
  parts_.is_port_wildcard = false;
  return this;
}

BuilderInterface* ContentSettingsPattern::Builder::WithPortWildcard() {
  parts_.port = "";
  parts_.is_port_wildcard = true;
  return this;
}

BuilderInterface* ContentSettingsPattern::Builder::WithHost(
    const std::string& host) {
  parts_.host = host;
  return this;
}

BuilderInterface* ContentSettingsPattern::Builder::WithDomainWildcard() {
  parts_.has_domain_wildcard = true;
  return this;
}

BuilderInterface* ContentSettingsPattern::Builder::WithScheme(
    const std::string& scheme) {
  parts_.scheme = scheme;
  parts_.is_scheme_wildcard = false;
  return this;
}

BuilderInterface* ContentSettingsPattern::Builder::WithSchemeWildcard() {
  parts_.scheme = "";
  parts_.is_scheme_wildcard = true;
  return this;
}

BuilderInterface* ContentSettingsPattern::Builder::WithPath(
    const std::string& path) {
  parts_.path = path;
  parts_.is_path_wildcard = false;
  return this;
}

BuilderInterface* ContentSettingsPattern::Builder::WithPathWildcard() {
  parts_.path = "";
  parts_.is_path_wildcard = true;
  return this;
}

BuilderInterface* ContentSettingsPattern::Builder::Invalid() {
  is_valid_ = false;
  return this;
}

ContentSettingsPattern ContentSettingsPattern::Builder::Build() {
  if (!is_valid_)
    return ContentSettingsPattern();
  if (!Canonicalize(&parts_))
    return ContentSettingsPattern();
  if (use_legacy_validate_) {
    is_valid_ = LegacyValidate(parts_);
  } else {
    is_valid_ = Validate(parts_);
  }
  if (!is_valid_)
    return ContentSettingsPattern();

  // A pattern is invalid if canonicalization is not idempotent.
  // This check is here because it should be checked no matter
  // use_legacy_validate_ is.
  PatternParts parts(parts_);
  if (!Canonicalize(&parts))
    return ContentSettingsPattern();
  if (ContentSettingsPattern(parts_, true) !=
      ContentSettingsPattern(parts, true)) {
    return ContentSettingsPattern();
  }

  return ContentSettingsPattern(parts_, is_valid_);
}

// static
bool ContentSettingsPattern::Builder::Canonicalize(PatternParts* parts) {
  // Canonicalize the scheme part.
  const std::string scheme(StringToLowerASCII(parts->scheme));
  parts->scheme = scheme;

  if (parts->scheme == std::string(chrome::kFileScheme) &&
      !parts->is_path_wildcard) {
      GURL url(std::string(chrome::kFileScheme) +
               std::string(content::kStandardSchemeSeparator) + parts->path);
      parts->path = url.path();
  }

  // Canonicalize the host part.
  const std::string host(parts->host);
  url_canon::CanonHostInfo host_info;
  std::string canonicalized_host(net::CanonicalizeHost(host, &host_info));
  if (host_info.IsIPAddress() && parts->has_domain_wildcard)
    return false;
  canonicalized_host = net::TrimEndingDot(canonicalized_host);

  parts->host = "";
  if ((host.find('*') == std::string::npos) &&
      !canonicalized_host.empty()) {
    // Valid host.
    parts->host += canonicalized_host;
  }
  return true;
}

// static
bool ContentSettingsPattern::Builder::Validate(const PatternParts& parts) {
  // Sanity checks first: {scheme, port} wildcards imply empty {scheme, port}.
  if ((parts.is_scheme_wildcard && !parts.scheme.empty()) ||
      (parts.is_port_wildcard && !parts.port.empty())) {
    NOTREACHED();
    return false;
  }

  // file:// URL patterns have an empty host and port.
  if (parts.scheme == std::string(chrome::kFileScheme)) {
    if (parts.has_domain_wildcard || !parts.host.empty() || !parts.port.empty())
      return false;
    if (parts.is_path_wildcard)
      return parts.path.empty();
    return (!parts.path.empty() &&
            parts.path != "/" &&
            parts.path.find("*") == std::string::npos);
  }

  // If the pattern is for an extension URL test if it is valid.
  if (parts.scheme == std::string(extensions::kExtensionScheme) &&
      parts.port.empty() &&
      !parts.is_port_wildcard) {
    return true;
  }

  // Non-file patterns are invalid if either the scheme, host or port part is
  // empty.
  if ((parts.scheme.empty() && !parts.is_scheme_wildcard) ||
      (parts.host.empty() && !parts.has_domain_wildcard) ||
      (parts.port.empty() && !parts.is_port_wildcard)) {
    return false;
  }

  if (parts.host.find("*") != std::string::npos)
    return false;

  // Test if the scheme is supported or a wildcard.
  if (!parts.is_scheme_wildcard &&
      parts.scheme != std::string(chrome::kHttpScheme) &&
      parts.scheme != std::string(chrome::kHttpsScheme)) {
    return false;
  }
  return true;
}

// static
bool ContentSettingsPattern::Builder::LegacyValidate(
    const PatternParts& parts) {
  // If the pattern is for a "file-pattern" test if it is valid.
  if (parts.scheme == std::string(chrome::kFileScheme) &&
      !parts.is_scheme_wildcard &&
      parts.host.empty() &&
      parts.port.empty())
    return true;

  // If the pattern is for an extension URL test if it is valid.
  if (parts.scheme == std::string(extensions::kExtensionScheme) &&
      !parts.is_scheme_wildcard &&
      !parts.host.empty() &&
      !parts.has_domain_wildcard &&
      parts.port.empty() &&
      !parts.is_port_wildcard)
    return true;

  // Non-file patterns are invalid if either the scheme, host or port part is
  // empty.
  if ((!parts.is_scheme_wildcard) ||
      (parts.host.empty() && !parts.has_domain_wildcard) ||
      (!parts.is_port_wildcard))
    return false;

  // Test if the scheme is supported or a wildcard.
  if (!parts.is_scheme_wildcard &&
      parts.scheme != std::string(chrome::kHttpScheme) &&
      parts.scheme != std::string(chrome::kHttpsScheme)) {
    return false;
  }
  return true;
}

// ////////////////////////////////////////////////////////////////////////////
// ContentSettingsPattern::PatternParts
//
ContentSettingsPattern::PatternParts::PatternParts()
        : is_scheme_wildcard(false),
          has_domain_wildcard(false),
          is_port_wildcard(false),
          is_path_wildcard(false) {}

ContentSettingsPattern::PatternParts::~PatternParts() {}

// ////////////////////////////////////////////////////////////////////////////
// ContentSettingsPattern
//

// The version of the pattern format implemented. Version 1 includes the
// following patterns:
//   - [*.]domain.tld (matches domain.tld and all sub-domains)
//   - host (matches an exact hostname)
//   - a.b.c.d (matches an exact IPv4 ip)
//   - [a:b:c:d:e:f:g:h] (matches an exact IPv6 ip)
//   - file:///tmp/test.html (a complete URL without a host)
// Version 2 adds a resource identifier for plugins.
// TODO(jochen): update once this feature is no longer behind a flag.
const int ContentSettingsPattern::kContentSettingsPatternVersion = 1;

// TODO(markusheintz): These two constants were moved to the Pattern Parser.
// Remove once the dependency of the ContentSettingsBaseProvider is removed.
const char* ContentSettingsPattern::kDomainWildcard = "[*.]";
const size_t ContentSettingsPattern::kDomainWildcardLength = 4;

// static
BuilderInterface* ContentSettingsPattern::CreateBuilder(
    bool validate) {
  return new Builder(validate);
}

// static
ContentSettingsPattern ContentSettingsPattern::FromURL(
    const GURL& url) {
  scoped_ptr<ContentSettingsPattern::BuilderInterface> builder(
      ContentSettingsPattern::CreateBuilder(false));

  const GURL* local_url = &url;
  if (url.SchemeIsFileSystem() && url.inner_url()) {
    local_url = url.inner_url();
  }
  if (local_url->SchemeIsFile()) {
    builder->WithScheme(local_url->scheme())->WithPath(local_url->path());
  } else {
    // Please keep the order of the ifs below as URLs with an IP as host can
    // also have a "http" scheme.
    if (local_url->HostIsIPAddress()) {
      builder->WithScheme(local_url->scheme())->WithHost(local_url->host());
    } else if (local_url->SchemeIs(chrome::kHttpScheme)) {
      builder->WithSchemeWildcard()->WithDomainWildcard()->WithHost(
          local_url->host());
    } else if (local_url->SchemeIs(chrome::kHttpsScheme)) {
      builder->WithScheme(local_url->scheme())->WithDomainWildcard()->WithHost(
          local_url->host());
    } else {
      // Unsupported scheme
    }
    if (local_url->port().empty()) {
      if (local_url->SchemeIs(chrome::kHttpsScheme))
        builder->WithPort(GetDefaultPort(chrome::kHttpsScheme));
      else
        builder->WithPortWildcard();
    } else {
      builder->WithPort(local_url->port());
    }
  }
  return builder->Build();
}

// static
ContentSettingsPattern ContentSettingsPattern::FromURLNoWildcard(
    const GURL& url) {
  scoped_ptr<ContentSettingsPattern::BuilderInterface> builder(
      ContentSettingsPattern::CreateBuilder(false));

  const GURL* local_url = &url;
  if (url.SchemeIsFileSystem() && url.inner_url()) {
    local_url = url.inner_url();
  }
  if (local_url->SchemeIsFile()) {
    builder->WithScheme(local_url->scheme())->WithPath(local_url->path());
  } else {
    builder->WithScheme(local_url->scheme())->WithHost(local_url->host());
    if (local_url->port().empty()) {
      builder->WithPort(GetDefaultPort(local_url->scheme()));
    } else {
      builder->WithPort(local_url->port());
    }
  }
  return builder->Build();
}

// static
ContentSettingsPattern ContentSettingsPattern::FromString(
    const std::string& pattern_spec) {
  scoped_ptr<ContentSettingsPattern::BuilderInterface> builder(
      ContentSettingsPattern::CreateBuilder(false));
  content_settings::PatternParser::Parse(pattern_spec, builder.get());
  return builder->Build();
}

// static
ContentSettingsPattern ContentSettingsPattern::LegacyFromString(
    const std::string& pattern_spec) {
  scoped_ptr<ContentSettingsPattern::BuilderInterface> builder(
      ContentSettingsPattern::CreateBuilder(true));
  content_settings::PatternParser::Parse(pattern_spec, builder.get());
  return builder->Build();
}

// static
ContentSettingsPattern ContentSettingsPattern::Wildcard() {
  scoped_ptr<ContentSettingsPattern::BuilderInterface> builder(
      ContentSettingsPattern::CreateBuilder(true));
  builder->WithSchemeWildcard()->WithDomainWildcard()->WithPortWildcard()->
           WithPathWildcard();
  return builder->Build();
}

ContentSettingsPattern::ContentSettingsPattern()
  : is_valid_(false) {
}

ContentSettingsPattern::ContentSettingsPattern(
    const PatternParts& parts,
    bool valid)
    : parts_(parts),
      is_valid_(valid) {
}

void ContentSettingsPattern::WriteToMessage(IPC::Message* m) const {
  IPC::WriteParam(m, is_valid_);
  IPC::WriteParam(m, parts_);
}

bool ContentSettingsPattern::ReadFromMessage(const IPC::Message* m,
                                             PickleIterator* iter) {
  return IPC::ReadParam(m, iter, &is_valid_) &&
         IPC::ReadParam(m, iter, &parts_);
}

bool ContentSettingsPattern::Matches(
    const GURL& url) const {
  // An invalid pattern matches nothing.
  if (!is_valid_)
    return false;

  const GURL* local_url = &url;
  if (url.SchemeIsFileSystem() && url.inner_url()) {
    local_url = url.inner_url();
  }

  // Match the scheme part.
  const std::string scheme(local_url->scheme());
  if (!parts_.is_scheme_wildcard &&
      parts_.scheme != scheme) {
    return false;
  }

  // File URLs have no host. Matches if the pattern has the path wildcard set,
  // or if the path in the URL is identical to the one in the pattern.
  // For filesystem:file URLs, the path used is the filesystem type, so all
  // filesystem:file:///temporary/... are equivalent.
  // TODO(markusheintz): Content settings should be defined for all files on
  // a machine. Unless there is a good use case for supporting paths for file
  // patterns, stop supporting path for file patterns.
  if (!parts_.is_scheme_wildcard && scheme == chrome::kFileScheme)
    return parts_.is_path_wildcard ||
        parts_.path == std::string(local_url->path());

  // Match the host part.
  const std::string host(net::TrimEndingDot(local_url->host()));
  if (!parts_.has_domain_wildcard) {
    if (parts_.host != host)
      return false;
  } else {
    if (!IsSubDomainOrEqual(host, parts_.host))
      return false;
  }

  // For chrome extensions URLs ignore the port.
  if (parts_.scheme == std::string(extensions::kExtensionScheme))
    return true;

  // Match the port part.
  std::string port(local_url->port());

  // Use the default port if the port string is empty. GURL returns an empty
  // string if no port at all was specified or if the default port was
  // specified.
  if (port.empty()) {
    port = GetDefaultPort(scheme);
  }

  if (!parts_.is_port_wildcard &&
      parts_.port != port ) {
    return false;
  }

  return true;
}

bool ContentSettingsPattern::MatchesAllHosts() const {
  return parts_.has_domain_wildcard && parts_.host.empty();
}

const std::string ContentSettingsPattern::ToString() const {
  if (IsValid())
    return content_settings::PatternParser::ToString(parts_);
  else
    return std::string();
}

ContentSettingsPattern::Relation ContentSettingsPattern::Compare(
    const ContentSettingsPattern& other) const {
  // Two invalid patterns are identical in the way they behave. They don't match
  // anything and are represented as an empty string. So it's fair to treat them
  // as identical.
  if ((this == &other) ||
      (!is_valid_ && !other.is_valid_))
    return IDENTITY;

  if (!is_valid_ && other.is_valid_)
    return DISJOINT_ORDER_POST;
  if (is_valid_ && !other.is_valid_)
    return DISJOINT_ORDER_PRE;

  // If either host, port or scheme are disjoint return immediately.
  Relation host_relation = CompareHost(parts_, other.parts_);
  if (host_relation == DISJOINT_ORDER_PRE ||
      host_relation == DISJOINT_ORDER_POST)
    return host_relation;

  Relation port_relation = ComparePort(parts_, other.parts_);
  if (port_relation == DISJOINT_ORDER_PRE ||
      port_relation == DISJOINT_ORDER_POST)
    return port_relation;

  Relation scheme_relation = CompareScheme(parts_, other.parts_);
  if (scheme_relation == DISJOINT_ORDER_PRE ||
      scheme_relation == DISJOINT_ORDER_POST)
    return scheme_relation;

  if (host_relation != IDENTITY)
    return host_relation;
  if (port_relation != IDENTITY)
    return port_relation;
  return scheme_relation;
}

bool ContentSettingsPattern::operator==(
    const ContentSettingsPattern& other) const {
  return Compare(other) == IDENTITY;
}

bool ContentSettingsPattern::operator!=(
    const ContentSettingsPattern& other) const {
  return !(*this == other);
}

bool ContentSettingsPattern::operator<(
    const ContentSettingsPattern& other) const {
  return Compare(other) < 0;
}

bool ContentSettingsPattern::operator>(
    const ContentSettingsPattern& other) const {
  return Compare(other) > 0;
}

// static
ContentSettingsPattern::Relation ContentSettingsPattern::CompareHost(
    const ContentSettingsPattern::PatternParts& parts,
    const ContentSettingsPattern::PatternParts& other_parts) {
  if (!parts.has_domain_wildcard && !other_parts.has_domain_wildcard) {
    // Case 1: No host starts with a wild card
    int result = CompareDomainNames(parts.host, other_parts.host);
    if (result == 0)
      return ContentSettingsPattern::IDENTITY;
    if (result < 0)
      return ContentSettingsPattern::DISJOINT_ORDER_PRE;
    return ContentSettingsPattern::DISJOINT_ORDER_POST;
  } else if (parts.has_domain_wildcard && !other_parts.has_domain_wildcard) {
    // Case 2: |host| starts with a domain wildcard and |other_host| does not
    // start with a domain wildcard.
    // Examples:
    // "this" host:   [*.]google.com
    // "other" host:  google.com
    //
    // [*.]google.com
    // mail.google.com
    //
    // [*.]mail.google.com
    // google.com
    //
    // [*.]youtube.com
    // google.de
    //
    // [*.]youtube.com
    // mail.google.com
    //
    // *
    // google.de
    if (IsSubDomainOrEqual(other_parts.host, parts.host)) {
      return ContentSettingsPattern::SUCCESSOR;
    } else {
       if (CompareDomainNames(parts.host, other_parts.host) < 0)
         return ContentSettingsPattern::DISJOINT_ORDER_PRE;
       return ContentSettingsPattern::DISJOINT_ORDER_POST;
    }
  } else if (!parts.has_domain_wildcard && other_parts.has_domain_wildcard) {
    // Case 3: |host| starts NOT with a domain wildcard and |other_host| starts
    // with a domain wildcard.
    if (IsSubDomainOrEqual(parts.host, other_parts.host)) {
      return ContentSettingsPattern::PREDECESSOR;
    } else {
      if (CompareDomainNames(parts.host, other_parts.host) < 0)
        return ContentSettingsPattern::DISJOINT_ORDER_PRE;
      return ContentSettingsPattern::DISJOINT_ORDER_POST;
    }
  } else if (parts.has_domain_wildcard && other_parts.has_domain_wildcard) {
    // Case 4: |host| and |other_host| both start with a domain wildcard.
    // Examples:
    // [*.]google.com
    // [*.]google.com
    //
    // [*.]google.com
    // [*.]mail.google.com
    //
    // [*.]youtube.com
    // [*.]google.de
    //
    // [*.]youtube.com
    // [*.]mail.google.com
    //
    // [*.]youtube.com
    // *
    //
    // *
    // [*.]youtube.com
    if (parts.host == other_parts.host) {
      return ContentSettingsPattern::IDENTITY;
    } else if (IsSubDomainOrEqual(other_parts.host, parts.host)) {
      return ContentSettingsPattern::SUCCESSOR;
    } else if (IsSubDomainOrEqual(parts.host, other_parts.host)) {
      return ContentSettingsPattern::PREDECESSOR;
    } else {
      if (CompareDomainNames(parts.host, other_parts.host) < 0)
        return ContentSettingsPattern::DISJOINT_ORDER_PRE;
      return ContentSettingsPattern::DISJOINT_ORDER_POST;
    }
  }

  NOTREACHED();
  return ContentSettingsPattern::IDENTITY;
}

// static
ContentSettingsPattern::Relation ContentSettingsPattern::CompareScheme(
    const ContentSettingsPattern::PatternParts& parts,
    const ContentSettingsPattern::PatternParts& other_parts) {
  if (parts.is_scheme_wildcard && !other_parts.is_scheme_wildcard)
    return ContentSettingsPattern::SUCCESSOR;
  if (!parts.is_scheme_wildcard && other_parts.is_scheme_wildcard)
    return ContentSettingsPattern::PREDECESSOR;

  int result = parts.scheme.compare(other_parts.scheme);
  if (result == 0)
    return ContentSettingsPattern::IDENTITY;
  if (result > 0)
    return ContentSettingsPattern::DISJOINT_ORDER_PRE;
  return ContentSettingsPattern::DISJOINT_ORDER_POST;
}

// static
ContentSettingsPattern::Relation ContentSettingsPattern::ComparePort(
    const ContentSettingsPattern::PatternParts& parts,
    const ContentSettingsPattern::PatternParts& other_parts) {
  if (parts.is_port_wildcard && !other_parts.is_port_wildcard)
    return ContentSettingsPattern::SUCCESSOR;
  if (!parts.is_port_wildcard && other_parts.is_port_wildcard)
    return ContentSettingsPattern::PREDECESSOR;

  int result = parts.port.compare(other_parts.port);
  if (result == 0)
    return ContentSettingsPattern::IDENTITY;
  if (result > 0)
    return ContentSettingsPattern::DISJOINT_ORDER_PRE;
  return ContentSettingsPattern::DISJOINT_ORDER_POST;
}
