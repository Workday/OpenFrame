// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_TABS_ASH_PANEL_CONTENTS_H_
#define CHROME_BROWSER_EXTENSIONS_API_TABS_ASH_PANEL_CONTENTS_H_

#include <vector>

#include "apps/shell_window.h"
#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/extensions/extension_function_dispatcher.h"
#include "chrome/browser/ui/ash/launcher/launcher_favicon_loader.h"
#include "content/public/browser/web_contents_observer.h"

class AshPanelWindowController;
class GURL;

namespace content {
class RenderViewHost;
}

namespace extensions {
struct DraggableRegion;
}

// apps::ShellWindowContents class specific to panel windows created by v1
// extenstions. This class maintains a WebContents instance and observes it for
// the purpose of passing messages to the extensions system. It also creates
// an extensions::WindowController instance for interfacing with the v1
// extensions API.
class AshPanelContents : public apps::ShellWindowContents,
                         public content::WebContentsObserver,
                         public LauncherFaviconLoader::Delegate,
                         public ExtensionFunctionDispatcher::Delegate {
 public:
  explicit AshPanelContents(apps::ShellWindow* host);
  virtual ~AshPanelContents();

  // apps::ShellWindowContents
  virtual void Initialize(Profile* profile, const GURL& url) OVERRIDE;
  virtual void LoadContents(int32 creator_process_id) OVERRIDE;
  virtual void NativeWindowChanged(apps::NativeAppWindow* native_app_window)
      OVERRIDE;
  virtual void NativeWindowClosed() OVERRIDE;
  virtual content::WebContents* GetWebContents() const OVERRIDE;

  // LauncherFaviconLoader::Delegate overrides:
  virtual void FaviconUpdated() OVERRIDE;

  // ExtensionFunctionDispatcher::Delegate
  virtual extensions::WindowController* GetExtensionWindowController() const
      OVERRIDE;
  virtual content::WebContents* GetAssociatedWebContents() const OVERRIDE;

  LauncherFaviconLoader* launcher_favicon_loader_for_test() {
    return launcher_favicon_loader_.get();
  }

 private:
  // content::WebContentsObserver
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

  void OnRequest(const ExtensionHostMsg_Request_Params& params);

  apps::ShellWindow* host_;
  GURL url_;
  scoped_ptr<content::WebContents> web_contents_;
  scoped_ptr<ExtensionFunctionDispatcher> extension_function_dispatcher_;
  scoped_ptr<AshPanelWindowController> window_controller_;
  scoped_ptr<LauncherFaviconLoader> launcher_favicon_loader_;

  DISALLOW_COPY_AND_ASSIGN(AshPanelContents);
};

#endif  // CHROME_BROWSER_EXTENSIONS_API_TABS_ASH_PANEL_CONTENTS_H_
