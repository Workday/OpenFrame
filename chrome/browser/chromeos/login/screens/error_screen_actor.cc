// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/screens/error_screen_actor.h"

namespace chromeos {

ErrorScreenActor::ErrorScreenActor()
    : ui_state_(ErrorScreen::UI_STATE_UNKNOWN),
      error_state_(ErrorScreen::ERROR_STATE_UNKNOWN),
      parent_screen_(OobeUI::SCREEN_UNKNOWN) {
}

ErrorScreenActor::~ErrorScreenActor() {
}

}  // namespace chromeos
