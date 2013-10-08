// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/options/wifi_config_view.h"

#include "ash/system/chromeos/network/network_connect.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chromeos/enrollment_dialog_view.h"
#include "chrome/browser/chromeos/options/network_connect.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chromeos/login/login_state.h"
#include "chromeos/network/network_configuration_handler.h"
#include "chromeos/network/network_event_log.h"
#include "chromeos/network/network_handler.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/network/network_ui_data.h"
#include "chromeos/network/onc/onc_constants.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "ui/base/events/event.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/layout/layout_constants.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_client_view.h"

namespace chromeos {

namespace {

// Combobox that supports a preferred width.  Used by Server CA combobox
// because the strings inside it are too wide.
class ComboboxWithWidth : public views::Combobox {
 public:
  ComboboxWithWidth(ui::ComboboxModel* model, int width)
      : Combobox(model),
        width_(width) {
  }
  virtual ~ComboboxWithWidth() {}
  virtual gfx::Size GetPreferredSize() OVERRIDE {
    gfx::Size size = Combobox::GetPreferredSize();
    size.set_width(width_);
    return size;
  }
 private:
  int width_;
  DISALLOW_COPY_AND_ASSIGN(ComboboxWithWidth);
};

enum SecurityComboboxIndex {
  SECURITY_INDEX_NONE  = 0,
  SECURITY_INDEX_WEP   = 1,
  SECURITY_INDEX_PSK   = 2,
  SECURITY_INDEX_COUNT = 3
};

// Methods in alphabetical order.
enum EAPMethodComboboxIndex {
  EAP_METHOD_INDEX_NONE  = 0,
  EAP_METHOD_INDEX_LEAP  = 1,
  EAP_METHOD_INDEX_PEAP  = 2,
  EAP_METHOD_INDEX_TLS   = 3,
  EAP_METHOD_INDEX_TTLS  = 4,
  EAP_METHOD_INDEX_COUNT = 5
};

enum Phase2AuthComboboxIndex {
  PHASE_2_AUTH_INDEX_AUTO     = 0,  // LEAP, EAP-TLS have only this auth.
  PHASE_2_AUTH_INDEX_MD5      = 1,
  PHASE_2_AUTH_INDEX_MSCHAPV2 = 2,  // PEAP has up to this auth.
  PHASE_2_AUTH_INDEX_MSCHAP   = 3,
  PHASE_2_AUTH_INDEX_PAP      = 4,
  PHASE_2_AUTH_INDEX_CHAP     = 5,  // EAP-TTLS has up to this auth.
  PHASE_2_AUTH_INDEX_COUNT    = 6
};

void ShillError(const std::string& function,
                const std::string& error_name,
                scoped_ptr<base::DictionaryValue> error_data) {
  NET_LOG_ERROR("Shill Error from WifiConfigView: " + error_name, function);
}

}  // namespace

namespace internal {

class SecurityComboboxModel : public ui::ComboboxModel {
 public:
  SecurityComboboxModel();
  virtual ~SecurityComboboxModel();

  // Overridden from ui::ComboboxModel:
  virtual int GetItemCount() const OVERRIDE;
  virtual string16 GetItemAt(int index) OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(SecurityComboboxModel);
};

class EAPMethodComboboxModel : public ui::ComboboxModel {
 public:
  EAPMethodComboboxModel();
  virtual ~EAPMethodComboboxModel();

  // Overridden from ui::ComboboxModel:
  virtual int GetItemCount() const OVERRIDE;
  virtual string16 GetItemAt(int index) OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(EAPMethodComboboxModel);
};

class Phase2AuthComboboxModel : public ui::ComboboxModel {
 public:
  explicit Phase2AuthComboboxModel(views::Combobox* eap_method_combobox);
  virtual ~Phase2AuthComboboxModel();

  // Overridden from ui::ComboboxModel:
  virtual int GetItemCount() const OVERRIDE;
  virtual string16 GetItemAt(int index) OVERRIDE;

 private:
  views::Combobox* eap_method_combobox_;

  DISALLOW_COPY_AND_ASSIGN(Phase2AuthComboboxModel);
};

class ServerCACertComboboxModel : public ui::ComboboxModel {
 public:
  ServerCACertComboboxModel();
  virtual ~ServerCACertComboboxModel();

  // Overridden from ui::ComboboxModel:
  virtual int GetItemCount() const OVERRIDE;
  virtual string16 GetItemAt(int index) OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(ServerCACertComboboxModel);
};

class UserCertComboboxModel : public ui::ComboboxModel {
 public:
  UserCertComboboxModel();
  virtual ~UserCertComboboxModel();

  // Overridden from ui::ComboboxModel:
  virtual int GetItemCount() const OVERRIDE;
  virtual string16 GetItemAt(int index) OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(UserCertComboboxModel);
};

// SecurityComboboxModel -------------------------------------------------------

SecurityComboboxModel::SecurityComboboxModel() {
}

SecurityComboboxModel::~SecurityComboboxModel() {
}

int SecurityComboboxModel::GetItemCount() const {
    return SECURITY_INDEX_COUNT;
  }
string16 SecurityComboboxModel::GetItemAt(int index) {
  if (index == SECURITY_INDEX_NONE)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_NONE);
  else if (index == SECURITY_INDEX_WEP)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_WEP);
  else if (index == SECURITY_INDEX_PSK)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_PSK);
  NOTREACHED();
  return string16();
}

// EAPMethodComboboxModel ------------------------------------------------------

EAPMethodComboboxModel::EAPMethodComboboxModel() {
}

EAPMethodComboboxModel::~EAPMethodComboboxModel() {
}

int EAPMethodComboboxModel::GetItemCount() const {
  return EAP_METHOD_INDEX_COUNT;
}
string16 EAPMethodComboboxModel::GetItemAt(int index) {
  if (index == EAP_METHOD_INDEX_NONE)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_EAP_METHOD_NONE);
  else if (index == EAP_METHOD_INDEX_LEAP)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_EAP_METHOD_LEAP);
  else if (index == EAP_METHOD_INDEX_PEAP)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_EAP_METHOD_PEAP);
  else if (index == EAP_METHOD_INDEX_TLS)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_EAP_METHOD_TLS);
  else if (index == EAP_METHOD_INDEX_TTLS)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_EAP_METHOD_TTLS);
  NOTREACHED();
  return string16();
}

