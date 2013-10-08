// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_APP_MODE_KIOSK_APP_MANAGER_OBSERVER_H_
#define CHROME_BROWSER_CHROMEOS_APP_MODE_KIOSK_APP_MANAGER_OBSERVER_H_

#include <string>

namespace chromeos {

class KioskAppManagerObserver {
 public:
  // Invoked when the app data is changed or loading state is changed.
  virtual void OnKioskAppDataChanged(const std::string& app_id) {}

  // Invoked when failed to load web store data of an app.
  virtual void OnKioskAppDataLoadFailure(const std::string& app_id) {}

  // Invoked when the Kiosk Apps configuration changes.
  virtual void OnKioskAppsSettingsChanged() {}

 protected:
  virtual ~KioskAppManagerObserver() {}
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_APP_MODE_KIOSK_APP_MANAGER_OBSERVER_H_
