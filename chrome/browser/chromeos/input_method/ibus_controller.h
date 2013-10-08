// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_INPUT_METHOD_IBUS_CONTROLLER_H_
#define CHROME_BROWSER_CHROMEOS_INPUT_METHOD_IBUS_CONTROLLER_H_

#include <string>
#include <utility>
#include <vector>

namespace chromeos {
namespace input_method {

struct InputMethodConfigValue;
struct InputMethodProperty;
typedef std::vector<InputMethodProperty> InputMethodPropertyList;

// IBusController is used to interact with the system input method framework
// (which is currently IBus).
// TODO(nona): Split this class into IBusPropertyHandler and IBusConfigHandler.
//             Note: IBusConfig will be deprecated once ibus-daemon is gone.
class IBusController {
 public:
  class Observer {
   public:
    virtual ~Observer() {}
    virtual void PropertyChanged() = 0;
    // TODO(yusukes): Add functions for IPC error handling.
  };

  // Creates an instance of the class.
  static IBusController* Create();

  virtual ~IBusController();

  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;

  virtual void ClearProperties() = 0;

  // Sets a configuration of an input method engine. Returns true if the
  // configuration is successfully set. For example, when you set
  // "engine/Mozc/history_learning_level", |section| should be "engine/Mozc",
  // and |config_name| should be "history_learning_level".
  virtual bool SetInputMethodConfig(const std::string& section,
                                    const std::string& config_name,
                                    const InputMethodConfigValue& value) = 0;

  // Activates the input method property specified by the |key|. Returns true on
  // success.
  virtual bool ActivateInputMethodProperty(const std::string& key) = 0;

  // Gets the latest input method property send from the system input method
  // framework.
  virtual const InputMethodPropertyList& GetCurrentProperties() const = 0;
};

}  // namespace input_method
}  // namespace chromeos

// TODO(yusukes,nona): This interface does not depend on IBus actually.
// Rename the file if needed.

#endif  // CHROME_BROWSER_CHROMEOS_INPUT_METHOD_IBUS_CONTROLLER_H_