// Phase2AuthComboboxModel -----------------------------------------------------

Phase2AuthComboboxModel::Phase2AuthComboboxModel(
    views::Combobox* eap_method_combobox)
    : eap_method_combobox_(eap_method_combobox) {
}

Phase2AuthComboboxModel::~Phase2AuthComboboxModel() {
}

int Phase2AuthComboboxModel::GetItemCount() const {
  switch (eap_method_combobox_->selected_index()) {
    case EAP_METHOD_INDEX_NONE:
    case EAP_METHOD_INDEX_TLS:
    case EAP_METHOD_INDEX_LEAP:
      return PHASE_2_AUTH_INDEX_AUTO + 1;
    case EAP_METHOD_INDEX_PEAP:
      return PHASE_2_AUTH_INDEX_MSCHAPV2 + 1;
    case EAP_METHOD_INDEX_TTLS:
      return PHASE_2_AUTH_INDEX_CHAP + 1;
  }
  NOTREACHED();
  return 0;
}

string16 Phase2AuthComboboxModel::GetItemAt(int index) {
  if (index == PHASE_2_AUTH_INDEX_AUTO)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PHASE_2_AUTH_AUTO);
  else if (index == PHASE_2_AUTH_INDEX_MD5)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PHASE_2_AUTH_MD5);
  else if (index == PHASE_2_AUTH_INDEX_MSCHAPV2)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PHASE_2_AUTH_MSCHAPV2);
  else if (index == PHASE_2_AUTH_INDEX_MSCHAP)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PHASE_2_AUTH_MSCHAP);
  else if (index == PHASE_2_AUTH_INDEX_PAP)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PHASE_2_AUTH_PAP);
  else if (index == PHASE_2_AUTH_INDEX_CHAP)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PHASE_2_AUTH_CHAP);
  NOTREACHED();
  return string16();
}

// ServerCACertComboboxModel ---------------------------------------------------

ServerCACertComboboxModel::ServerCACertComboboxModel() {
}

ServerCACertComboboxModel::~ServerCACertComboboxModel() {
}

int ServerCACertComboboxModel::GetItemCount() const {
  if (CertLibrary::Get()->CertificatesLoading())
    return 1;  // "Loading"
  // First "Default", then the certs, then "Do not check".
  return CertLibrary::Get()->NumCertificates(
      CertLibrary::CERT_TYPE_SERVER_CA) + 2;
}

string16 ServerCACertComboboxModel::GetItemAt(int index) {
  if (CertLibrary::Get()->CertificatesLoading())
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_LOADING);
  if (index == 0)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_SERVER_CA_DEFAULT);
  if (index == GetItemCount() - 1)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_SERVER_CA_DO_NOT_CHECK);
  int cert_index = index - 1;
  return CertLibrary::Get()->GetCertDisplayStringAt(
      CertLibrary::CERT_TYPE_SERVER_CA, cert_index);
}

// UserCertComboboxModel -------------------------------------------------------

UserCertComboboxModel::UserCertComboboxModel() {
}

UserCertComboboxModel::~UserCertComboboxModel() {
}

int UserCertComboboxModel::GetItemCount() const {
  if (CertLibrary::Get()->CertificatesLoading())
    return 1;  // "Loading"
  int num_certs =
      CertLibrary::Get()->NumCertificates(CertLibrary::CERT_TYPE_USER);
  if (num_certs == 0)
    return 1;  // "None installed"
  return num_certs;
}

string16 UserCertComboboxModel::GetItemAt(int index) {
  if (CertLibrary::Get()->CertificatesLoading())
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_LOADING);
  if (CertLibrary::Get()->NumCertificates(CertLibrary::CERT_TYPE_USER) == 0)
    return l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_USER_CERT_NONE_INSTALLED);
  return CertLibrary::Get()->GetCertDisplayStringAt(
      CertLibrary::CERT_TYPE_USER, index);
}

}  // namespace internal

WifiConfigView::WifiConfigView(NetworkConfigView* parent,
                               const std::string& service_path,
                               bool show_8021x)
    : ChildNetworkConfigView(parent, service_path),
      ssid_textfield_(NULL),
      eap_method_combobox_(NULL),
      phase_2_auth_label_(NULL),
      phase_2_auth_combobox_(NULL),
      user_cert_label_(NULL),
      user_cert_combobox_(NULL),
      server_ca_cert_label_(NULL),
      server_ca_cert_combobox_(NULL),
      identity_label_(NULL),
      identity_textfield_(NULL),
      identity_anonymous_label_(NULL),
      identity_anonymous_textfield_(NULL),
      save_credentials_checkbox_(NULL),
      share_network_checkbox_(NULL),
      shared_network_label_(NULL),
      security_combobox_(NULL),
      passphrase_label_(NULL),
      passphrase_textfield_(NULL),
      passphrase_visible_button_(NULL),
      error_label_(NULL),
      weak_ptr_factory_(this) {
  Init(show_8021x);
  NetworkHandler::Get()->network_state_handler()->AddObserver(this, FROM_HERE);
}

WifiConfigView::~WifiConfigView() {
  if (NetworkHandler::IsInitialized()) {
    NetworkHandler::Get()->network_state_handler()->RemoveObserver(
        this, FROM_HERE);
  }
  CertLibrary::Get()->RemoveObserver(this);
}

string16 WifiConfigView::GetTitle() const {
  return l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_JOIN_WIFI_NETWORKS);
}

views::View* WifiConfigView::GetInitiallyFocusedView() {
  // Return a reasonable widget for initial focus,
  // depending on what we're showing.
  if (ssid_textfield_)
    return ssid_textfield_;
  else if (eap_method_combobox_)
    return eap_method_combobox_;
  else if (passphrase_textfield_ && passphrase_textfield_->enabled())
    return passphrase_textfield_;
  else
    return NULL;
}

