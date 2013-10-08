// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/net/url_fixer_upper.h"

#include <algorithm>

#if defined(OS_POSIX)
#include "base/environment.h"
#endif
#include "base/file_util.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/url_constants.h"
#include "net/base/escape.h"
#include "net/base/net_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/url_file.h"
#include "url/url_parse.h"
#include "url/url_util.h"

const char* URLFixerUpper::home_directory_override = NULL;

namespace {

// TODO(estade): Remove these ugly, ugly functions. They are only used in
// SegmentURL. A url_parse::Parsed object keeps track of a bunch of indices into
// a url string, and these need to be updated when the URL is converted from
// UTF8 to UTF16. Instead of this after-the-fact adjustment, we should parse it
// in the correct string format to begin with.
url_parse::Component UTF8ComponentToUTF16Component(
    const std::string& text_utf8,
    const url_parse::Component& component_utf8) {
  if (component_utf8.len == -1)
    return url_parse::Component();

  std::string before_component_string =
      text_utf8.substr(0, component_utf8.begin);
  std::string component_string = text_utf8.substr(component_utf8.begin,
                                                  component_utf8.len);
  string16 before_component_string_16 = UTF8ToUTF16(before_component_string);
  string16 component_string_16 = UTF8ToUTF16(component_string);
  url_parse::Component component_16(before_component_string_16.length(),
                                    component_string_16.length());
  return component_16;
}

void UTF8PartsToUTF16Parts(const std::string& text_utf8,
                           const url_parse::Parsed& parts_utf8,
                           url_parse::Parsed* parts) {
  if (IsStringASCII(text_utf8)) {
    *parts = parts_utf8;
    return;
  }

  parts->scheme =
      UTF8ComponentToUTF16Component(text_utf8, parts_utf8.scheme);
  parts ->username =
      UTF8ComponentToUTF16Component(text_utf8, parts_utf8.username);
  parts->password =
      UTF8ComponentToUTF16Component(text_utf8, parts_utf8.password);
  parts->host =
      UTF8ComponentToUTF16Component(text_utf8, parts_utf8.host);
  parts->port =
      UTF8ComponentToUTF16Component(text_utf8, parts_utf8.port);
  parts->path =
      UTF8ComponentToUTF16Component(text_utf8, parts_utf8.path);
  parts->query =
      UTF8ComponentToUTF16Component(text_utf8, parts_utf8.query);
  parts->ref =
      UTF8ComponentToUTF16Component(text_utf8, parts_utf8.ref);
}

TrimPositions TrimWhitespaceUTF8(const std::string& input,
                                 TrimPositions positions,
                                 std::string* output) {
  // This implementation is not so fast since it converts the text encoding
  // twice. Please feel free to file a bug if this function hurts the
  // performance of Chrome.
  DCHECK(IsStringUTF8(input));
  string16 input16 = UTF8ToUTF16(input);
  string16 output16;
  TrimPositions result = TrimWhitespace(input16, positions, &output16);
  *output = UTF16ToUTF8(output16);
  return result;
}

// does some basic fixes for input that we want to test for file-ness
void PrepareStringForFileOps(const base::FilePath& text,
                             base::FilePath::StringType* output) {
#if defined(OS_WIN)
  TrimWhitespace(text.value(), TRIM_ALL, output);
  replace(output->begin(), output->end(), '/', '\\');
#else
  TrimWhitespaceUTF8(text.value(), TRIM_ALL, output);
#endif
}

// Tries to create a full path from |text|.  If the result is valid and the
// file exists, returns true and sets |full_path| to the result.  Otherwise,
// returns false and leaves |full_path| unchanged.
bool ValidPathForFile(const base::FilePath::StringType& text,
                      base::FilePath* full_path) {
  base::FilePath file_path = base::MakeAbsoluteFilePath(base::FilePath(text));
  if (file_path.empty())
    return false;

  if (!base::PathExists(file_path))
    return false;

  *full_path = file_path;
  return true;
}

#if defined(OS_POSIX)
// Given a path that starts with ~, return a path that starts with an
// expanded-out /user/foobar directory.
std::string FixupHomedir(const std::string& text) {
  DCHECK(text.length() > 0 && text[0] == '~');

  if (text.length() == 1 || text[1] == '/') {
    const char* home = getenv(base::env_vars::kHome);
    if (URLFixerUpper::home_directory_override)
      home = URLFixerUpper::home_directory_override;
    // We'll probably break elsewhere if $HOME is undefined, but check here
    // just in case.
    if (!home)
      return text;
    return home + text.substr(1);
  }

  // Otherwise, this is a path like ~foobar/baz, where we must expand to
  // user foobar's home directory.  Officially, we should use getpwent(),
  // but that is a nasty blocking call.

#if defined(OS_MACOSX)
  static const char kHome[] = "/Users/";
#else
  static const char kHome[] = "/home/";
#endif
  return kHome + text.substr(1);
}
#endif

// Tries to create a file: URL from |text| if it looks like a filename, even if
// it doesn't resolve as a valid path or to an existing file.  Returns a
// (possibly invalid) file: URL in |fixed_up_url| for input beginning
// with a drive specifier or "\\".  Returns the unchanged input in other cases
// (including file: URLs: these don't look like filenames).
std::string FixupPath(const std::string& text) {
  DCHECK(!text.empty());

  base::FilePath::StringType filename;
#if defined(OS_WIN)
  base::FilePath input_path(UTF8ToWide(text));
  PrepareStringForFileOps(input_path, &filename);

  // Fixup Windows-style drive letters, where "C:" gets rewritten to "C|".
  if (filename.length() > 1 && filename[1] == '|')
    filename[1] = ':';
#elif defined(OS_POSIX)
  base::FilePath input_path(text);
  PrepareStringForFileOps(input_path, &filename);
  if (filename.length() > 0 && filename[0] == '~')
    filename = FixupHomedir(filename);
#endif

  // Here, we know the input looks like a file.
  GURL file_url = net::FilePathToFileURL(base::FilePath(filename));
  if (file_url.is_valid()) {
    return UTF16ToUTF8(net::FormatUrl(file_url, std::string(),
        net::kFormatUrlOmitUsernamePassword, net::UnescapeRule::NORMAL, NULL,
        NULL, NULL));
  }

  // Invalid file URL, just return the input.
  return text;
}

// Checks |domain| to see if a valid TLD is already present.  If not, appends
// |desired_tld| to the domain, and prepends "www." unless it's already present.
void AddDesiredTLD(const std::string& desired_tld, std::string* domain) {
  if (desired_tld.empty() || domain->empty())
    return;

  // Check the TLD.  If the return value is positive, we already have a TLD, so
  // abort.  If the return value is std::string::npos, there's no valid host,
  // but we can try to append a TLD anyway, since the host may become valid once
  // the TLD is attached -- for example, "999999999999" is detected as a broken
  // IP address and marked invalid, but attaching ".com" makes it legal.  When
  // the return value is 0, there's a valid host with no known TLD, so we can
  // definitely append the user's TLD.  We disallow unknown registries here so
  // users can input "mail.yahoo" and hit ctrl-enter to get
  // "www.mail.yahoo.com".
  const size_t registry_length =
      net::registry_controlled_domains::GetRegistryLength(
          *domain,
          net::registry_controlled_domains::EXCLUDE_UNKNOWN_REGISTRIES,
          net::registry_controlled_domains::EXCLUDE_PRIVATE_REGISTRIES);
  if ((registry_length != 0) && (registry_length != std::string::npos))
    return;

  // Add the suffix at the end of the domain.
  const size_t domain_length(domain->length());
  DCHECK_GT(domain_length, 0U);
  DCHECK_NE(desired_tld[0], '.');
  if ((*domain)[domain_length - 1] != '.')
    domain->push_back('.');
  domain->append(desired_tld);

  // Now, if the domain begins with "www.", stop.
  const std::string prefix("www.");
  if (domain->compare(0, prefix.length(), prefix) != 0) {
    // Otherwise, add www. to the beginning of the URL.
    domain->insert(0, prefix);
  }
}

inline void FixupUsername(const std::string& text,
                          const url_parse::Component& part,
                          std::string* url) {
  if (!part.is_valid())
    return;

  // We don't fix up the username at the moment.
  url->append(text, part.begin, part.len);
  // Do not append the trailing '@' because we might need to include the user's
  // password.  FixupURL itself will append the '@' for us.
}

inline void FixupPassword(const std::string& text,
                          const url_parse::Component& part,
                          std::string* url) {
  if (!part.is_valid())
    return;

  // We don't fix up the password at the moment.
  url->append(":");
  url->append(text, part.begin, part.len);
}

void FixupHost(const std::string& text,
               const url_parse::Component& part,
               bool has_scheme,
               const std::string& desired_tld,
               std::string* url) {
  if (!part.is_valid())
    return;

  // Make domain valid.
  // Strip all leading dots and all but one trailing dot, unless the user only
  // typed dots, in which case their input is totally invalid and we should just
  // leave it unchanged.
  std::string domain(text, part.begin, part.len);
  const size_t first_nondot(domain.find_first_not_of('.'));
  if (first_nondot != std::string::npos) {
    domain.erase(0, first_nondot);
    size_t last_nondot(domain.find_last_not_of('.'));
    DCHECK(last_nondot != std::string::npos);
    last_nondot += 2;  // Point at second period in ending string
    if (last_nondot < domain.length())
      domain.erase(last_nondot);
  }

  // Add any user-specified TLD, if applicable.
  AddDesiredTLD(desired_tld, &domain);

  url->append(domain);
}

void FixupPort(const std::string& text,
               const url_parse::Component& part,
               std::string* url) {
  if (!part.is_valid())
    return;

  // We don't fix up the port at the moment.
  url->append(":");
  url->append(text, part.begin, part.len);
}

inline void FixupPath(const std::string& text,
                      const url_parse::Component& part,
                      std::string* url) {
  if (!part.is_valid() || part.len == 0) {
    // We should always have a path.
    url->append("/");
    return;
  }

  // Append the path as is.
  url->append(text, part.begin, part.len);
}

inline void FixupQuery(const std::string& text,
                       const url_parse::Component& part,
                       std::string* url) {
  if (!part.is_valid())
    return;

  // We don't fix up the query at the moment.
  url->append("?");
  url->append(text, part.begin, part.len);
}

inline void FixupRef(const std::string& text,
                     const url_parse::Component& part,
                     std::string* url) {
  if (!part.is_valid())
    return;

  // We don't fix up the ref at the moment.
  url->append("#");
  url->append(text, part.begin, part.len);
}

bool HasPort(const std::string& original_text,
             const url_parse::Component& scheme_component) {
  // Find the range between the ":" and the "/".
  size_t port_start = scheme_component.end() + 1;
  size_t port_end = port_start;
  while ((port_end < original_text.length()) &&
         !url_parse::IsAuthorityTerminator(original_text[port_end]))
    ++port_end;
  if (port_end == port_start)
    return false;

  // Scan the range to see if it is entirely digits.
  for (size_t i = port_start; i < port_end; ++i) {
    if (!IsAsciiDigit(original_text[i]))
      return false;
  }

  return true;
}

// Try to extract a valid scheme from the beginning of |text|.
// If successful, set |scheme_component| to the text range where the scheme
// was located, and fill |canon_scheme| with its canonicalized form.
// Otherwise, return false and leave the outputs in an indeterminate state.
bool GetValidScheme(const std::string &text,
                    url_parse::Component* scheme_component,
                    std::string* canon_scheme) {
  // Locate everything up to (but not including) the first ':'
  if (!url_parse::ExtractScheme(text.data(), static_cast<int>(text.length()),
                                scheme_component)) {
    return false;
  }

  // Make sure the scheme contains only valid characters, and convert
  // to lowercase.  This also catches IPv6 literals like [::1], because
  // brackets are not in the whitelist.
  url_canon::StdStringCanonOutput canon_scheme_output(canon_scheme);
  url_parse::Component canon_scheme_component;
  if (!url_canon::CanonicalizeScheme(text.data(), *scheme_component,
                                     &canon_scheme_output,
                                     &canon_scheme_component))
    return false;

  // Strip the ':', and any trailing buffer space.
  DCHECK_EQ(0, canon_scheme_component.begin);
  canon_scheme->erase(canon_scheme_component.len);

  // We need to fix up the segmentation for "www.example.com:/".  For this
  // case, we guess that schemes with a "." are not actually schemes.
  if (canon_scheme->find('.') != std::string::npos)
    return false;

  // We need to fix up the segmentation for "www:123/".  For this case, we
  // will add an HTTP scheme later and make the URL parser happy.
  // TODO(pkasting): Maybe we should try to use GURL's parser for this?
  if (HasPort(text, *scheme_component))
    return false;

  // Everything checks out.
  return true;
}

// Performs the work for URLFixerUpper::SegmentURL. |text| may be modified on
// output on success: a semicolon following a valid scheme is replaced with a
// colon.
std::string SegmentURLInternal(std::string* text, url_parse::Parsed* parts) {
  // Initialize the result.
  *parts = url_parse::Parsed();

  std::string trimmed;
  TrimWhitespaceUTF8(*text, TRIM_ALL, &trimmed);
  if (trimmed.empty())
    return std::string();  // Nothing to segment.

#if defined(OS_WIN)
  int trimmed_length = static_cast<int>(trimmed.length());
  if (url_parse::DoesBeginWindowsDriveSpec(trimmed.data(), 0, trimmed_length) ||
      url_parse::DoesBeginUNCPath(trimmed.data(), 0, trimmed_length, true))
    return "file";
#elif defined(OS_POSIX)
  if (base::FilePath::IsSeparator(trimmed.data()[0]) ||
      trimmed.data()[0] == '~')
    return "file";
#endif

  // Otherwise, we need to look at things carefully.
  std::string scheme;
  if (!GetValidScheme(*text, &parts->scheme, &scheme)) {
    // Try again if there is a ';' in the text. If changing it to a ':' results
    // in a scheme being found, continue processing with the modified text.
    bool found_scheme = false;
    size_t semicolon = text->find(';');
    if (semicolon != 0 && semicolon != std::string::npos) {
      (*text)[semicolon] = ':';
      if (GetValidScheme(*text, &parts->scheme, &scheme))
        found_scheme = true;
      else
        (*text)[semicolon] = ';';
    }
    if (!found_scheme) {
      // Couldn't determine the scheme, so just pick one.
      parts->scheme.reset();
      scheme.assign(StartsWithASCII(*text, "ftp.", false) ?
                    chrome::kFtpScheme : chrome::kHttpScheme);
    }
  }

  // Proceed with about and chrome schemes, but not file or nonstandard schemes.
  if ((scheme != chrome::kAboutScheme) && (scheme != chrome::kChromeUIScheme) &&
      ((scheme == chrome::kFileScheme) || !url_util::IsStandard(scheme.c_str(),
           url_parse::Component(0, static_cast<int>(scheme.length())))))
    return scheme;

  if (scheme == chrome::kFileSystemScheme) {
    // Have the GURL parser do the heavy lifting for us.
    url_parse::ParseFileSystemURL(text->data(),
        static_cast<int>(text->length()), parts);
    return scheme;
  }

  if (parts->scheme.is_valid()) {
    // Have the GURL parser do the heavy lifting for us.
    url_parse::ParseStandardURL(text->data(), static_cast<int>(text->length()),
                                parts);
    return scheme;
  }

  // We need to add a scheme in order for ParseStandardURL to be happy.
  // Find the first non-whitespace character.
  std::string::iterator first_nonwhite = text->begin();
  while ((first_nonwhite != text->end()) && IsWhitespace(*first_nonwhite))
    ++first_nonwhite;

  // Construct the text to parse by inserting the scheme.
  std::string inserted_text(scheme);
  inserted_text.append(content::kStandardSchemeSeparator);
  std::string text_to_parse(text->begin(), first_nonwhite);
  text_to_parse.append(inserted_text);
  text_to_parse.append(first_nonwhite, text->end());

  // Have the GURL parser do the heavy lifting for us.
  url_parse::ParseStandardURL(text_to_parse.data(),
                              static_cast<int>(text_to_parse.length()),
                              parts);

  // Offset the results of the parse to match the original text.
  const int offset = -static_cast<int>(inserted_text.length());
  URLFixerUpper::OffsetComponent(offset, &parts->scheme);
  URLFixerUpper::OffsetComponent(offset, &parts->username);
  URLFixerUpper::OffsetComponent(offset, &parts->password);
  URLFixerUpper::OffsetComponent(offset, &parts->host);
  URLFixerUpper::OffsetComponent(offset, &parts->port);
  URLFixerUpper::OffsetComponent(offset, &parts->path);
  URLFixerUpper::OffsetComponent(offset, &parts->query);
  URLFixerUpper::OffsetComponent(offset, &parts->ref);

  return scheme;
}

}  // namespace

