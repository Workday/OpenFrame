// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "extensions/common/extensions_client.h"

namespace extensions {

namespace {

ExtensionsClient* g_client = NULL;

void Initialize(ExtensionsClient* client) {
  client->RegisterManifestHandlers();
}

}  // namespace

ExtensionsClient* ExtensionsClient::Get() {
  return g_client;
}

void ExtensionsClient::Set(ExtensionsClient* client) {
  // This can happen in unit tests, where the utility thread runs in-process.
  if (g_client)
    return;
  g_client = client;
  Initialize(g_client);
}

}  // namespace extensions
