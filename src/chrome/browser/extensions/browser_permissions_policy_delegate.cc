// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/browser_permissions_policy_delegate.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "content/public/browser/browser_thread.h"

#if !defined(OS_CHROMEOS)
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#endif

namespace extensions {

namespace errors = extension_manifest_errors;

BrowserPermissionsPolicyDelegate::BrowserPermissionsPolicyDelegate() {
  PermissionsData::SetPolicyDelegate(this);
}
BrowserPermissionsPolicyDelegate::~BrowserPermissionsPolicyDelegate() {
  PermissionsData::SetPolicyDelegate(NULL);
}

bool BrowserPermissionsPolicyDelegate::CanExecuteScriptOnPage(
    const Extension* extension,
    const GURL& document_url,
    const GURL& top_document_url,
    int tab_id,
    const UserScript* script,
    int process_id,
    std::string* error) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

#if !defined(OS_CHROMEOS)
  // NULL in unit tests.
  if (!g_browser_process->profile_manager())
    return true;

  // We don't have a Profile in this context. That's OK - for our purposes,
  // we can just check every Profile for its signin process. If any of them
  // match, block script access.
  std::vector<Profile*> profiles =
      g_browser_process->profile_manager()->GetLoadedProfiles();
  for (std::vector<Profile*>::iterator profile = profiles.begin();
       profile != profiles.end(); ++profile) {
    SigninManager* signin_manager =
        SigninManagerFactory::GetForProfile(*profile);
    if (signin_manager && signin_manager->IsSigninProcess(process_id)) {
      if (error)
        *error = errors::kCannotScriptSigninPage;
      return false;
    }
  }
#endif

  return true;
}

}  // namespace extensions