std::string URLFixerUpper::SegmentURL(const std::string& text,
                                      url_parse::Parsed* parts) {
  std::string mutable_text(text);
  return SegmentURLInternal(&mutable_text, parts);
}

string16 URLFixerUpper::SegmentURL(const string16& text,
                                   url_parse::Parsed* parts) {
  std::string text_utf8 = UTF16ToUTF8(text);
  url_parse::Parsed parts_utf8;
  std::string scheme_utf8 = SegmentURL(text_utf8, &parts_utf8);
  UTF8PartsToUTF16Parts(text_utf8, parts_utf8, parts);
  return UTF8ToUTF16(scheme_utf8);
}

GURL URLFixerUpper::FixupURL(const std::string& text,
                             const std::string& desired_tld) {
  std::string trimmed;
  TrimWhitespaceUTF8(text, TRIM_ALL, &trimmed);
  if (trimmed.empty())
    return GURL();  // Nothing here.

  // Segment the URL.
  url_parse::Parsed parts;
  std::string scheme(SegmentURLInternal(&trimmed, &parts));

  // For view-source: URLs, we strip "view-source:", do fixup, and stick it back
  // on.  This allows us to handle things like "view-source:google.com".
  if (scheme == content::kViewSourceScheme) {
    // Reject "view-source:view-source:..." to avoid deep recursion.
    std::string view_source(content::kViewSourceScheme + std::string(":"));
    if (!StartsWithASCII(text, view_source + view_source, false)) {
      return GURL(content::kViewSourceScheme + std::string(":") +
          FixupURL(trimmed.substr(scheme.length() + 1),
                   desired_tld).possibly_invalid_spec());
    }
  }

  // We handle the file scheme separately.
  if (scheme == chrome::kFileScheme)
    return GURL(parts.scheme.is_valid() ? text : FixupPath(text));

  // We handle the filesystem scheme separately.
  if (scheme == chrome::kFileSystemScheme) {
    if (parts.inner_parsed() && parts.inner_parsed()->scheme.is_valid())
      return GURL(text);
    return GURL();
  }

  // Parse and rebuild about: and chrome: URLs, except about:blank.
  bool chrome_url = !LowerCaseEqualsASCII(trimmed, content::kAboutBlankURL) &&
      ((scheme == chrome::kAboutScheme) || (scheme == chrome::kChromeUIScheme));

  // For some schemes whose layouts we understand, we rebuild it.
  if (chrome_url || url_util::IsStandard(scheme.c_str(),
          url_parse::Component(0, static_cast<int>(scheme.length())))) {
    // Replace the about: scheme with the chrome: scheme.
    std::string url(chrome_url ? chrome::kChromeUIScheme : scheme);
    url.append(content::kStandardSchemeSeparator);

    // We need to check whether the |username| is valid because it is our
    // responsibility to append the '@' to delineate the user information from
    // the host portion of the URL.
    if (parts.username.is_valid()) {
      FixupUsername(trimmed, parts.username, &url);
      FixupPassword(trimmed, parts.password, &url);
      url.append("@");
    }

    FixupHost(trimmed, parts.host, parts.scheme.is_valid(), desired_tld, &url);
    if (chrome_url && !parts.host.is_valid())
      url.append(chrome::kChromeUIDefaultHost);
    FixupPort(trimmed, parts.port, &url);
    FixupPath(trimmed, parts.path, &url);
    FixupQuery(trimmed, parts.query, &url);
    FixupRef(trimmed, parts.ref, &url);

    return GURL(url);
  }

  // In the worst-case, we insert a scheme if the URL lacks one.
  if (!parts.scheme.is_valid()) {
    std::string fixed_scheme(scheme);
    fixed_scheme.append(content::kStandardSchemeSeparator);
    trimmed.insert(0, fixed_scheme);
  }

  return GURL(trimmed);
}

