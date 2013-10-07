// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_warning_set.h"

#include "base/files/file_path.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_set.h"
#include "content/public/browser/browser_thread.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "net/base/escape.h"
#include "ui/base/l10n/l10n_util.h"

using content::BrowserThread;

namespace {
// Prefix for message parameters indicating that the parameter needs to
// be translated from an extension id to the extension name.
const char kTranslate[] = "TO_TRANSLATE:";
const size_t kMaxNumberOfParameters = 4;
}

namespace extensions {

//
// ExtensionWarning
//

ExtensionWarning::ExtensionWarning(
    WarningType type,
    const std::string& extension_id,
    int message_id,
    const std::vector<std::string>& message_parameters)
    : type_(type),
      extension_id_(extension_id),
      message_id_(message_id),
      message_parameters_(message_parameters) {
  // These are invalid here because they do not have corresponding warning
  // messages in the UI.
  CHECK_NE(type, kInvalid);
  CHECK_NE(type, kMaxWarningType);
  CHECK_LE(message_parameters.size(), kMaxNumberOfParameters);
}

ExtensionWarning::ExtensionWarning(const ExtensionWarning& other)
  : type_(other.type_),
    extension_id_(other.extension_id_),
    message_id_(other.message_id_),
    message_parameters_(other.message_parameters_) {}

ExtensionWarning::~ExtensionWarning() {
}

ExtensionWarning& ExtensionWarning::operator=(const ExtensionWarning& other) {
  type_ = other.type_;
  extension_id_ = other.extension_id_;
  message_id_ = other.message_id_;
  message_parameters_ = other.message_parameters_;
  return *this;
}

// static
ExtensionWarning ExtensionWarning::CreateNetworkDelayWarning(
    const std::string& extension_id) {
  std::vector<std::string> message_parameters;
  message_parameters.push_back(l10n_util::GetStringUTF8(IDS_PRODUCT_NAME));
  return ExtensionWarning(
      kNetworkDelay,
      extension_id,
      IDS_EXTENSION_WARNINGS_NETWORK_DELAY,
      message_parameters);
}

// static
ExtensionWarning ExtensionWarning::CreateNetworkConflictWarning(
    const std::string& extension_id) {
  std::vector<std::string> message_parameters;
  return ExtensionWarning(
      kNetworkConflict,
      extension_id,
      IDS_EXTENSION_WARNINGS_NETWORK_CONFLICT,
      message_parameters);
}

// static
ExtensionWarning ExtensionWarning::CreateRedirectConflictWarning(
    const std::string& extension_id,
    const std::string& winning_extension_id,
    const GURL& attempted_redirect_url,
    const GURL& winning_redirect_url) {
  std::vector<std::string> message_parameters;
  message_parameters.push_back(attempted_redirect_url.spec());
  message_parameters.push_back(kTranslate + winning_extension_id);
  message_parameters.push_back(winning_redirect_url.spec());
  return ExtensionWarning(
      kRedirectConflict,
      extension_id,
      IDS_EXTENSION_WARNINGS_REDIRECT_CONFLICT,
      message_parameters);
}

// static
ExtensionWarning ExtensionWarning::CreateRequestHeaderConflictWarning(
    const std::string& extension_id,
    const std::string& winning_extension_id,
    const std::string& conflicting_header) {
  std::vector<std::string> message_parameters;
  message_parameters.push_back(conflicting_header);
  message_parameters.push_back(kTranslate + winning_extension_id);
  return ExtensionWarning(
      kNetworkConflict,
      extension_id,
      IDS_EXTENSION_WARNINGS_REQUEST_HEADER_CONFLICT,
      message_parameters);
}

// static
ExtensionWarning ExtensionWarning::CreateResponseHeaderConflictWarning(
    const std::string& extension_id,
    const std::string& winning_extension_id,
    const std::string& conflicting_header) {
  std::vector<std::string> message_parameters;
  message_parameters.push_back(conflicting_header);
  message_parameters.push_back(kTranslate + winning_extension_id);
  return ExtensionWarning(
      kNetworkConflict,
      extension_id,
      IDS_EXTENSION_WARNINGS_RESPONSE_HEADER_CONFLICT,
      message_parameters);
}

// static
ExtensionWarning ExtensionWarning::CreateCredentialsConflictWarning(
    const std::string& extension_id,
    const std::string& winning_extension_id) {
  std::vector<std::string> message_parameters;
  message_parameters.push_back(kTranslate + winning_extension_id);
  return ExtensionWarning(
      kNetworkConflict,
      extension_id,
      IDS_EXTENSION_WARNINGS_CREDENTIALS_CONFLICT,
      message_parameters);
}

// static
ExtensionWarning ExtensionWarning::CreateRepeatedCacheFlushesWarning(
    const std::string& extension_id) {
  std::vector<std::string> message_parameters;
  message_parameters.push_back(l10n_util::GetStringUTF8(IDS_PRODUCT_NAME));
  return ExtensionWarning(
      kRepeatedCacheFlushes,
      extension_id,
      IDS_EXTENSION_WARNINGS_NETWORK_DELAY,
      message_parameters);
}

// static
ExtensionWarning ExtensionWarning::CreateDownloadFilenameConflictWarning(
    const std::string& losing_extension_id,
    const std::string& winning_extension_id,
    const base::FilePath& losing_filename,
    const base::FilePath& winning_filename) {
  std::vector<std::string> message_parameters;
  message_parameters.push_back(UTF16ToUTF8(losing_filename.LossyDisplayName()));
  message_parameters.push_back(kTranslate + winning_extension_id);
  message_parameters.push_back(UTF16ToUTF8(
      winning_filename.LossyDisplayName()));
  return ExtensionWarning(
      kDownloadFilenameConflict,
      losing_extension_id,
      IDS_EXTENSION_WARNINGS_DOWNLOAD_FILENAME_CONFLICT,
      message_parameters);
}

std::string ExtensionWarning::GetLocalizedMessage(
    const ExtensionSet* extensions) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // These parameters may be unsafe (URLs and Extension names) and need
  // to be HTML-escaped before being embedded in the UI. Also extension IDs
  // are translated to full extension names.
  std::vector<string16> final_parameters;
  for (size_t i = 0; i < message_parameters_.size(); ++i) {
    std::string message = message_parameters_[i];
    if (StartsWithASCII(message, kTranslate, true)) {
      std::string extension_id = message.substr(sizeof(kTranslate) - 1);
      const extensions::Extension* extension =
          extensions->GetByID(extension_id);
      message = extension ? extension->name() : extension_id;
    }
    final_parameters.push_back(UTF8ToUTF16(net::EscapeForHTML(message)));
  }

  COMPILE_ASSERT(kMaxNumberOfParameters == 4u, YouNeedToAddMoreCaseStatements);
  switch (final_parameters.size()) {
    case 0:
      return l10n_util::GetStringUTF8(message_id_);
    case 1:
      return l10n_util::GetStringFUTF8(message_id_, final_parameters[0]);
    case 2:
      return l10n_util::GetStringFUTF8(message_id_, final_parameters[0],
          final_parameters[1]);
    case 3:
      return l10n_util::GetStringFUTF8(message_id_, final_parameters[0],
          final_parameters[1], final_parameters[2]);
    case 4:
      return l10n_util::GetStringFUTF8(message_id_, final_parameters[0],
          final_parameters[1], final_parameters[2], final_parameters[3]);
    default:
      NOTREACHED();
      return std::string();
  }
}

bool operator<(const ExtensionWarning& a, const ExtensionWarning& b) {
  if (a.extension_id() != b.extension_id())
    return a.extension_id() < b.extension_id();
  return a.warning_type() < b.warning_type();
}

}  // namespace extensions
