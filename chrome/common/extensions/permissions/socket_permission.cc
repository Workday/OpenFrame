// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/permissions/socket_permission.h"

#include <algorithm>

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/extensions/permissions/permissions_info.h"
#include "chrome/common/extensions/permissions/set_disjunction_permission.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace extensions {

SocketPermission::SocketPermission(const APIPermissionInfo* info)
  : SetDisjunctionPermission<SocketPermissionData, SocketPermission>(info) {
}

SocketPermission::~SocketPermission() {
}

PermissionMessages SocketPermission::GetMessages() const {
  DCHECK(HasMessages());
  PermissionMessages result;
  if (!AddAnyHostMessage(result)) {
    AddSpecificHostMessage(result);
    AddSubdomainHostMessage(result);
  }
  return result;
}

bool SocketPermission::AddAnyHostMessage(PermissionMessages& messages)
    const {
  std::set<SocketPermissionData>::const_iterator i;
  for (i = data_set_.begin(); i != data_set_.end(); ++i) {
    if (i->GetHostType() == SocketPermissionData::ANY_HOST) {
      messages.push_back(PermissionMessage(
            PermissionMessage::kSocketAnyHost,
            l10n_util::GetStringUTF16(
                IDS_EXTENSION_PROMPT_WARNING_SOCKET_ANY_HOST)));
      return true;
    }
  }
  return false;
}

void SocketPermission::AddSubdomainHostMessage(PermissionMessages& messages)
    const {
  std::set<string16> domains;
  std::set<SocketPermissionData>::const_iterator i;
  for (i = data_set_.begin(); i != data_set_.end(); ++i) {
    if (i->GetHostType() == SocketPermissionData::HOSTS_IN_DOMAINS)
      domains.insert(UTF8ToUTF16(i->GetHost()));
  }
  if (!domains.empty()) {
    int id = (domains.size() == 1) ?
             IDS_EXTENSION_PROMPT_WARNING_SOCKET_HOSTS_IN_DOMAIN :
             IDS_EXTENSION_PROMPT_WARNING_SOCKET_HOSTS_IN_DOMAINS;
    messages.push_back(PermissionMessage(
          PermissionMessage::kSocketDomainHosts,
          l10n_util::GetStringFUTF16(
              id,
              JoinString(
                  std::vector<string16>(
                      domains.begin(), domains.end()), ' '))));
  }
}

void SocketPermission::AddSpecificHostMessage(PermissionMessages& messages)
    const {
  std::set<string16> hostnames;
  std::set<SocketPermissionData>::const_iterator i;
  for (i = data_set_.begin(); i != data_set_.end(); ++i) {
    if (i->GetHostType() == SocketPermissionData::SPECIFIC_HOSTS)
      hostnames.insert(UTF8ToUTF16(i->GetHost()));
  }
  if (!hostnames.empty()) {
    int id = (hostnames.size() == 1) ?
             IDS_EXTENSION_PROMPT_WARNING_SOCKET_SPECIFIC_HOST :
             IDS_EXTENSION_PROMPT_WARNING_SOCKET_SPECIFIC_HOSTS;
    messages.push_back(PermissionMessage(
          PermissionMessage::kSocketSpecificHosts,
          l10n_util::GetStringFUTF16(
              id,
              JoinString(
                  std::vector<string16>(
                      hostnames.begin(), hostnames.end()), ' '))));
  }
}

}  // namespace extensions
