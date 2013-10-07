// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CUSTOM_HANDLERS_PROTOCOL_HANDLER_REGISTRY_FACTORY_H_
#define CHROME_BROWSER_CUSTOM_HANDLERS_PROTOCOL_HANDLER_REGISTRY_FACTORY_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"

class Profile;
class ProtocolHandlerRegistry;
template <typename T> struct DefaultSingletonTraits;

// Singleton that owns all ProtocolHandlerRegistrys and associates them with
// Profiles. Listens for the Profile's destruction notification and cleans up
// the associated ProtocolHandlerRegistry.
class ProtocolHandlerRegistryFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  // Returns the singleton instance of the ProtocolHandlerRegistryFactory.
  static ProtocolHandlerRegistryFactory* GetInstance();

  // Returns the ProtocolHandlerRegistry that provides intent registration for
  // |profile|. Ownership stays with this factory object.
  static ProtocolHandlerRegistry* GetForProfile(Profile* profile);

 protected:
  // BrowserContextKeyedServiceFactory implementation.
  virtual bool ServiceIsCreatedWithBrowserContext() const OVERRIDE;
  virtual content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const OVERRIDE;
  virtual bool ServiceIsNULLWhileTesting() const OVERRIDE;

 private:
  friend struct DefaultSingletonTraits<ProtocolHandlerRegistryFactory>;

  ProtocolHandlerRegistryFactory();
  virtual ~ProtocolHandlerRegistryFactory();

  // BrowserContextKeyedServiceFactory implementation.
  virtual BrowserContextKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(ProtocolHandlerRegistryFactory);
};

#endif  // CHROME_BROWSER_CUSTOM_HANDLERS_PROTOCOL_HANDLER_REGISTRY_FACTORY_H_
