// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/desktop_notification_service_factory.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/notifications/desktop_notification_service.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

// static
DesktopNotificationService* DesktopNotificationServiceFactory::GetForProfile(
    Profile* profile) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return static_cast<DesktopNotificationService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
DesktopNotificationServiceFactory* DesktopNotificationServiceFactory::
    GetInstance() {
  return Singleton<DesktopNotificationServiceFactory>::get();
}

DesktopNotificationServiceFactory::DesktopNotificationServiceFactory()
    : BrowserContextKeyedServiceFactory(
        "DesktopNotificationService",
        BrowserContextDependencyManager::GetInstance()) {
}

DesktopNotificationServiceFactory::~DesktopNotificationServiceFactory() {
}

BrowserContextKeyedService*
DesktopNotificationServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  DesktopNotificationService* service =
      new DesktopNotificationService(static_cast<Profile*>(profile), NULL);
  return service;
}

content::BrowserContext*
DesktopNotificationServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextOwnInstanceInIncognito(context);
}
