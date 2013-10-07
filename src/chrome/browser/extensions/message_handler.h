// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_MESSAGE_HANDLER_H_
#define CHROME_BROWSER_EXTENSIONS_MESSAGE_HANDLER_H_

#include <string>

#include "content/public/browser/render_view_host_observer.h"

namespace extensions {

// Filters and dispatches extension-related IPC messages that arrive from
// renderers. There is one of these objects for each RenderViewHost in Chrome.
// Contrast this with extensions::TabHelper, which is only created for
// WebContents.
//
// TODO(aa): Handling of content script messaging should be able to move to EFD
// once there is an EFD for every RVHD where extension code can run. Then we
// could eliminate this class. Right now, we don't end up with an EFD for tab
// contents unless that tab contents is hosting chrome-extension:// URLs. That
// still leaves content scripts. See also: crbug.com/80307.
class MessageHandler : public content::RenderViewHostObserver {
 public:
  // |sender| is guaranteed to outlive this object.
  explicit MessageHandler(content::RenderViewHost* render_view_host);
  virtual ~MessageHandler();

  // RenderViewHostObserver overrides.
  virtual void RenderViewHostInitialized() OVERRIDE;
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

 private:
  // Message handlers.
  void OnPostMessage(int port_id, const std::string& message);

  DISALLOW_COPY_AND_ASSIGN(MessageHandler);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_MESSAGE_HANDLER_H_
