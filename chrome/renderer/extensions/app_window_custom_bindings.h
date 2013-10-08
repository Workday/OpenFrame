// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_EXTENSIONS_APP_WINDOW_CUSTOM_BINDINGS_H_
#define CHROME_RENDERER_EXTENSIONS_APP_WINDOW_CUSTOM_BINDINGS_H_

#include "chrome/renderer/extensions/chrome_v8_extension.h"

namespace extensions {
class Dispatcher;

// Implements custom bindings for the app.window API.
class AppWindowCustomBindings : public ChromeV8Extension {
 public:
  AppWindowCustomBindings(Dispatcher* dispatcher, ChromeV8Context* context);

 private:
  void GetView(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Return string containing the HTML <template> for the <window-controls>
  // custom element.
  void GetWindowControlsHtmlTemplate(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  DISALLOW_COPY_AND_ASSIGN(AppWindowCustomBindings);
};

}  // namespace extensions

#endif  // CHROME_RENDERER_EXTENSIONS_APP_WINDOW_CUSTOM_BINDINGS_H_
