// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/custom_handlers/register_protocol_handler_permission_request.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/custom_handlers/protocol_handler_registry.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/user_metrics.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/vector_icons_public.h"

#if defined(OS_MACOSX)
#include "grit/theme_resources.h"
#endif

namespace {

base::string16 GetProtocolName(
    const ProtocolHandler& handler) {
  if (handler.protocol() == "mailto")
    return l10n_util::GetStringUTF16(IDS_REGISTER_PROTOCOL_HANDLER_MAILTO_NAME);
  if (handler.protocol() == "webcal")
    return l10n_util::GetStringUTF16(IDS_REGISTER_PROTOCOL_HANDLER_WEBCAL_NAME);
  return base::UTF8ToUTF16(handler.protocol());
}

}  // namespace

RegisterProtocolHandlerPermissionRequest
::RegisterProtocolHandlerPermissionRequest(
      ProtocolHandlerRegistry* registry,
      const ProtocolHandler& handler,
      GURL url,
      bool user_gesture)
    : registry_(registry),
      handler_(handler),
      url_(url),
      user_gesture_(user_gesture) {}

RegisterProtocolHandlerPermissionRequest::
~RegisterProtocolHandlerPermissionRequest() {}

gfx::VectorIconId RegisterProtocolHandlerPermissionRequest::GetVectorIconId()
    const {
#if defined(OS_MACOSX)
  return gfx::VectorIconId::VECTOR_ICON_NONE;
#else
  return gfx::VectorIconId::PROTOCOL_HANDLER;
#endif
}

int RegisterProtocolHandlerPermissionRequest::GetIconId() const {
#if defined(OS_MACOSX)
  return IDR_REGISTER_PROTOCOL_HANDLER;
#else
  return 0;
#endif
}

base::string16
RegisterProtocolHandlerPermissionRequest::GetMessageText() const {
  ProtocolHandler old_handler = registry_->GetHandlerFor(handler_.protocol());
  return old_handler.IsEmpty() ?
      l10n_util::GetStringFUTF16(
          IDS_REGISTER_PROTOCOL_HANDLER_CONFIRM,
          base::UTF8ToUTF16(handler_.url().host()),
          GetProtocolName(handler_)) :
      l10n_util::GetStringFUTF16(
          IDS_REGISTER_PROTOCOL_HANDLER_CONFIRM_REPLACE,
          base::UTF8ToUTF16(handler_.url().host()),
          GetProtocolName(handler_),
          base::UTF8ToUTF16(old_handler.url().host()));
}

base::string16
RegisterProtocolHandlerPermissionRequest::GetMessageTextFragment() const {
  ProtocolHandler old_handler = registry_->GetHandlerFor(handler_.protocol());
  return old_handler.IsEmpty() ?
      l10n_util::GetStringFUTF16(
          IDS_REGISTER_PROTOCOL_HANDLER_CONFIRM_FRAGMENT,
          GetProtocolName(handler_)) :
      l10n_util::GetStringFUTF16(
          IDS_REGISTER_PROTOCOL_HANDLER_CONFIRM_REPLACE_FRAGMENT,
          GetProtocolName(handler_),
          base::UTF8ToUTF16(old_handler.url().host()));
}

bool RegisterProtocolHandlerPermissionRequest::HasUserGesture() const {
  return user_gesture_;
}

GURL RegisterProtocolHandlerPermissionRequest::GetRequestingHostname() const {
  return url_;
}

void RegisterProtocolHandlerPermissionRequest::PermissionGranted() {
  content::RecordAction(
      base::UserMetricsAction("RegisterProtocolHandler.Infobar_Accept"));
  registry_->OnAcceptRegisterProtocolHandler(handler_);
}

void RegisterProtocolHandlerPermissionRequest::PermissionDenied() {
  content::RecordAction(
      base::UserMetricsAction("RegisterProtocolHandler.InfoBar_Deny"));
  registry_->OnIgnoreRegisterProtocolHandler(handler_);
}

void RegisterProtocolHandlerPermissionRequest::Cancelled() {
  content::RecordAction(
      base::UserMetricsAction("RegisterProtocolHandler.InfoBar_Deny"));
  registry_->OnIgnoreRegisterProtocolHandler(handler_);
}

void RegisterProtocolHandlerPermissionRequest::RequestFinished() {
  delete this;
}
