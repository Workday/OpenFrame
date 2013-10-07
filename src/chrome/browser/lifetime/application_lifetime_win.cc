// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/lifetime/application_lifetime.h"

#include "ui/views/widget/widget.h"

namespace chrome {

void HandleAppExitingForPlatform() {
  views::Widget::CloseAllSecondaryWidgets();
}

}  // namespace chrome