// The rules are different here than for regular fixup, since we need to handle
// input like "hello.html" and know to look in the current directory.  Regular
// fixup will look for cues that it is actually a file path before trying to
// figure out what file it is.  If our logic doesn't work, we will fall back on
// regular fixup.
GURL URLFixerUpper::FixupRelativeFile(const base::FilePath& base_dir,
                                      const base::FilePath& text) {
  base::FilePath old_cur_directory;
  if (!base_dir.empty()) {
    // Save the old current directory before we move to the new one.
    file_util::GetCurrentDirectory(&old_cur_directory);
    file_util::SetCurrentDirectory(base_dir);
  }

  // Allow funny input with extra whitespace and the wrong kind of slashes.
  base::FilePath::StringType trimmed;
  PrepareStringForFileOps(text, &trimmed);

  bool is_file = true;
  // Avoid recognizing definite non-file URLs as file paths.
  GURL gurl(trimmed);
  if (gurl.is_valid() && gurl.IsStandard())
    is_file = false;
  base::FilePath full_path;
  if (is_file && !ValidPathForFile(trimmed, &full_path)) {
    // Not a path as entered, try unescaping it in case the user has
    // escaped things. We need to go through 8-bit since the escaped values
    // only represent 8-bit values.
#if defined(OS_WIN)
    std::wstring unescaped = UTF8ToWide(net::UnescapeURLComponent(
        WideToUTF8(trimmed),
        net::UnescapeRule::SPACES | net::UnescapeRule::URL_SPECIAL_CHARS));
#elif defined(OS_POSIX)
    std::string unescaped = net::UnescapeURLComponent(
        trimmed,
        net::UnescapeRule::SPACES | net::UnescapeRule::URL_SPECIAL_CHARS);
#endif

    if (!ValidPathForFile(unescaped, &full_path))
      is_file = false;
  }

  // Put back the current directory if we saved it.
  if (!base_dir.empty())
    file_util::SetCurrentDirectory(old_cur_directory);

  if (is_file) {
    GURL file_url = net::FilePathToFileURL(full_path);
    if (file_url.is_valid())
      return GURL(UTF16ToUTF8(net::FormatUrl(file_url, std::string(),
          net::kFormatUrlOmitUsernamePassword, net::UnescapeRule::NORMAL, NULL,
          NULL, NULL)));
    // Invalid files fall through to regular processing.
  }

  // Fall back on regular fixup for this input.
#if defined(OS_WIN)
  std::string text_utf8 = WideToUTF8(text.value());
#elif defined(OS_POSIX)
  std::string text_utf8 = text.value();
#endif
  return FixupURL(text_utf8, std::string());
}

void URLFixerUpper::OffsetComponent(int offset, url_parse::Component* part) {
  DCHECK(part);

  if (part->is_valid()) {
    // Offset the location of this component.
    part->begin += offset;

    // This part might not have existed in the original text.
    if (part->begin < 0)
      part->reset();
  }
}
