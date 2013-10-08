// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRINTING_CLOUD_PRINT_CLOUD_PRINT_PROXY_SERVICE_FACTORY_H_
#define CHROME_BROWSER_PRINTING_CLOUD_PRINT_CLOUD_PRINT_PROXY_SERVICE_FACTORY_H_

#include "base/compiler_specific.h"
#include "base/memory/singleton.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"

class CloudPrintProxyService;
class Profile;

// Singleton that owns all CloudPrintProxyServices and associates them with
// Profiles. Listens for the Profile's destruction notification and cleans up
// the associated CloudPrintProxyService.
class CloudPrintProxyServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  // Returns the CloudPrintProxyService for |profile|, creating if not yet
  // created.
  static CloudPrintProxyService* GetForProfile(Profile* profile);

  static CloudPrintProxyServiceFactory* GetInstance();

 private:
  friend struct DefaultSingletonTraits<CloudPrintProxyServiceFactory>;

  CloudPrintProxyServiceFactory();
  virtual ~CloudPrintProxyServiceFactory();

  // BrowserContextKeyedServiceFactory:
  virtual BrowserContextKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const OVERRIDE;
  virtual bool ServiceIsNULLWhileTesting() const OVERRIDE;
};

#endif  // CHROME_BROWSER_PRINTING_CLOUD_PRINT_CLOUD_PRINT_PROXY_SERVICE_FACTORY_H_