bool WifiConfigView::CanLogin() {
  static const size_t kMinWirelessPasswordLen = 5;

  // We either have an existing wifi network or the user entered an SSID.
  if (service_path_.empty() && GetSsid().empty())
    return false;

  // If the network requires a passphrase, make sure it is the right length.
  if (passphrase_textfield_ != NULL
      && passphrase_textfield_->enabled()
      && passphrase_textfield_->text().length() < kMinWirelessPasswordLen)
    return false;

  // If we're using EAP, we must have a method.
  if (eap_method_combobox_ &&
      eap_method_combobox_->selected_index() == EAP_METHOD_INDEX_NONE)
    return false;

  // Block login if certs are required but user has none.
  if (UserCertRequired() && (!HaveUserCerts() || !IsUserCertValid()))
      return false;

  return true;
}

bool WifiConfigView::UserCertRequired() const {
  return UserCertActive();
}

bool WifiConfigView::HaveUserCerts() const {
  return CertLibrary::Get()->NumCertificates(CertLibrary::CERT_TYPE_USER) > 0;
}

bool WifiConfigView::IsUserCertValid() const {
  if (!UserCertActive())
    return false;
  int index = user_cert_combobox_->selected_index();
  if (index < 0)
    return false;
  // Currently only hardware-backed user certificates are valid.
  if (CertLibrary::Get()->IsHardwareBacked() &&
      !CertLibrary::Get()->IsCertHardwareBackedAt(
          CertLibrary::CERT_TYPE_USER, index))
    return false;
  return true;
}

bool WifiConfigView::Phase2AuthActive() const {
  if (phase_2_auth_combobox_)
    return phase_2_auth_combobox_->model()->GetItemCount() > 1;
  return false;
}

bool WifiConfigView::PassphraseActive() const {
  if (eap_method_combobox_) {
    // No password for EAP-TLS.
    int index = eap_method_combobox_->selected_index();
    return index != EAP_METHOD_INDEX_NONE && index != EAP_METHOD_INDEX_TLS;
  } else if (security_combobox_) {
    return security_combobox_->selected_index() != SECURITY_INDEX_NONE;
  }
  return false;
}

bool WifiConfigView::UserCertActive() const {
  // User certs only for EAP-TLS.
  if (eap_method_combobox_)
    return eap_method_combobox_->selected_index() == EAP_METHOD_INDEX_TLS;

  return false;
}

bool WifiConfigView::CaCertActive() const {
  // No server CA certs for LEAP.
  if (eap_method_combobox_) {
    int index = eap_method_combobox_->selected_index();
    return index != EAP_METHOD_INDEX_NONE && index != EAP_METHOD_INDEX_LEAP;
  }
  return false;
}

void WifiConfigView::UpdateDialogButtons() {
  parent_->GetDialogClientView()->UpdateDialogButtons();
}

void WifiConfigView::RefreshEapFields() {
  // If EAP method changes, the phase 2 auth choices may have changed also.
  phase_2_auth_combobox_->ModelChanged();
  phase_2_auth_combobox_->SetSelectedIndex(0);
  bool phase_2_auth_enabled = Phase2AuthActive();
  phase_2_auth_combobox_->SetEnabled(phase_2_auth_enabled &&
                                     phase_2_auth_ui_data_.IsEditable());
  phase_2_auth_label_->SetEnabled(phase_2_auth_enabled);

  // Passphrase.
  bool passphrase_enabled = PassphraseActive();
  passphrase_textfield_->SetEnabled(passphrase_enabled &&
                                    passphrase_ui_data_.IsEditable());
  passphrase_label_->SetEnabled(passphrase_enabled);
  if (!passphrase_enabled)
    passphrase_textfield_->SetText(string16());

  // User cert.
  bool certs_loading = CertLibrary::Get()->CertificatesLoading();
  bool user_cert_enabled = UserCertActive();
  user_cert_label_->SetEnabled(user_cert_enabled);
  bool have_user_certs = !certs_loading && HaveUserCerts();
  user_cert_combobox_->SetEnabled(user_cert_enabled &&
                                  have_user_certs &&
                                  user_cert_ui_data_.IsEditable());
  user_cert_combobox_->ModelChanged();
  user_cert_combobox_->SetSelectedIndex(0);

  // Server CA.
  bool ca_cert_enabled = CaCertActive();
  server_ca_cert_label_->SetEnabled(ca_cert_enabled);
  server_ca_cert_combobox_->SetEnabled(ca_cert_enabled &&
                                       !certs_loading &&
                                       server_ca_cert_ui_data_.IsEditable());
  server_ca_cert_combobox_->ModelChanged();
  server_ca_cert_combobox_->SetSelectedIndex(0);

  // No anonymous identity if no phase 2 auth.
  bool identity_anonymous_enabled = phase_2_auth_enabled;
  identity_anonymous_textfield_->SetEnabled(
      identity_anonymous_enabled && identity_anonymous_ui_data_.IsEditable());
  identity_anonymous_label_->SetEnabled(identity_anonymous_enabled);
  if (!identity_anonymous_enabled)
    identity_anonymous_textfield_->SetText(string16());

  RefreshShareCheckbox();
}

void WifiConfigView::RefreshShareCheckbox() {
  if (!share_network_checkbox_)
    return;

  if (security_combobox_ &&
      security_combobox_->selected_index() == SECURITY_INDEX_NONE) {
    share_network_checkbox_->SetEnabled(false);
    share_network_checkbox_->SetChecked(true);
  } else if (eap_method_combobox_ &&
             (eap_method_combobox_->selected_index() == EAP_METHOD_INDEX_TLS ||
              user_cert_combobox_->selected_index() != 0)) {
    // Can not share TLS network (requires certificate), or any network where
    // user certificates are enabled.
    share_network_checkbox_->SetEnabled(false);
    share_network_checkbox_->SetChecked(false);
  } else if (!LoginState::Get()->IsUserAuthenticated()) {
    // If not logged in as an authenticated user, networks must be shared.
    share_network_checkbox_->SetEnabled(false);
    share_network_checkbox_->SetChecked(true);
  } else {
    share_network_checkbox_->SetEnabled(true);
    share_network_checkbox_->SetChecked(false);  // Default to unshared.
  }
}

