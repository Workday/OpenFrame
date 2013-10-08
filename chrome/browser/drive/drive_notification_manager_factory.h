// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DRIVE_DRIVE_NOTIFICATION_MANAGER_FACTORY_H_
#define CHROME_BROWSER_DRIVE_DRIVE_NOTIFICATION_MANAGER_FACTORY_H_

#include "base/memory/singleton.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"

class Profile;

namespace drive {

class DriveNotificationManager;

// Singleton that owns all DriveNotificationManager and associates them with
// profiles.
class DriveNotificationManagerFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  static DriveNotificationManager* GetForProfile(Profile* profile);

  static DriveNotificationManagerFactory* GetInstance();

 private:
  friend struct DefaultSingletonTraits<DriveNotificationManagerFactory>;

  DriveNotificationManagerFactory();
  virtual ~DriveNotificationManagerFactory();

  // BrowserContextKeyedServiceFactory implementation.
  virtual BrowserContextKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const OVERRIDE;
};

}  // namespace drive

#endif  // CHROME_BROWSER_DRIVE_DRIVE_NOTIFICATION_MANAGER_FACTORY_H_
