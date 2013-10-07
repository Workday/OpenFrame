// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/android/extensions/extension_install_ui_android.h"

#include "base/logging.h"
#include "chrome/browser/extensions/extension_install_prompt.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"

void ExtensionInstallUIAndroid::OnInstallSuccess(
    const extensions::Extension* extension, SkBitmap* icon) {
  NOTIMPLEMENTED();
}

void ExtensionInstallUIAndroid::OnInstallFailure(
    const extensions::CrxInstallerError& error) {
  NOTIMPLEMENTED();
}

void ExtensionInstallUIAndroid::SetSkipPostInstallUI(bool skip_ui) {
  NOTIMPLEMENTED();
}

// static
ExtensionInstallUI* ExtensionInstallUI::Create(Profile* profile) {
  NOTIMPLEMENTED();
  return NULL;
}

// static
void ExtensionInstallUI::OpenAppInstalledUI(
    Profile* profile, const std::string& app_id) {
  NOTIMPLEMENTED();
}

// static
void ExtensionInstallUI::DisableFailureUIForTests() {
  NOTIMPLEMENTED();
}

// static
ExtensionInstallPrompt* ExtensionInstallUI::CreateInstallPromptWithBrowser(
    Browser* browser) {
  NOTIMPLEMENTED();
  return NULL;
}

// static
ExtensionInstallPrompt* ExtensionInstallUI::CreateInstallPromptWithProfile(
    Profile* profile) {
  NOTIMPLEMENTED();
  return NULL;
}
