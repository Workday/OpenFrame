// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/ibus_controller.h"

#include "chrome/browser/chromeos/input_method/ibus_controller_impl.h"

namespace chromeos {
namespace input_method {

IBusController::~IBusController() {
}

// static
IBusController* IBusController::Create() {
  return new IBusControllerImpl;
}

}  // namespace input_method
}  // namespace chromeos
