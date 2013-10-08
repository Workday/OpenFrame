// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_EXTENSIONS_FEEDBACK_PRIVATE_CUSTOM_BINDINGS_H_
#define CHROME_RENDERER_EXTENSIONS_FEEDBACK_PRIVATE_CUSTOM_BINDINGS_H_

#include "chrome/renderer/extensions/chrome_v8_extension.h"

namespace extensions {

// The native component of custom bindings for the chrome.app.runtime API.
class FeedbackPrivateCustomBindings : public ChromeV8Extension {
 public:
  FeedbackPrivateCustomBindings(Dispatcher* dispatcher,
                                ChromeV8Context* context);

 private:
  DISALLOW_COPY_AND_ASSIGN(FeedbackPrivateCustomBindings);
};

}  // namespace extensions

#endif  // CHROME_RENDERER_EXTENSIONS_FEEDBACK_PRIVATE_CUSTOM_BINDINGS_H_