void WifiConfigView::UpdateErrorLabel() {
  base::string16 error_msg;
  if (UserCertRequired() && CertLibrary::Get()->CertificatesLoaded()) {
    if (!HaveUserCerts()) {
      if (!LoginState::Get()->IsUserAuthenticated()) {
        error_msg = l10n_util::GetStringUTF16(
            IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_LOGIN_FOR_USER_CERT);
      } else {
        error_msg = l10n_util::GetStringUTF16(
            IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PLEASE_INSTALL_USER_CERT);
      }
    } else if (!IsUserCertValid()) {
      error_msg = l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_REQUIRE_HARDWARE_BACKED);
    }
  }
  if (error_msg.empty() && !service_path_.empty()) {
    const NetworkState* wifi = NetworkHandler::Get()->network_state_handler()->
        GetNetworkState(service_path_);
    if (wifi && wifi->connection_state() == flimflam::kStateFailure)
      error_msg = ash::network_connect::ErrorString(wifi->error());
  }
  if (!error_msg.empty()) {
    error_label_->SetText(error_msg);
    error_label_->SetVisible(true);
  } else {
    error_label_->SetVisible(false);
  }
}

void WifiConfigView::ContentsChanged(views::Textfield* sender,
                                     const string16& new_contents) {
  UpdateDialogButtons();
}

bool WifiConfigView::HandleKeyEvent(views::Textfield* sender,
                                    const ui::KeyEvent& key_event) {
  if (sender == passphrase_textfield_ &&
      key_event.key_code() == ui::VKEY_RETURN) {
    parent_->GetDialogClientView()->AcceptWindow();
  }
  return false;
}

void WifiConfigView::ButtonPressed(views::Button* sender,
                                   const ui::Event& event) {
  if (sender == passphrase_visible_button_) {
    if (passphrase_textfield_) {
      passphrase_textfield_->SetObscured(!passphrase_textfield_->IsObscured());
      passphrase_visible_button_->SetToggled(
          !passphrase_textfield_->IsObscured());
    }
  } else {
    NOTREACHED();
  }
}

void WifiConfigView::OnSelectedIndexChanged(views::Combobox* combobox) {
  if (combobox == security_combobox_) {
    bool passphrase_enabled = PassphraseActive();
    passphrase_label_->SetEnabled(passphrase_enabled);
    passphrase_textfield_->SetEnabled(passphrase_enabled &&
                                      passphrase_ui_data_.IsEditable());
    if (!passphrase_enabled)
      passphrase_textfield_->SetText(string16());
    RefreshShareCheckbox();
  } else if (combobox == user_cert_combobox_) {
    RefreshShareCheckbox();
  } else if (combobox == eap_method_combobox_) {
    RefreshEapFields();
  }
  UpdateDialogButtons();
  UpdateErrorLabel();
}

void WifiConfigView::OnCertificatesLoaded(bool initial_load) {
  RefreshEapFields();
  UpdateDialogButtons();
  UpdateErrorLabel();
}

bool WifiConfigView::Login() {
  const bool share_default = true;
  if (service_path_.empty()) {
    // Set configuration properties.
    base::DictionaryValue properties;
    properties.SetStringWithoutPathExpansion(
        flimflam::kTypeProperty, flimflam::kTypeWifi);
    properties.SetStringWithoutPathExpansion(
        flimflam::kSSIDProperty, GetSsid());
    properties.SetStringWithoutPathExpansion(
        flimflam::kModeProperty, flimflam::kModeManaged);
    properties.SetBooleanWithoutPathExpansion(
        flimflam::kSaveCredentialsProperty, GetSaveCredentials());
    std::string security = flimflam::kSecurityNone;
    if (!eap_method_combobox_) {
      // Hidden ordinary Wi-Fi connection.
      switch (security_combobox_->selected_index()) {
        case SECURITY_INDEX_NONE:
          security = flimflam::kSecurityNone;
          break;
        case SECURITY_INDEX_WEP:
          security = flimflam::kSecurityWep;
          break;
        case SECURITY_INDEX_PSK:
          security = flimflam::kSecurityPsk;
          break;
      }
      std::string passphrase = GetPassphrase();
      if (!passphrase.empty()) {
        properties.SetStringWithoutPathExpansion(
            flimflam::kPassphraseProperty, GetPassphrase());
      }
    } else {
      // Hidden 802.1X EAP Wi-Fi connection.
      security = flimflam::kSecurity8021x;
      SetEapProperties(&properties);
    }
    properties.SetStringWithoutPathExpansion(
        flimflam::kSecurityProperty, security);

    // Configure and connect to network.
    bool shared = GetShareNetwork(share_default);
    ash::network_connect::CreateConfigurationAndConnect(&properties, shared);
  } else {
    const NetworkState* wifi = NetworkHandler::Get()->network_state_handler()->
        GetNetworkState(service_path_);
    if (!wifi) {
      // Shill no longer knows about this wifi network (edge case).
      // TODO(stevenjb): Add notification for this.
      NET_LOG_ERROR("Network not found", service_path_);
      return true;  // Close dialog
    }
    base::DictionaryValue properties;
    if (eap_method_combobox_) {
      // Visible 802.1X EAP Wi-Fi connection.
      SetEapProperties(&properties);
      properties.SetBooleanWithoutPathExpansion(
          flimflam::kSaveCredentialsProperty, GetSaveCredentials());
    } else {
      // Visible ordinary Wi-Fi connection.
      const std::string passphrase = GetPassphrase();
      if (!passphrase.empty()) {
        properties.SetStringWithoutPathExpansion(
            flimflam::kPassphraseProperty, passphrase);
      }
    }
    bool share_network = GetShareNetwork(share_default);
    ash::network_connect::ConfigureNetworkAndConnect(
        service_path_, properties, share_network);
  }
  return true;  // dialog will be closed
}

std::string WifiConfigView::GetSsid() const {
  std::string result;
  if (ssid_textfield_ != NULL) {
    std::string untrimmed = UTF16ToUTF8(ssid_textfield_->text());
    TrimWhitespaceASCII(untrimmed, TRIM_ALL, &result);
  }
  return result;
}

std::string WifiConfigView::GetPassphrase() const {
  std::string result;
  if (passphrase_textfield_ != NULL)
    result = UTF16ToUTF8(passphrase_textfield_->text());
  return result;
}

bool WifiConfigView::GetSaveCredentials() const {
  if (!save_credentials_checkbox_)
    return true;  // share networks by default (e.g. non 8021x).
  return save_credentials_checkbox_->checked();
}

