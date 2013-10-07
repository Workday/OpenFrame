// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/mock_ibus_controller.h"

#include "chromeos/ime/input_method_config.h"
#include "chromeos/ime/input_method_property.h"

namespace chromeos {
namespace input_method {

MockIBusController::MockIBusController()
    : activate_input_method_property_count_(0),
      activate_input_method_property_return_(true),
      set_input_method_config_internal_count_(0),
      set_input_method_config_internal_return_(true) {
}

MockIBusController::~MockIBusController() {
}

bool MockIBusController::ActivateInputMethodProperty(const std::string& key) {
  ++activate_input_method_property_count_;
  activate_input_method_property_key_ = key;
  return activate_input_method_property_return_;
}

bool MockIBusController::SetInputMethodConfigInternal(
    const ConfigKeyType& key,
    const InputMethodConfigValue& value) {
  ++set_input_method_config_internal_count_;
  set_input_method_config_internal_key_ = key;
  set_input_method_config_internal_value_ = value;
  return set_input_method_config_internal_return_;
}

}  // namespace input_method
}  // namespace chromeos
