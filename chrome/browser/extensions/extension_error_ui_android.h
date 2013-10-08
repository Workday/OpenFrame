// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_ERROR_UI_ANDROID_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_ERROR_UI_ANDROID_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/extensions/extension_error_ui.h"

class ExtensionService;

class ExtensionErrorUIAndroid : public ExtensionErrorUI {
 public:
  explicit ExtensionErrorUIAndroid(ExtensionService* extension_service);
  virtual ~ExtensionErrorUIAndroid();

  // ExtensionErrorUI implementation:
  virtual bool ShowErrorInBubbleView() OVERRIDE;
  virtual void ShowExtensions() OVERRIDE;
  virtual void Close() OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(ExtensionErrorUIAndroid);
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_ERROR_UI_ANDROID_H_
