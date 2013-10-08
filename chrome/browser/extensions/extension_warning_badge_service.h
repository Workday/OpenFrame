// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_WARNING_BADGE_SERVICE_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_WARNING_BADGE_SERVICE_H_

#include "base/compiler_specific.h"
#include "base/threading/non_thread_safe.h"
#include "chrome/browser/extensions/extension_warning_service.h"
#include "chrome/browser/extensions/extension_warning_set.h"

// TODO(battre): Rename ExtensionWarningBadgeService to WarningBadgeService.

class Profile;

namespace extensions {

// A service that is responsible for showing an extension warning badge on the
// wrench menu.
class ExtensionWarningBadgeService : public ExtensionWarningService::Observer,
                                     public base::NonThreadSafe {
 public:
  explicit ExtensionWarningBadgeService(Profile* profile);
  virtual ~ExtensionWarningBadgeService();

  // Black lists all currently active extension warnings, so that they do not
  // trigger a warning badge again for the life-time of the browsing session.
  void SuppressCurrentWarnings();

 protected:
  // Virtual for testing.
  virtual const std::set<ExtensionWarning>& GetCurrentWarnings() const;

 private:
  // Implementation of ExtensionWarningService::Observer.
  virtual void ExtensionWarningsChanged() OVERRIDE;

  void UpdateBadgeStatus();
  virtual void ShowBadge(bool show);

  Profile* profile_;

  // Warnings that do not trigger a badge on the wrench menu.
  ExtensionWarningSet suppressed_warnings_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionWarningBadgeService);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_WARNING_BADGE_SERVICE_H_
