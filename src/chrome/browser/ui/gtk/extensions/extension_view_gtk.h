// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_EXTENSIONS_EXTENSION_VIEW_GTK_H_
#define CHROME_BROWSER_UI_GTK_EXTENSIONS_EXTENSION_VIEW_GTK_H_

#include "base/basictypes.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/size.h"

class Browser;
class SkBitmap;

namespace content {
class RenderViewHost;
}

namespace extensions {
class ExtensionHost;
}

class ExtensionViewGtk {
 public:
  ExtensionViewGtk(extensions::ExtensionHost* extension_host, Browser* browser);

  class Container {
   public:
    virtual ~Container() {}
    virtual void OnExtensionSizeChanged(ExtensionViewGtk* view,
                                        const gfx::Size& new_size) {}
  };

  void Init();

  gfx::NativeView native_view();
  Browser* browser() const { return browser_; }

  void SetBackground(const SkBitmap& background);

  // Sets the container for this view.
  void SetContainer(Container* container) { container_ = container; }

  // Method for the ExtensionHost to notify us about the correct size for
  // extension contents.
  void ResizeDueToAutoResize(const gfx::Size& new_size);

  // Method for the ExtensionHost to notify us when the RenderViewHost has a
  // connection.
  void RenderViewCreated();

  content::RenderViewHost* render_view_host() const;

 private:
  void CreateWidgetHostView();

  Browser* browser_;

  extensions::ExtensionHost* extension_host_;

  // The background the view should have once it is initialized. This is set
  // when the view has a custom background, but hasn't been initialized yet.
  SkBitmap pending_background_;

  // This view's container.
  Container* container_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionViewGtk);
};

#endif  // CHROME_BROWSER_UI_GTK_EXTENSIONS_EXTENSION_VIEW_GTK_H_
