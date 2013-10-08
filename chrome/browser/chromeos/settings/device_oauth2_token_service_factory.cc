// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/settings/device_oauth2_token_service_factory.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/settings/device_oauth2_token_service.h"
#include "content/public/browser/browser_thread.h"

namespace chromeos {

static DeviceOAuth2TokenService* g_device_oauth2_token_service_ = NULL;

DeviceOAuth2TokenServiceFactory::DeviceOAuth2TokenServiceFactory() {
}

// static
DeviceOAuth2TokenService* DeviceOAuth2TokenServiceFactory::Get() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  return g_device_oauth2_token_service_;
}

// static
void DeviceOAuth2TokenServiceFactory::Initialize() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  DCHECK(!g_device_oauth2_token_service_);
  g_device_oauth2_token_service_ = new DeviceOAuth2TokenService(
      g_browser_process->system_request_context(),
      g_browser_process->local_state());
}

// static
void DeviceOAuth2TokenServiceFactory::Shutdown() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (g_device_oauth2_token_service_) {
    delete g_device_oauth2_token_service_;
    g_device_oauth2_token_service_ = NULL;
  }
}

}  // namespace chromeos
