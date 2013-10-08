// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_APP_MODE_KIOSK_APP_UPDATE_SERVICE_H_
#define CHROME_BROWSER_CHROMEOS_APP_MODE_KIOSK_APP_UPDATE_SERVICE_H_

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/singleton.h"
#include "base/timer/timer.h"
#include "chrome/browser/chromeos/system/automatic_reboot_manager_observer.h"
#include "chrome/browser/extensions/update_observer.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"

class Profile;

namespace chromeos {

namespace system {
class AutomaticRebootManager;
}

// This class enforces automatic restart on app and Chrome updates in app mode.
class KioskAppUpdateService : public BrowserContextKeyedService,
                              public extensions::UpdateObserver,
                              public system::AutomaticRebootManagerObserver {
 public:
  KioskAppUpdateService(
      Profile* profile,
      system::AutomaticRebootManager* automatic_reboot_manager);
  virtual ~KioskAppUpdateService();

  void set_app_id(const std::string& app_id) { app_id_ = app_id; }
  std::string get_app_id() const { return app_id_; }

 private:
  friend class KioskAppUpdateServiceTest;

  void StartAppUpdateRestartTimer();
  void ForceAppUpdateRestart();

  // BrowserContextKeyedService overrides:
  virtual void Shutdown() OVERRIDE;

  // extensions::UpdateObserver overrides:
  virtual void OnAppUpdateAvailable(const std::string& app_id) OVERRIDE;
  virtual void OnChromeUpdateAvailable() OVERRIDE {}

  // system::AutomaticRebootManagerObserver overrides:
  virtual void OnRebootScheduled(Reason reason) OVERRIDE;
  virtual void WillDestroyAutomaticRebootManager() OVERRIDE;

  Profile* profile_;
  std::string app_id_;

  // After we detect an upgrade we start a one-short timer to force restart.
  base::OneShotTimer<KioskAppUpdateService> restart_timer_;

  system::AutomaticRebootManager* automatic_reboot_manager_;  // Not owned.

  DISALLOW_COPY_AND_ASSIGN(KioskAppUpdateService);
};

// Singleton that owns all KioskAppUpdateServices and associates them with
// profiles.
class KioskAppUpdateServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  // Returns the KioskAppUpdateService for |profile|, creating it if it is not
  // yet created.
  static KioskAppUpdateService* GetForProfile(Profile* profile);

  // Returns the KioskAppUpdateServiceFactory instance.
  static KioskAppUpdateServiceFactory* GetInstance();

 private:
  friend struct DefaultSingletonTraits<KioskAppUpdateServiceFactory>;

  KioskAppUpdateServiceFactory();
  virtual ~KioskAppUpdateServiceFactory();

  // BrowserContextKeyedServiceFactory overrides:
  virtual BrowserContextKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const OVERRIDE;
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_APP_MODE_KIOSK_APP_UPDATE_SERVICE_H_
