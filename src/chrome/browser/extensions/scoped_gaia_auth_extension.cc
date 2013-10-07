// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/scoped_gaia_auth_extension.h"

#include "base/command_line.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "grit/browser_resources.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/system/input_device_settings.h"
#include "chromeos/chromeos_constants.h"
#include "chromeos/chromeos_switches.h"
#endif

namespace {

extensions::ComponentLoader* GetComponentLoader(Profile* profile) {
  extensions::ExtensionSystem* extension_system =
      extensions::ExtensionSystem::Get(profile);
  ExtensionService* extension_service = extension_system->extension_service();
  return extension_service->component_loader();
}

void LoadGaiaAuthExtension(Profile* profile) {
  extensions::ComponentLoader* component_loader = GetComponentLoader(profile);
  const CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kAuthExtensionPath)) {
    base::FilePath auth_extension_path =
        command_line->GetSwitchValuePath(switches::kAuthExtensionPath);
    component_loader->Add(IDR_GAIA_AUTH_MANIFEST, auth_extension_path);
    return;
  }

  bool force_keyboard_oobe = false;
#if defined(OS_CHROMEOS)
  force_keyboard_oobe =
      chromeos::system::keyboard_settings::ForceKeyboardDrivenUINavigation();
#endif // OS_CHROMEOS
  if (force_keyboard_oobe) {
    component_loader->Add(IDR_GAIA_AUTH_KEYBOARD_MANIFEST,
                          base::FilePath(FILE_PATH_LITERAL("gaia_auth")));
  } else {
    component_loader->Add(IDR_GAIA_AUTH_MANIFEST,
                          base::FilePath(FILE_PATH_LITERAL("gaia_auth")));
  }
}

void UnloadGaiaAuthExtension(Profile* profile) {
  const char kGaiaAuthId[] = "mfffpogegjflfpflabcdkioaeobkgjik";
  GetComponentLoader(profile)->Remove(kGaiaAuthId);
}

}  // namespace

ScopedGaiaAuthExtension::ScopedGaiaAuthExtension(Profile* profile)
    : profile_(profile) {
  LoadGaiaAuthExtension(profile_);
}

ScopedGaiaAuthExtension::~ScopedGaiaAuthExtension() {
  UnloadGaiaAuthExtension(profile_);
}
