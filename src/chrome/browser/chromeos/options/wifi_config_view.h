// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_OPTIONS_WIFI_CONFIG_VIEW_H_
#define CHROME_BROWSER_CHROMEOS_OPTIONS_WIFI_CONFIG_VIEW_H_

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string16.h"
#include "chrome/browser/chromeos/cros/cert_library.h"
#include "chrome/browser/chromeos/cros/network_property_ui_data.h"
#include "chrome/browser/chromeos/options/network_config_view.h"
#include "chromeos/network/network_state_handler_observer.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "ui/base/models/combobox_model.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/combobox/combobox_listener.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace views {
class Checkbox;
class Label;
class ToggleImageButton;
}

namespace chromeos {

class NetworkState;

namespace internal {
class EAPMethodComboboxModel;
class Phase2AuthComboboxModel;
class SecurityComboboxModel;
class ServerCACertComboboxModel;
class UserCertComboboxModel;
}

// A dialog box for showing a password textfield.
class WifiConfigView : public ChildNetworkConfigView,
                       public views::TextfieldController,
                       public views::ButtonListener,
                       public views::ComboboxListener,
                       public CertLibrary::Observer,
                       public NetworkStateHandlerObserver {
 public:
  WifiConfigView(NetworkConfigView* parent,
                 const std::string& service_path,
                 bool show_8021x);
  virtual ~WifiConfigView();

  // views::TextfieldController
  virtual void ContentsChanged(views::Textfield* sender,
                               const string16& new_contents) OVERRIDE;
  virtual bool HandleKeyEvent(views::Textfield* sender,
                              const ui::KeyEvent& key_event) OVERRIDE;

  // views::ButtonListener
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE;

  // views::ComboboxListener
  virtual void OnSelectedIndexChanged(views::Combobox* combobox) OVERRIDE;

  // CertLibrary::Observer
  virtual void OnCertificatesLoaded(bool initial_load) OVERRIDE;

  // ChildNetworkConfigView
  virtual string16 GetTitle() const OVERRIDE;
  virtual views::View* GetInitiallyFocusedView() OVERRIDE;
  virtual bool CanLogin() OVERRIDE;
  virtual bool Login() OVERRIDE;
  virtual void Cancel() OVERRIDE;
  virtual void InitFocus() OVERRIDE;

  // NetworkStateHandlerObserver
  virtual void NetworkPropertiesUpdated(const NetworkState* network) OVERRIDE;

  // Parses a WiFi UI |property| from the ONC associated with |network|. |key|
  // is the property name within the ONC WiFi dictionary.
  static void ParseWiFiUIProperty(NetworkPropertyUIData* property_ui_data,
                                  const NetworkState* network,
                                  const std::string& key);

  // Parses a WiFi EAP UI |property| from the ONC associated with |network|.
  // |key| is the property name within the ONC WiFi.EAP dictionary.
  static void ParseWiFiEAPUIProperty(NetworkPropertyUIData* property_ui_data,
                                     const NetworkState* network,
                                     const std::string& key);

 private:
  // Initializes UI.  If |show_8021x| includes 802.1x config options.
  void Init(bool show_8021x);

  // Callback to initialize fields from uncached network properties.
  void InitFromProperties(bool show_8021x,
                          const std::string& service_path,
                          const base::DictionaryValue& dictionary);

  // Get input values.
  std::string GetSsid() const;
  std::string GetPassphrase() const;
  bool GetSaveCredentials() const;
  bool GetShareNetwork(bool share_default) const;

  // Get various 802.1X EAP values from the widgets.
  std::string GetEapMethod() const;
  std::string GetEapPhase2Auth() const;
  std::string GetEapServerCaCertPEM() const;
  bool GetEapUseSystemCas() const;
  std::string GetEapClientCertPkcs11Id() const;
  std::string GetEapIdentity() const;
  std::string GetEapAnonymousIdentity() const;

  // Fill in |properties| with the appropriate values.
  void SetEapProperties(base::DictionaryValue* properties);

  // Returns true if the EAP method requires a user certificate.
  bool UserCertRequired() const;

  // Returns true if at least one user certificate is installed.
  bool HaveUserCerts() const;

  // Returns true if there is a selected user certificate and it is valid.
  bool IsUserCertValid() const;

  // Returns true if the phase 2 auth is relevant.
  bool Phase2AuthActive() const;

  // Returns whether the current configuration requires a passphrase.
  bool PassphraseActive() const;

  // Returns true if a user cert should be selected.
  bool UserCertActive() const;

  // Returns true if a CA cert selection should be allowed.
  bool CaCertActive() const;

  // Updates state of the Login button.
  void UpdateDialogButtons();

  // Enable/Disable EAP fields as appropriate based on selected EAP method.
  void RefreshEapFields();

  // Enable/Disable "share this network" checkbox.
  void RefreshShareCheckbox();

  // Updates the error text label.
  void UpdateErrorLabel();

  NetworkPropertyUIData eap_method_ui_data_;
  NetworkPropertyUIData phase_2_auth_ui_data_;
  NetworkPropertyUIData user_cert_ui_data_;
  NetworkPropertyUIData server_ca_cert_ui_data_;
  NetworkPropertyUIData identity_ui_data_;
  NetworkPropertyUIData identity_anonymous_ui_data_;
  NetworkPropertyUIData save_credentials_ui_data_;
  NetworkPropertyUIData passphrase_ui_data_;

  views::Textfield* ssid_textfield_;
  scoped_ptr<internal::EAPMethodComboboxModel> eap_method_combobox_model_;
  views::Combobox* eap_method_combobox_;
  views::Label* phase_2_auth_label_;
  scoped_ptr<internal::Phase2AuthComboboxModel> phase_2_auth_combobox_model_;
  views::Combobox* phase_2_auth_combobox_;
  views::Label* user_cert_label_;
  scoped_ptr<internal::UserCertComboboxModel> user_cert_combobox_model_;
  views::Combobox* user_cert_combobox_;
  views::Label* server_ca_cert_label_;
  scoped_ptr<internal::ServerCACertComboboxModel>
      server_ca_cert_combobox_model_;
  views::Combobox* server_ca_cert_combobox_;
  views::Label* identity_label_;
  views::Textfield* identity_textfield_;
  views::Label* identity_anonymous_label_;
  views::Textfield* identity_anonymous_textfield_;
  views::Checkbox* save_credentials_checkbox_;
  views::Checkbox* share_network_checkbox_;
  views::Label* shared_network_label_;
  scoped_ptr<internal::SecurityComboboxModel> security_combobox_model_;
  views::Combobox* security_combobox_;
  views::Label* passphrase_label_;
  views::Textfield* passphrase_textfield_;
  views::ToggleImageButton* passphrase_visible_button_;
  views::Label* error_label_;

  base::WeakPtrFactory<WifiConfigView> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(WifiConfigView);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_OPTIONS_WIFI_CONFIG_VIEW_H_
