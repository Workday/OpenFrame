// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/display/display_configuration_observer.h"

#include "ash/display/display_controller.h"
#include "ash/shell.h"
#include "chrome/browser/chromeos/display/display_preferences.h"

namespace chromeos {

DisplayConfigurationObserver::DisplayConfigurationObserver() {
  ash::Shell::GetInstance()->display_controller()->AddObserver(this);
}

DisplayConfigurationObserver::~DisplayConfigurationObserver() {
  ash::Shell::GetInstance()->display_controller()->RemoveObserver(this);
}

void DisplayConfigurationObserver::OnDisplayConfigurationChanging() {
}

void DisplayConfigurationObserver::OnDisplayConfigurationChanged() {
  StoreDisplayPrefs();
}

}  // namespace chromeos
