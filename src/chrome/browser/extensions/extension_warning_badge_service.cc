// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_warning_badge_service.h"

#include "base/stl_util.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/ui/global_error/global_error.h"
#include "chrome/browser/ui/global_error/global_error_service.h"
#include "chrome/browser/ui/global_error/global_error_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace extensions {

namespace {
// Non-modal GlobalError implementation that warns the user if extensions
// created warnings or errors. If the user clicks on the wrench menu, the user
// is redirected to chrome://extensions to inspect the errors.
class ErrorBadge : public GlobalError {
 public:
  explicit ErrorBadge(ExtensionWarningBadgeService* badge_service);
  virtual ~ErrorBadge();

  // Implementation for GlobalError:
  virtual bool HasMenuItem() OVERRIDE;
  virtual int MenuItemCommandID() OVERRIDE;
  virtual string16 MenuItemLabel() OVERRIDE;
  virtual void ExecuteMenuItem(Browser* browser) OVERRIDE;

  virtual bool HasBubbleView() OVERRIDE;
  virtual string16 GetBubbleViewTitle() OVERRIDE;
  virtual std::vector<string16> GetBubbleViewMessages() OVERRIDE;
  virtual string16 GetBubbleViewAcceptButtonLabel() OVERRIDE;
  virtual string16 GetBubbleViewCancelButtonLabel() OVERRIDE;
  virtual void OnBubbleViewDidClose(Browser* browser) OVERRIDE;
  virtual void BubbleViewAcceptButtonPressed(Browser* browser) OVERRIDE;
  virtual void BubbleViewCancelButtonPressed(Browser* browser) OVERRIDE;

  static int GetMenuItemCommandID();

 private:
  ExtensionWarningBadgeService* badge_service_;

  DISALLOW_COPY_AND_ASSIGN(ErrorBadge);
};

ErrorBadge::ErrorBadge(ExtensionWarningBadgeService* badge_service)
    : badge_service_(badge_service) {}

ErrorBadge::~ErrorBadge() {}

bool ErrorBadge::HasMenuItem() {
  return true;
}

int ErrorBadge::MenuItemCommandID() {
  return GetMenuItemCommandID();
}

string16 ErrorBadge::MenuItemLabel() {
  return l10n_util::GetStringUTF16(IDS_EXTENSION_WARNINGS_WRENCH_MENU_ITEM);
}

void ErrorBadge::ExecuteMenuItem(Browser* browser) {
  // Suppress all current warnings in the extension service from triggering
  // a badge on the wrench menu in the future of this session.
  badge_service_->SuppressCurrentWarnings();

  chrome::ExecuteCommand(browser, IDC_MANAGE_EXTENSIONS);
}

bool ErrorBadge::HasBubbleView() {
  return false;
}

string16 ErrorBadge::GetBubbleViewTitle() {
  return string16();
}

std::vector<string16> ErrorBadge::GetBubbleViewMessages() {
  return std::vector<string16>();
}

string16 ErrorBadge::GetBubbleViewAcceptButtonLabel() {
  return string16();
}

string16 ErrorBadge::GetBubbleViewCancelButtonLabel() {
  return string16();
}

void ErrorBadge::OnBubbleViewDidClose(Browser* browser) {
}

void ErrorBadge::BubbleViewAcceptButtonPressed(Browser* browser) {
  NOTREACHED();
}

void ErrorBadge::BubbleViewCancelButtonPressed(Browser* browser) {
  NOTREACHED();
}

// static
int ErrorBadge::GetMenuItemCommandID() {
  return IDC_EXTENSION_ERRORS;
}

}  // namespace


ExtensionWarningBadgeService::ExtensionWarningBadgeService(Profile* profile)
    : profile_(profile) {
  DCHECK(CalledOnValidThread());
}

ExtensionWarningBadgeService::~ExtensionWarningBadgeService() {}

void ExtensionWarningBadgeService::SuppressCurrentWarnings() {
  DCHECK(CalledOnValidThread());
  size_t old_size = suppressed_warnings_.size();

  const ExtensionWarningSet& warnings = GetCurrentWarnings();
  suppressed_warnings_.insert(warnings.begin(), warnings.end());

  if (old_size != suppressed_warnings_.size())
    UpdateBadgeStatus();
}

const ExtensionWarningSet&
ExtensionWarningBadgeService::GetCurrentWarnings() const {
  return ExtensionSystem::Get(profile_)->warning_service()->warnings();
}

void ExtensionWarningBadgeService::ExtensionWarningsChanged() {
  DCHECK(CalledOnValidThread());
  UpdateBadgeStatus();
}

void ExtensionWarningBadgeService::UpdateBadgeStatus() {
  const std::set<ExtensionWarning>& warnings = GetCurrentWarnings();
  bool non_suppressed_warnings_exist = false;
  for (std::set<ExtensionWarning>::const_iterator i = warnings.begin();
       i != warnings.end(); ++i) {
    if (!ContainsKey(suppressed_warnings_, *i)) {
      non_suppressed_warnings_exist = true;
      break;
    }
  }
  ShowBadge(non_suppressed_warnings_exist);
}

void ExtensionWarningBadgeService::ShowBadge(bool show) {
  GlobalErrorService* service =
      GlobalErrorServiceFactory::GetForProfile(profile_);
  GlobalError* error = service->GetGlobalErrorByMenuItemCommandID(
      ErrorBadge::GetMenuItemCommandID());

  // Activate or hide the warning badge in case the current state is incorrect.
  if (error && !show) {
    service->RemoveGlobalError(error);
    delete error;
  } else if (!error && show) {
    service->AddGlobalError(new ErrorBadge(this));
  }
}

}  // extensions