bool WifiConfigView::GetShareNetwork(bool share_default) const {
  if (!share_network_checkbox_)
    return share_default;
  return share_network_checkbox_->checked();
}

std::string WifiConfigView::GetEapMethod() const {
  DCHECK(eap_method_combobox_);
  switch (eap_method_combobox_->selected_index()) {
    case EAP_METHOD_INDEX_PEAP:
      return flimflam::kEapMethodPEAP;
    case EAP_METHOD_INDEX_TLS:
      return flimflam::kEapMethodTLS;
    case EAP_METHOD_INDEX_TTLS:
      return flimflam::kEapMethodTTLS;
    case EAP_METHOD_INDEX_LEAP:
      return flimflam::kEapMethodLEAP;
    case EAP_METHOD_INDEX_NONE:
    default:
      return "";
  }
}

std::string WifiConfigView::GetEapPhase2Auth() const {
  DCHECK(phase_2_auth_combobox_);
  bool is_peap = (GetEapMethod() == flimflam::kEapMethodPEAP);
  switch (phase_2_auth_combobox_->selected_index()) {
    case PHASE_2_AUTH_INDEX_MD5:
      return is_peap ? flimflam::kEapPhase2AuthPEAPMD5
          : flimflam::kEapPhase2AuthTTLSMD5;
    case PHASE_2_AUTH_INDEX_MSCHAPV2:
      return is_peap ? flimflam::kEapPhase2AuthPEAPMSCHAPV2
          : flimflam::kEapPhase2AuthTTLSMSCHAPV2;
    case PHASE_2_AUTH_INDEX_MSCHAP:
      return flimflam::kEapPhase2AuthTTLSMSCHAP;
    case PHASE_2_AUTH_INDEX_PAP:
      return flimflam::kEapPhase2AuthTTLSPAP;
    case PHASE_2_AUTH_INDEX_CHAP:
      return flimflam::kEapPhase2AuthTTLSCHAP;
    case PHASE_2_AUTH_INDEX_AUTO:
    default:
      return "";
  }
}

std::string WifiConfigView::GetEapServerCaCertPEM() const {
  DCHECK(server_ca_cert_combobox_);
  int index = server_ca_cert_combobox_->selected_index();
  if (index == 0) {
    // First item is "Default".
    return std::string();
  } else if (index == server_ca_cert_combobox_->model()->GetItemCount() - 1) {
    // Last item is "Do not check".
    return std::string();
  } else {
    int cert_index = index - 1;
    return CertLibrary::Get()->GetCertPEMAt(
        CertLibrary::CERT_TYPE_SERVER_CA, cert_index);
  }
}

bool WifiConfigView::GetEapUseSystemCas() const {
  DCHECK(server_ca_cert_combobox_);
  // Only use system CAs if the first item ("Default") is selected.
  return server_ca_cert_combobox_->selected_index() == 0;
}

std::string WifiConfigView::GetEapClientCertPkcs11Id() const {
  DCHECK(user_cert_combobox_);
  if (!HaveUserCerts()) {
    return std::string();  // "None installed"
  } else {
    // Certificates are listed in the order they appear in the model.
    int index = user_cert_combobox_->selected_index();
    return CertLibrary::Get()->GetCertPkcs11IdAt(
        CertLibrary::CERT_TYPE_USER, index);
  }
}

std::string WifiConfigView::GetEapIdentity() const {
  DCHECK(identity_textfield_);
  return UTF16ToUTF8(identity_textfield_->text());
}

std::string WifiConfigView::GetEapAnonymousIdentity() const {
  DCHECK(identity_anonymous_textfield_);
  return UTF16ToUTF8(identity_anonymous_textfield_->text());
}

void WifiConfigView::SetEapProperties(base::DictionaryValue* properties) {
  properties->SetStringWithoutPathExpansion(
      flimflam::kEapIdentityProperty, GetEapIdentity());
  properties->SetStringWithoutPathExpansion(
      flimflam::kEapMethodProperty, GetEapMethod());
  properties->SetStringWithoutPathExpansion(
      flimflam::kEapPhase2AuthProperty, GetEapPhase2Auth());
  properties->SetStringWithoutPathExpansion(
      flimflam::kEapAnonymousIdentityProperty, GetEapAnonymousIdentity());

  // shill requires both CertID and KeyID for TLS connections, despite
  // the fact that by convention they are the same ID.
  properties->SetStringWithoutPathExpansion(
      flimflam::kEapCertIdProperty, GetEapClientCertPkcs11Id());
  properties->SetStringWithoutPathExpansion(
      flimflam::kEapKeyIdProperty, GetEapClientCertPkcs11Id());

  properties->SetBooleanWithoutPathExpansion(
      flimflam::kEapUseSystemCasProperty, GetEapUseSystemCas());
  properties->SetStringWithoutPathExpansion(
      flimflam::kEapPasswordProperty, GetPassphrase());

  base::ListValue* pem_list = new base::ListValue;
  pem_list->AppendString(GetEapServerCaCertPEM());
  properties->SetWithoutPathExpansion(
      shill::kEapCaCertPemProperty, pem_list);
}

void WifiConfigView::Cancel() {
}

