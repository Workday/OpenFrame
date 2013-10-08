// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_NETWORK_SCREEN_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_NETWORK_SCREEN_HANDLER_H_

#include <string>

#include "base/compiler_specific.h"
#include "chrome/browser/chromeos/login/screens/network_screen_actor.h"
#include "chrome/browser/ui/webui/chromeos/login/base_screen_handler.h"
#include "ui/gfx/point.h"

namespace chromeos {

class CoreOobeActor;

// WebUI implementation of NetworkScreenActor. It is used to interact with
// the welcome screen (part of the page) of the OOBE.
class NetworkScreenHandler : public NetworkScreenActor,
                             public BaseScreenHandler {
 public:
  explicit NetworkScreenHandler(CoreOobeActor* core_oobe_actor);
  virtual ~NetworkScreenHandler();

  // NetworkScreenActor implementation:
  virtual void SetDelegate(NetworkScreenActor::Delegate* screen) OVERRIDE;
  virtual void PrepareToShow() OVERRIDE;
  virtual void Show() OVERRIDE;
  virtual void Hide() OVERRIDE;
  virtual void ShowError(const string16& message) OVERRIDE;
  virtual void ClearErrors() OVERRIDE;
  virtual void ShowConnectingStatus(bool connecting,
                                    const string16& network_id) OVERRIDE;
  virtual void EnableContinue(bool enabled) OVERRIDE;

  // BaseScreenHandler implementation:
  virtual void DeclareLocalizedValues(LocalizedValuesBuilder* builder) OVERRIDE;
  virtual void GetAdditionalParameters(base::DictionaryValue* dict) OVERRIDE;
  virtual void Initialize() OVERRIDE;

  // WebUIMessageHandler implementation:
  virtual void RegisterMessages() OVERRIDE;

 private:
  // Handles moving off the screen.
  void HandleOnExit();

  // Handles change of the language.
  void HandleOnLanguageChanged(const std::string& locale);

  // Handles change of the input method.
  void HandleOnInputMethodChanged(const std::string& id);

  // Returns available languages. Caller gets the ownership. Note, it does
  // depend on the current locale.
  static base::ListValue* GetLanguageList();

  // Returns available input methods. Caller gets the ownership. Note, it does
  // depend on the current locale.
  static base::ListValue* GetInputMethods();

  NetworkScreenActor::Delegate* screen_;
  CoreOobeActor* core_oobe_actor_;

  bool is_continue_enabled_;

  // Keeps whether screen should be shown right after initialization.
  bool show_on_init_;

  // Position of the network control.
  gfx::Point network_control_pos_;

  DISALLOW_COPY_AND_ASSIGN(NetworkScreenHandler);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_NETWORK_SCREEN_HANDLER_H_
