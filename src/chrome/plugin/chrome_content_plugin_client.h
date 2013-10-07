// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_PLUGIN_CHROME_CONTENT_PLUGIN_CLIENT_H_
#define CHROME_PLUGIN_CHROME_CONTENT_PLUGIN_CLIENT_H_

#include "base/compiler_specific.h"
#include "content/public/plugin/content_plugin_client.h"

namespace chrome {

class ChromeContentPluginClient : public content::ContentPluginClient {
 public:
  virtual void PreSandboxInitialization() OVERRIDE;
  virtual void PluginProcessStarted(const string16& plugin_name) OVERRIDE;
};

}  // namespace chrome

#endif  // CHROME_PLUGIN_CHROME_CONTENT_PLUGIN_CLIENT_H_