// This will initialize the view depending on if we have a wifi network or not.
// And if we are doing simple password encryption or the more complicated
// 802.1x encryption.
// If we are creating the "Join other network..." dialog, we will allow user
// to enter the data. And if they select the 802.1x encryption, we will show
// the 802.1x fields.
void WifiConfigView::Init(bool show_8021x) {
  const NetworkState* wifi = NetworkHandler::Get()->network_state_handler()->
      GetNetworkState(service_path_);
  if (wifi) {
    DCHECK(wifi->type() == flimflam::kTypeWifi);
    if (wifi->security() == flimflam::kSecurity8021x)
      show_8021x = true;
    ParseWiFiEAPUIProperty(&eap_method_ui_data_, wifi, onc::eap::kOuter);
    ParseWiFiEAPUIProperty(&phase_2_auth_ui_data_, wifi, onc::eap::kInner);
    ParseWiFiEAPUIProperty(&user_cert_ui_data_, wifi, onc::eap::kClientCertRef);
    ParseWiFiEAPUIProperty(&server_ca_cert_ui_data_, wifi,
                           onc::eap::kServerCARef);
    if (server_ca_cert_ui_data_.IsManaged()) {
      ParseWiFiEAPUIProperty(&server_ca_cert_ui_data_, wifi,
                             onc::eap::kUseSystemCAs);
    }
    ParseWiFiEAPUIProperty(&identity_ui_data_, wifi, onc::eap::kIdentity);
    ParseWiFiEAPUIProperty(&identity_anonymous_ui_data_, wifi,
                           onc::eap::kAnonymousIdentity);
    ParseWiFiEAPUIProperty(&save_credentials_ui_data_, wifi,
                           onc::eap::kSaveCredentials);
    if (show_8021x)
      ParseWiFiEAPUIProperty(&passphrase_ui_data_, wifi, onc::eap::kPassword);
    else
      ParseWiFiUIProperty(&passphrase_ui_data_, wifi, onc::wifi::kPassphrase);
  }

  views::GridLayout* layout = views::GridLayout::CreatePanel(this);
  SetLayoutManager(layout);

  const int column_view_set_id = 0;
  views::ColumnSet* column_set = layout->AddColumnSet(column_view_set_id);
  const int kPasswordVisibleWidth = 20;
  // Label
  column_set->AddColumn(views::GridLayout::LEADING, views::GridLayout::FILL, 1,
                        views::GridLayout::USE_PREF, 0, 0);
  column_set->AddPaddingColumn(0, views::kRelatedControlSmallHorizontalSpacing);
  // Textfield, combobox.
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL, 1,
                        views::GridLayout::USE_PREF, 0,
                        ChildNetworkConfigView::kInputFieldMinWidth);
  column_set->AddPaddingColumn(0, views::kRelatedControlSmallHorizontalSpacing);
  // Password visible button / policy indicator.
  column_set->AddColumn(views::GridLayout::CENTER, views::GridLayout::FILL, 1,
                        views::GridLayout::USE_PREF, 0, kPasswordVisibleWidth);

  // SSID input
  layout->StartRow(0, column_view_set_id);
  layout->AddView(new views::Label(l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NETWORK_ID)));
  if (!wifi) {
    ssid_textfield_ = new views::Textfield(views::Textfield::STYLE_DEFAULT);
    ssid_textfield_->SetController(this);
    ssid_textfield_->SetAccessibleName(l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NETWORK_ID));
    layout->AddView(ssid_textfield_);
  } else {
    views::Label* label = new views::Label(UTF8ToUTF16(wifi->name()));
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    layout->AddView(label);
  }
  layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

  // Security select
  if (!wifi && !show_8021x) {
    layout->StartRow(0, column_view_set_id);
    string16 label_text = l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY);
    layout->AddView(new views::Label(label_text));
    security_combobox_model_.reset(new internal::SecurityComboboxModel);
    security_combobox_ = new views::Combobox(security_combobox_model_.get());
    security_combobox_->SetAccessibleName(label_text);
    security_combobox_->set_listener(this);
    layout->AddView(security_combobox_);
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
  }

  // Only enumerate certificates in the data model for 802.1X networks.
  if (show_8021x) {
    // Observer any changes to the certificate list.
    CertLibrary::Get()->AddObserver(this);

    // EAP method
    layout->StartRow(0, column_view_set_id);
    string16 eap_label_text = l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_EAP_METHOD);
    layout->AddView(new views::Label(eap_label_text));
    eap_method_combobox_model_.reset(new internal::EAPMethodComboboxModel);
    eap_method_combobox_ = new views::Combobox(
        eap_method_combobox_model_.get());
    eap_method_combobox_->SetAccessibleName(eap_label_text);
    eap_method_combobox_->set_listener(this);
    eap_method_combobox_->SetEnabled(eap_method_ui_data_.IsEditable());
    layout->AddView(eap_method_combobox_);
    layout->AddView(new ControlledSettingIndicatorView(eap_method_ui_data_));
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

    // Phase 2 authentication
    layout->StartRow(0, column_view_set_id);
    string16 phase_2_label_text = l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PHASE_2_AUTH);
    phase_2_auth_label_ = new views::Label(phase_2_label_text);
    layout->AddView(phase_2_auth_label_);
    phase_2_auth_combobox_model_.reset(
        new internal::Phase2AuthComboboxModel(eap_method_combobox_));
    phase_2_auth_combobox_ = new views::Combobox(
        phase_2_auth_combobox_model_.get());
    phase_2_auth_combobox_->SetAccessibleName(phase_2_label_text);
    phase_2_auth_label_->SetEnabled(false);
    phase_2_auth_combobox_->SetEnabled(false);
    phase_2_auth_combobox_->set_listener(this);
    layout->AddView(phase_2_auth_combobox_);
    layout->AddView(new ControlledSettingIndicatorView(phase_2_auth_ui_data_));
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

    // Server CA certificate
    layout->StartRow(0, column_view_set_id);
    string16 server_ca_cert_label_text = l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_SERVER_CA);
    server_ca_cert_label_ = new views::Label(server_ca_cert_label_text);
    layout->AddView(server_ca_cert_label_);
    server_ca_cert_combobox_model_.reset(
        new internal::ServerCACertComboboxModel());
    server_ca_cert_combobox_ = new ComboboxWithWidth(
        server_ca_cert_combobox_model_.get(),
        ChildNetworkConfigView::kInputFieldMinWidth);
    server_ca_cert_combobox_->SetAccessibleName(server_ca_cert_label_text);
    server_ca_cert_label_->SetEnabled(false);
    server_ca_cert_combobox_->SetEnabled(false);
    server_ca_cert_combobox_->set_listener(this);
    layout->AddView(server_ca_cert_combobox_);
    layout->AddView(
        new ControlledSettingIndicatorView(server_ca_cert_ui_data_));
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

    // User certificate
    layout->StartRow(0, column_view_set_id);
    string16 user_cert_label_text = l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT);
    user_cert_label_ = new views::Label(user_cert_label_text);
    layout->AddView(user_cert_label_);
    user_cert_combobox_model_.reset(new internal::UserCertComboboxModel());
    user_cert_combobox_ = new views::Combobox(user_cert_combobox_model_.get());
    user_cert_combobox_->SetAccessibleName(user_cert_label_text);
    user_cert_label_->SetEnabled(false);
    user_cert_combobox_->SetEnabled(false);
    user_cert_combobox_->set_listener(this);
    layout->AddView(user_cert_combobox_);
    layout->AddView(new ControlledSettingIndicatorView(user_cert_ui_data_));
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

    // Identity
    layout->StartRow(0, column_view_set_id);
    string16 identity_label_text = l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_IDENTITY);
    identity_label_ = new views::Label(identity_label_text);
    layout->AddView(identity_label_);
    identity_textfield_ = new views::Textfield(
        views::Textfield::STYLE_DEFAULT);
    identity_textfield_->SetAccessibleName(identity_label_text);
    identity_textfield_->SetController(this);
    identity_textfield_->SetEnabled(identity_ui_data_.IsEditable());
    layout->AddView(identity_textfield_);
    layout->AddView(new ControlledSettingIndicatorView(identity_ui_data_));
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
  }

  // Passphrase input
  layout->StartRow(0, column_view_set_id);
  int label_text_id = IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PASSPHRASE;
  string16 passphrase_label_text = l10n_util::GetStringUTF16(label_text_id);
  passphrase_label_ = new views::Label(passphrase_label_text);
  layout->AddView(passphrase_label_);
  passphrase_textfield_ = new views::Textfield(
      views::Textfield::STYLE_OBSCURED);
  passphrase_textfield_->SetController(this);
  // Disable passphrase input initially for other network.
  passphrase_label_->SetEnabled(wifi != NULL);
  passphrase_textfield_->SetEnabled(wifi && passphrase_ui_data_.IsEditable());
  passphrase_textfield_->SetAccessibleName(passphrase_label_text);
  layout->AddView(passphrase_textfield_);

  if (passphrase_ui_data_.IsManaged()) {
    layout->AddView(new ControlledSettingIndicatorView(passphrase_ui_data_));
  } else {
    // Password visible button.
    passphrase_visible_button_ = new views::ToggleImageButton(this);
    passphrase_visible_button_->set_focusable(true);
    passphrase_visible_button_->SetTooltipText(
        l10n_util::GetStringUTF16(
            IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PASSPHRASE_SHOW));
    passphrase_visible_button_->SetToggledTooltipText(
        l10n_util::GetStringUTF16(
            IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PASSPHRASE_HIDE));
    passphrase_visible_button_->SetImage(
        views::ImageButton::STATE_NORMAL,
        ResourceBundle::GetSharedInstance().
        GetImageSkiaNamed(IDR_NETWORK_SHOW_PASSWORD));
    passphrase_visible_button_->SetImage(
        views::ImageButton::STATE_HOVERED,
        ResourceBundle::GetSharedInstance().
        GetImageSkiaNamed(IDR_NETWORK_SHOW_PASSWORD_HOVER));
    passphrase_visible_button_->SetToggledImage(
        views::ImageButton::STATE_NORMAL,
        ResourceBundle::GetSharedInstance().
        GetImageSkiaNamed(IDR_NETWORK_HIDE_PASSWORD));
    passphrase_visible_button_->SetToggledImage(
        views::ImageButton::STATE_HOVERED,
        ResourceBundle::GetSharedInstance().
        GetImageSkiaNamed(IDR_NETWORK_HIDE_PASSWORD_HOVER));
    passphrase_visible_button_->SetImageAlignment(
        views::ImageButton::ALIGN_CENTER, views::ImageButton::ALIGN_MIDDLE);
    layout->AddView(passphrase_visible_button_);
  }

  layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

  if (show_8021x) {
    // Anonymous identity
    layout->StartRow(0, column_view_set_id);
    identity_anonymous_label_ =
        new views::Label(l10n_util::GetStringUTF16(
            IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_IDENTITY_ANONYMOUS));
    layout->AddView(identity_anonymous_label_);
    identity_anonymous_textfield_ = new views::Textfield(
        views::Textfield::STYLE_DEFAULT);
    identity_anonymous_label_->SetEnabled(false);
    identity_anonymous_textfield_->SetEnabled(false);
    identity_anonymous_textfield_->SetController(this);
    layout->AddView(identity_anonymous_textfield_);
    layout->AddView(
        new ControlledSettingIndicatorView(identity_anonymous_ui_data_));
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
  }

  // Checkboxes.

  // Save credentials
  if (show_8021x) {
    layout->StartRow(0, column_view_set_id);
    save_credentials_checkbox_ = new views::Checkbox(
        l10n_util::GetStringUTF16(
            IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SAVE_CREDENTIALS));
    save_credentials_checkbox_->SetEnabled(
        save_credentials_ui_data_.IsEditable());
    layout->SkipColumns(1);
    layout->AddView(save_credentials_checkbox_);
    layout->AddView(
        new ControlledSettingIndicatorView(save_credentials_ui_data_));
  }

  // Share network
  if (!wifi || wifi->profile_path().empty()) {
    layout->StartRow(0, column_view_set_id);
    share_network_checkbox_ = new views::Checkbox(
        l10n_util::GetStringUTF16(
            IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SHARE_NETWORK));
    layout->SkipColumns(1);
    layout->AddView(share_network_checkbox_);
  }
  layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

  // Create an error label.
  layout->StartRow(0, column_view_set_id);
  layout->SkipColumns(1);
  error_label_ = new views::Label();
  error_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  error_label_->SetEnabledColor(SK_ColorRED);
  layout->AddView(error_label_);

  // Initialize the field and checkbox values.

  if (!wifi && show_8021x)
    RefreshEapFields();

  RefreshShareCheckbox();
  UpdateErrorLabel();

  if (wifi) {
    NetworkHandler::Get()->network_configuration_handler()->GetProperties(
        service_path_,
        base::Bind(&WifiConfigView::InitFromProperties,
                   weak_ptr_factory_.GetWeakPtr(), show_8021x),
        base::Bind(&ShillError, "GetProperties"));
  }
}

