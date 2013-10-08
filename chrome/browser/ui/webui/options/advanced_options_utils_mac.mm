// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#include "chrome/browser/ui/webui/options/advanced_options_utils.h"

#include "base/logging.h"
#include "base/mac/mac_logging.h"
#include "base/mac/scoped_aedesc.h"

using content::WebContents;

namespace options {

void AdvancedOptionsUtilities::ShowNetworkProxySettings(
      WebContents* web_contents) {
  NSArray* itemsToOpen = [NSArray arrayWithObject:[NSURL fileURLWithPath:
      @"/System/Library/PreferencePanes/Network.prefPane"]];

  const char* proxyPrefCommand = "Proxies";
  base::mac::ScopedAEDesc<> openParams;
  OSStatus status = AECreateDesc('ptru',
                                 proxyPrefCommand,
                                 strlen(proxyPrefCommand),
                                 openParams.OutPointer());
  OSSTATUS_LOG_IF(ERROR, status != noErr, status)
      << "Failed to create open params";

  LSLaunchURLSpec launchSpec = { 0 };
  launchSpec.itemURLs = (CFArrayRef)itemsToOpen;
  launchSpec.passThruParams = openParams;
  launchSpec.launchFlags = kLSLaunchAsync | kLSLaunchDontAddToRecents;
  LSOpenFromURLSpec(&launchSpec, NULL);
}

void AdvancedOptionsUtilities::ShowManageSSLCertificates(
      WebContents* web_contents) {
  NSString* const kKeychainBundleId = @"com.apple.keychainaccess";
  [[NSWorkspace sharedWorkspace]
   launchAppWithBundleIdentifier:kKeychainBundleId
   options:0L
   additionalEventParamDescriptor:nil
   launchIdentifier:nil];
}

}  // namespace options
