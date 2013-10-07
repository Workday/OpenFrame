// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/external_protocol_dialog_delegate.h"

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/external_protocol/external_protocol_handler.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/text/text_elider.h"

ExternalProtocolDialogDelegate::ExternalProtocolDialogDelegate(const GURL& url)
    : ProtocolDialogDelegate(url) {
}

ExternalProtocolDialogDelegate::~ExternalProtocolDialogDelegate() {
}

string16 ExternalProtocolDialogDelegate::GetMessageText() const {
  const int kMaxUrlWithoutSchemeSize = 256;
  const int kMaxCommandSize = 256;
  // TODO(calamity): Look up the command in ExternalProtocolHandler and pass it
  // into the constructor. Will require simultaneous change of
  // ExternalProtocolHandler::RunExternalProtocolDialog across all platforms.
  string16 command =
    UTF8ToUTF16(ShellIntegration::GetApplicationForProtocol(url()));
  string16 elided_url_without_scheme;
  string16 elided_command;
  ui::ElideString(ASCIIToUTF16(url().possibly_invalid_spec()),
                  kMaxUrlWithoutSchemeSize, &elided_url_without_scheme);
  ui::ElideString(command, kMaxCommandSize, &elided_command);

  string16 message_text = l10n_util::GetStringFUTF16(
      IDS_EXTERNAL_PROTOCOL_INFORMATION,
      ASCIIToUTF16(url().scheme() + ":"),
      elided_url_without_scheme) + ASCIIToUTF16("\n\n");

  message_text += l10n_util::GetStringFUTF16(
      IDS_EXTERNAL_PROTOCOL_APPLICATION_TO_LAUNCH,
      elided_command) + ASCIIToUTF16("\n\n");

  message_text += l10n_util::GetStringUTF16(IDS_EXTERNAL_PROTOCOL_WARNING);
  return message_text;
}

string16 ExternalProtocolDialogDelegate::GetCheckboxText() const {
  return l10n_util::GetStringUTF16(IDS_EXTERNAL_PROTOCOL_CHECKBOX_TEXT);
}

string16 ExternalProtocolDialogDelegate::GetTitleText() const {
  return l10n_util::GetStringUTF16(IDS_EXTERNAL_PROTOCOL_TITLE);
}

void ExternalProtocolDialogDelegate::DoAccept(
    const GURL& url,
    bool dont_block) const {
  if (dont_block) {
      ExternalProtocolHandler::SetBlockState(
          url.scheme(), ExternalProtocolHandler::DONT_BLOCK);
  }

  ExternalProtocolHandler::LaunchUrlWithoutSecurityCheck(url);
}

void ExternalProtocolDialogDelegate::DoCancel(
    const GURL& url,
    bool dont_block) const {
  if (dont_block) {
      ExternalProtocolHandler::SetBlockState(
          url.scheme(), ExternalProtocolHandler::BLOCK);
  }
}