void WifiConfigView::InitFromProperties(
    bool show_8021x,
    const std::string& service_path,
    const base::DictionaryValue& properties) {
  std::string passphrase;
  properties.GetStringWithoutPathExpansion(
      flimflam::kPassphraseProperty, &passphrase);
  passphrase_textfield_->SetText(UTF8ToUTF16(passphrase));

  if (!show_8021x)
    return;

  // EAP Method
  std::string eap_method;
  properties.GetStringWithoutPathExpansion(
      flimflam::kEapMethodProperty, &eap_method);
  if (eap_method == flimflam::kEapMethodPEAP)
    eap_method_combobox_->SetSelectedIndex(EAP_METHOD_INDEX_PEAP);
  else if (eap_method == flimflam::kEapMethodTTLS)
    eap_method_combobox_->SetSelectedIndex(EAP_METHOD_INDEX_TTLS);
  else if (eap_method == flimflam::kEapMethodTLS)
    eap_method_combobox_->SetSelectedIndex(EAP_METHOD_INDEX_TLS);
  else if (eap_method == flimflam::kEapMethodLEAP)
    eap_method_combobox_->SetSelectedIndex(EAP_METHOD_INDEX_LEAP);
  RefreshEapFields();

  // Phase 2 authentication and anonymous identity.
  if (Phase2AuthActive()) {
    std::string eap_phase_2_auth;
    properties.GetStringWithoutPathExpansion(
        flimflam::kEapPhase2AuthProperty, &eap_phase_2_auth);
    if (eap_phase_2_auth == flimflam::kEapPhase2AuthTTLSMD5)
      phase_2_auth_combobox_->SetSelectedIndex(PHASE_2_AUTH_INDEX_MD5);
    else if (eap_phase_2_auth == flimflam::kEapPhase2AuthTTLSMSCHAPV2)
      phase_2_auth_combobox_->SetSelectedIndex(PHASE_2_AUTH_INDEX_MSCHAPV2);
    else if (eap_phase_2_auth == flimflam::kEapPhase2AuthTTLSMSCHAP)
      phase_2_auth_combobox_->SetSelectedIndex(PHASE_2_AUTH_INDEX_MSCHAP);
    else if (eap_phase_2_auth == flimflam::kEapPhase2AuthTTLSPAP)
      phase_2_auth_combobox_->SetSelectedIndex(PHASE_2_AUTH_INDEX_PAP);
    else if (eap_phase_2_auth == flimflam::kEapPhase2AuthTTLSCHAP)
      phase_2_auth_combobox_->SetSelectedIndex(PHASE_2_AUTH_INDEX_CHAP);

    std::string eap_anonymous_identity;
    properties.GetStringWithoutPathExpansion(
        flimflam::kEapAnonymousIdentityProperty, &eap_anonymous_identity);
    identity_anonymous_textfield_->SetText(UTF8ToUTF16(eap_anonymous_identity));
  }

  // Server CA certificate.
  if (CaCertActive()) {
    std::string eap_ca_cert_pem;
    const base::ListValue* pems = NULL;
    if (properties.GetListWithoutPathExpansion(
            shill::kEapCaCertPemProperty, &pems))
      pems->GetString(0, &eap_ca_cert_pem);
    if (eap_ca_cert_pem.empty()) {
      bool eap_use_system_cas = false;
      properties.GetBooleanWithoutPathExpansion(
          flimflam::kEapUseSystemCasProperty, &eap_use_system_cas);
      if (eap_use_system_cas) {
        // "Default"
        server_ca_cert_combobox_->SetSelectedIndex(0);
      } else {
        // "Do not check".
        server_ca_cert_combobox_->SetSelectedIndex(
            server_ca_cert_combobox_->model()->GetItemCount() - 1);
      }
    } else {
      // Select the certificate if available.
      int cert_index = CertLibrary::Get()->GetCertIndexByPEM(
          CertLibrary::CERT_TYPE_SERVER_CA, eap_ca_cert_pem);
      if (cert_index >= 0) {
        // Skip item for "Default".
        server_ca_cert_combobox_->SetSelectedIndex(1 + cert_index);
      }
    }
  }

  // User certificate.
  if (UserCertActive()) {
    std::string eap_cert_id;
    properties.GetStringWithoutPathExpansion(
        flimflam::kEapCertIdProperty, &eap_cert_id);
    if (!eap_cert_id.empty()) {
      int cert_index = CertLibrary::Get()->GetCertIndexByPkcs11Id(
          CertLibrary::CERT_TYPE_USER, eap_cert_id);
      if (cert_index >= 0)
        user_cert_combobox_->SetSelectedIndex(cert_index);
    }
  }

  // Identity is always active.
  std::string eap_identity;
  properties.GetStringWithoutPathExpansion(
      flimflam::kEapIdentityProperty, &eap_identity);
  identity_textfield_->SetText(UTF8ToUTF16(eap_identity));

  // Passphrase
  if (PassphraseActive()) {
    std::string eap_password;
    properties.GetStringWithoutPathExpansion(
        flimflam::kEapPasswordProperty, &eap_password);
    passphrase_textfield_->SetText(UTF8ToUTF16(eap_password));
  }

  // Save credentials
  bool save_credentials = false;
  properties.GetBooleanWithoutPathExpansion(
      flimflam::kSaveCredentialsProperty, &save_credentials);
  save_credentials_checkbox_->SetChecked(save_credentials);

  RefreshShareCheckbox();
  UpdateErrorLabel();
}

void WifiConfigView::InitFocus() {
  views::View* view_to_focus = GetInitiallyFocusedView();
  if (view_to_focus)
    view_to_focus->RequestFocus();
}

void WifiConfigView::NetworkPropertiesUpdated(const NetworkState* network) {
  if (network->path() != service_path_)
    return;
  UpdateErrorLabel();
}

// static
void WifiConfigView::ParseWiFiUIProperty(
    NetworkPropertyUIData* property_ui_data,
    const NetworkState* network,
    const std::string& key) {
  onc::ONCSource onc_source = onc::ONC_SOURCE_NONE;
  const base::DictionaryValue* onc =
      network_connect::FindPolicyForActiveUser(network, &onc_source);

  property_ui_data->ParseOncProperty(
      onc_source,
      onc,
      base::StringPrintf("%s.%s", onc::network_config::kWiFi, key.c_str()));
}

// static
void WifiConfigView::ParseWiFiEAPUIProperty(
    NetworkPropertyUIData* property_ui_data,
    const NetworkState* network,
    const std::string& key) {
  ParseWiFiUIProperty(
      property_ui_data, network,
      base::StringPrintf("%s.%s", onc::wifi::kEAP, key.c_str()));
}

}  // namespace chromeos
