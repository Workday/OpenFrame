// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/network_connection_handler.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/shill_manager_client.h"
#include "chromeos/dbus/shill_service_client.h"
#include "chromeos/network/client_cert_util.h"
#include "chromeos/network/managed_network_configuration_handler.h"
#include "chromeos/network/network_configuration_handler.h"
#include "chromeos/network/network_event_log.h"
#include "chromeos/network/network_handler_callbacks.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/network/network_ui_data.h"
#include "dbus/object_path.h"
#include "net/cert/x509_certificate.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

void InvokeErrorCallback(const std::string& service_path,
                         const network_handler::ErrorCallback& error_callback,
                         const std::string& error_name) {
  std::string error_msg = "Connect Error: " + error_name;
  NET_LOG_ERROR(error_msg, service_path);
  if (error_callback.is_null())
    return;
  scoped_ptr<base::DictionaryValue> error_data(
      network_handler::CreateErrorData(service_path, error_name, error_msg));
  error_callback.Run(error_name, error_data.Pass());
}

bool IsAuthenticationError(const std::string& error) {
  return (error == flimflam::kErrorBadWEPKey ||
          error == flimflam::kErrorPppAuthFailed ||
          error == shill::kErrorEapLocalTlsFailed ||
          error == shill::kErrorEapRemoteTlsFailed ||
          error == shill::kErrorEapAuthenticationFailed);
}

void CopyStringFromDictionary(const base::DictionaryValue& source,
                              const std::string& key,
                              base::DictionaryValue* dest) {
  std::string string_value;
  if (source.GetStringWithoutPathExpansion(key, &string_value))
    dest->SetStringWithoutPathExpansion(key, string_value);
}

bool NetworkRequiresActivation(const NetworkState* network) {
  return (network->type() == flimflam::kTypeCellular &&
          (network->activation_state() != flimflam::kActivationStateActivated ||
           network->cellular_out_of_credits()));
}

bool VPNIsConfigured(const std::string& service_path,
                     const std::string& provider_type,
                     const base::DictionaryValue& provider_properties) {
  if (provider_type == flimflam::kProviderOpenVpn) {
    std::string hostname;
    provider_properties.GetStringWithoutPathExpansion(
        flimflam::kHostProperty, &hostname);
    if (hostname.empty()) {
      NET_LOG_EVENT("OpenVPN: No hostname", service_path);
      return false;
    }
    std::string username;
    provider_properties.GetStringWithoutPathExpansion(
        flimflam::kOpenVPNUserProperty, &username);
    if (username.empty()) {
      NET_LOG_EVENT("OpenVPN: No username", service_path);
      return false;
    }
    bool passphrase_required = false;
    provider_properties.GetBooleanWithoutPathExpansion(
        flimflam::kPassphraseRequiredProperty, &passphrase_required);
    if (passphrase_required) {
      NET_LOG_EVENT("OpenVPN: Passphrase Required", service_path);
      return false;
    }
    NET_LOG_EVENT("OpenVPN Is Configured", service_path);
  } else {
    bool passphrase_required = false;
    std::string passphrase;
    provider_properties.GetBooleanWithoutPathExpansion(
        flimflam::kL2tpIpsecPskRequiredProperty, &passphrase_required);
    if (passphrase_required) {
      NET_LOG_EVENT("VPN: Passphrase Required", service_path);
      return false;
    }
    NET_LOG_EVENT("VPN Is Configured", service_path);
  }
  return true;
}

}  // namespace

const char NetworkConnectionHandler::kErrorNotFound[] = "not-found";
const char NetworkConnectionHandler::kErrorConnected[] = "connected";
const char NetworkConnectionHandler::kErrorConnecting[] = "connecting";
const char NetworkConnectionHandler::kErrorNotConnected[] = "not-connected";
const char NetworkConnectionHandler::kErrorPassphraseRequired[] =
    "passphrase-required";
const char NetworkConnectionHandler::kErrorActivationRequired[] =
    "activation-required";
const char NetworkConnectionHandler::kErrorCertificateRequired[] =
    "certificate-required";
const char NetworkConnectionHandler::kErrorConfigurationRequired[] =
    "configuration-required";
const char NetworkConnectionHandler::kErrorAuthenticationRequired[] =
    "authentication-required";
const char NetworkConnectionHandler::kErrorShillError[] = "shill-error";
const char NetworkConnectionHandler::kErrorConnectFailed[] = "connect-failed";
const char NetworkConnectionHandler::kErrorConfigureFailed[] =
    "configure-failed";
const char NetworkConnectionHandler::kErrorActivateFailed[] =
    "activate-failed";
const char NetworkConnectionHandler::kErrorMissingProvider[] =
    "missing-provider";
const char NetworkConnectionHandler::kErrorConnectCanceled[] =
    "connect-canceled";
const char NetworkConnectionHandler::kErrorUnknown[] = "unknown-error";

struct NetworkConnectionHandler::ConnectRequest {
  ConnectRequest(const std::string& service_path,
                 const base::Closure& success,
                 const network_handler::ErrorCallback& error)
      : service_path(service_path),
        connect_state(CONNECT_REQUESTED),
        success_callback(success),
        error_callback(error) {
  }
  enum ConnectState {
    CONNECT_REQUESTED = 0,
    CONNECT_STARTED = 1,
    CONNECT_CONNECTING = 2
  };
  std::string service_path;
  ConnectState connect_state;
  base::Closure success_callback;
  network_handler::ErrorCallback error_callback;
};

NetworkConnectionHandler::NetworkConnectionHandler()
    : cert_loader_(NULL),
      network_state_handler_(NULL),
      network_configuration_handler_(NULL),
      logged_in_(false),
      certificates_loaded_(false) {
}

NetworkConnectionHandler::~NetworkConnectionHandler() {
  if (network_state_handler_)
    network_state_handler_->RemoveObserver(this, FROM_HERE);
  if (cert_loader_)
    cert_loader_->RemoveObserver(this);
  if (LoginState::IsInitialized())
    LoginState::Get()->RemoveObserver(this);
}

void NetworkConnectionHandler::Init(
    NetworkStateHandler* network_state_handler,
    NetworkConfigurationHandler* network_configuration_handler) {
  if (LoginState::IsInitialized()) {
    LoginState::Get()->AddObserver(this);
    logged_in_ =
        LoginState::Get()->GetLoggedInState() == LoginState::LOGGED_IN_ACTIVE;
  }
  if (CertLoader::IsInitialized()) {
    cert_loader_ = CertLoader::Get();
    cert_loader_->AddObserver(this);
    certificates_loaded_ = cert_loader_->certificates_loaded();
  } else {
    // TODO(stevenjb): Require a mock or stub cert_loader in tests.
    certificates_loaded_ = true;
  }
  if (network_state_handler) {
    network_state_handler_ = network_state_handler;
    network_state_handler_->AddObserver(this, FROM_HERE);
  }
  network_configuration_handler_ = network_configuration_handler;
}

void NetworkConnectionHandler::LoggedInStateChanged(
    LoginState::LoggedInState state) {
  if (state == LoginState::LOGGED_IN_ACTIVE) {
    logged_in_ = true;
    NET_LOG_EVENT("Logged In", "");
  }
}

void NetworkConnectionHandler::OnCertificatesLoaded(
    const net::CertificateList& cert_list,
    bool initial_load) {
  certificates_loaded_ = true;
  NET_LOG_EVENT("Certificates Loaded", "");
  if (queued_connect_) {
    NET_LOG_EVENT("Connecting to Queued Network",
                  queued_connect_->service_path);
    ConnectToNetwork(queued_connect_->service_path,
                     queued_connect_->success_callback,
                     queued_connect_->error_callback,
                     false /* check_error_state */);
  } else if (initial_load) {
    // Once certificates have loaded, connect to the "best" available network.
    network_state_handler_->ConnectToBestWifiNetwork();
  }
}

void NetworkConnectionHandler::ConnectToNetwork(
    const std::string& service_path,
    const base::Closure& success_callback,
    const network_handler::ErrorCallback& error_callback,
    bool check_error_state) {
  NET_LOG_USER("ConnectToNetwork", service_path);
  // Clear any existing queued connect request.
  queued_connect_.reset();
  if (HasConnectingNetwork(service_path)) {
    NET_LOG_USER("Connect Request While Pending", service_path);
    InvokeErrorCallback(service_path, error_callback, kErrorConnecting);
    return;
  }

  // Check cached network state for connected, connecting, or unactivated
  // networks. These states will not be affected by a recent configuration.
  // Note: NetworkState may not exist for a network that was recently
  // configured, in which case these checks do not apply anyway.
  const NetworkState* network =
      network_state_handler_->GetNetworkState(service_path);

  if (network) {
    // For existing networks, perform some immediate consistency checks.
    if (network->IsConnectedState()) {
      InvokeErrorCallback(service_path, error_callback, kErrorConnected);
      return;
    }
    if (network->IsConnectingState()) {
      InvokeErrorCallback(service_path, error_callback, kErrorConnecting);
      return;
    }
    if (NetworkRequiresActivation(network)) {
      InvokeErrorCallback(service_path, error_callback,
                          kErrorActivationRequired);
      return;
    }

    if (check_error_state) {
      const std::string& error = network->error();
      if (error == flimflam::kErrorConnectFailed) {
        InvokeErrorCallback(
            service_path, error_callback, kErrorPassphraseRequired);
        return;
      }
      if (error == flimflam::kErrorBadPassphrase) {
        InvokeErrorCallback(
            service_path, error_callback, kErrorPassphraseRequired);
        return;
      }
      if (IsAuthenticationError(error)) {
        InvokeErrorCallback(
            service_path, error_callback, kErrorAuthenticationRequired);
        return;
      }
    }
  }

  // All synchronous checks passed, add |service_path| to connecting list.
  pending_requests_.insert(std::make_pair(
      service_path,
      ConnectRequest(service_path, success_callback, error_callback)));

  // Connect immediately to 'connectable' networks.
  // TODO(stevenjb): Shill needs to properly set Connectable for VPN.
  if (network &&
      network->connectable() && network->type() != flimflam::kTypeVPN) {
    CallShillConnect(service_path);
    return;
  }

  // Request additional properties to check. VerifyConfiguredAndConnect will
  // use only these properties, not cached properties, to ensure that they
  // are up to date after any recent configuration.
  network_configuration_handler_->GetProperties(
      service_path,
      base::Bind(&NetworkConnectionHandler::VerifyConfiguredAndConnect,
                 AsWeakPtr(), check_error_state),
      base::Bind(&NetworkConnectionHandler::HandleConfigurationFailure,
                 AsWeakPtr(), service_path));
}

void NetworkConnectionHandler::DisconnectNetwork(
    const std::string& service_path,
    const base::Closure& success_callback,
    const network_handler::ErrorCallback& error_callback) {
  NET_LOG_USER("DisconnectNetwork", service_path);
  const NetworkState* network =
      network_state_handler_->GetNetworkState(service_path);
  if (!network) {
    InvokeErrorCallback(service_path, error_callback, kErrorNotFound);
    return;
  }
  if (!network->IsConnectedState()) {
    InvokeErrorCallback(service_path, error_callback, kErrorNotConnected);
    return;
  }
  CallShillDisconnect(service_path, success_callback, error_callback);
}

void NetworkConnectionHandler::ActivateNetwork(
    const std::string& service_path,
    const std::string& carrier,
    const base::Closure& success_callback,
    const network_handler::ErrorCallback& error_callback) {
  NET_LOG_USER("DisconnectNetwork", service_path);
  const NetworkState* network =
      network_state_handler_->GetNetworkState(service_path);
  if (!network) {
    InvokeErrorCallback(service_path, error_callback, kErrorNotFound);
    return;
  }
  CallShillActivate(service_path, carrier, success_callback, error_callback);
}

bool NetworkConnectionHandler::HasConnectingNetwork(
    const std::string& service_path) {
  return pending_requests_.count(service_path) != 0;
}

void NetworkConnectionHandler::NetworkListChanged() {
  CheckAllPendingRequests();
}

void NetworkConnectionHandler::NetworkPropertiesUpdated(
    const NetworkState* network) {
  if (HasConnectingNetwork(network->path()))
    CheckPendingRequest(network->path());
}

NetworkConnectionHandler::ConnectRequest*
NetworkConnectionHandler::GetPendingRequest(const std::string& service_path) {
  std::map<std::string, ConnectRequest>::iterator iter =
      pending_requests_.find(service_path);
  return iter != pending_requests_.end() ? &(iter->second) : NULL;
}

// ConnectToNetwork implementation

void NetworkConnectionHandler::VerifyConfiguredAndConnect(
    bool check_error_state,
    const std::string& service_path,
    const base::DictionaryValue& service_properties) {
  NET_LOG_EVENT("VerifyConfiguredAndConnect", service_path);

  // If 'passphrase_required' is still true, then the 'Passphrase' property
  // has not been set to a minimum length value.
  bool passphrase_required = false;
  service_properties.GetBooleanWithoutPathExpansion(
      flimflam::kPassphraseRequiredProperty, &passphrase_required);
  if (passphrase_required) {
    ErrorCallbackForPendingRequest(service_path, kErrorPassphraseRequired);
    return;
  }

  std::string type, security;
  service_properties.GetStringWithoutPathExpansion(
      flimflam::kTypeProperty, &type);
  service_properties.GetStringWithoutPathExpansion(
      flimflam::kSecurityProperty, &security);
  bool connectable = false;
  service_properties.GetBooleanWithoutPathExpansion(
      flimflam::kConnectableProperty, &connectable);

  // In case NetworkState was not available in ConnectToNetwork (e.g. it had
  // been recently configured), we need to check Connectable again.
  if (connectable && type != flimflam::kTypeVPN) {
    // TODO(stevenjb): Shill needs to properly set Connectable for VPN.
    CallShillConnect(service_path);
    return;
  }

  // Get VPN provider type and host (required for configuration) and ensure
  // that required VPN non-cert properties are set.
  std::string vpn_provider_type, vpn_provider_host;
  if (type == flimflam::kTypeVPN) {
    // VPN Provider values are read from the "Provider" dictionary, not the
    // "Provider.Type", etc keys (which are used only to set the values).
    const base::DictionaryValue* provider_properties;
    if (service_properties.GetDictionaryWithoutPathExpansion(
            flimflam::kProviderProperty, &provider_properties)) {
      provider_properties->GetStringWithoutPathExpansion(
          flimflam::kTypeProperty, &vpn_provider_type);
      provider_properties->GetStringWithoutPathExpansion(
          flimflam::kHostProperty, &vpn_provider_host);
    }
    if (vpn_provider_type.empty() || vpn_provider_host.empty()) {
      ErrorCallbackForPendingRequest(service_path, kErrorMissingProvider);
      return;
    }
    // VPN requires a host and username to be set.
    if (!VPNIsConfigured(
            service_path, vpn_provider_type, *provider_properties)) {
      NET_LOG_ERROR("VPN Not Configured", service_path);
      ErrorCallbackForPendingRequest(service_path, kErrorConfigurationRequired);
      return;
    }
  }

  client_cert::ConfigType client_cert_type = client_cert::CONFIG_TYPE_NONE;
  if (type == flimflam::kTypeVPN) {
    if (vpn_provider_type == flimflam::kProviderOpenVpn)
      client_cert_type = client_cert::CONFIG_TYPE_OPENVPN;
    else
      client_cert_type = client_cert::CONFIG_TYPE_IPSEC;
  } else if (type == flimflam::kTypeWifi &&
             security == flimflam::kSecurity8021x) {
    client_cert_type = client_cert::CONFIG_TYPE_EAP;
  }

  base::DictionaryValue config_properties;
  if (client_cert_type != client_cert::CONFIG_TYPE_NONE) {
    // If the client certificate must be configured, this will be set to a
    // non-empty string.
    std::string pkcs11_id;

    // Check certificate properties in kUIDataProperty if configured.
    // Note: Wifi/VPNConfigView set these properties explicitly, in which case
    //   only the TPM must be configured.
    scoped_ptr<NetworkUIData> ui_data =
        ManagedNetworkConfigurationHandler::GetUIData(service_properties);
    if (ui_data && ui_data->certificate_type() == CLIENT_CERT_TYPE_PATTERN) {
      // User must be logged in to connect to a network requiring a certificate.
      if (!logged_in_ || !cert_loader_) {
        ErrorCallbackForPendingRequest(service_path, kErrorCertificateRequired);
        return;
      }

      // If certificates have not been loaded yet, queue the connect request.
      if (!certificates_loaded_) {
        ConnectRequest* request = GetPendingRequest(service_path);
        if (!request) {
          NET_LOG_ERROR("No pending request to queue", service_path);
          return;
        }
        NET_LOG_EVENT("Connect Request Queued", service_path);
        queued_connect_.reset(new ConnectRequest(
            service_path, request->success_callback, request->error_callback));
        pending_requests_.erase(service_path);
        return;
      }

      pkcs11_id = CertificateIsConfigured(ui_data.get());
      // Ensure the certificate is available and configured.
      if (!cert_loader_->IsHardwareBacked() || pkcs11_id.empty()) {
        ErrorCallbackForPendingRequest(service_path, kErrorCertificateRequired);
        return;
      }
    }

    // The network may not be 'Connectable' because the TPM properties are not
    // set up, so configure tpm slot/pin before connecting.
    if (cert_loader_ && cert_loader_->IsHardwareBacked()) {
      // Pass NULL if pkcs11_id is empty, so that it doesn't clear any
      // previously configured client cert.
      client_cert::SetShillProperties(client_cert_type,
                                      cert_loader_->tpm_token_slot(),
                                      cert_loader_->tpm_user_pin(),
                                      pkcs11_id.empty() ? NULL : &pkcs11_id,
                                      &config_properties);
    }
  }

  if (!config_properties.empty()) {
    NET_LOG_EVENT("Configuring Network", service_path);

    // Set configuration properties required by Shill to identify the network.
    config_properties.SetStringWithoutPathExpansion(
        flimflam::kTypeProperty, type);
    CopyStringFromDictionary(service_properties, flimflam::kNameProperty,
                             &config_properties);
    CopyStringFromDictionary(service_properties, flimflam::kGuidProperty,
                             &config_properties);
    if (type == flimflam::kTypeVPN) {
      config_properties.SetStringWithoutPathExpansion(
          flimflam::kProviderTypeProperty, vpn_provider_type);
      config_properties.SetStringWithoutPathExpansion(
          flimflam::kProviderHostProperty, vpn_provider_host);
    } else if (type == flimflam::kTypeWifi) {
      config_properties.SetStringWithoutPathExpansion(
          flimflam::kSecurityProperty, security);
    }

    network_configuration_handler_->SetProperties(
        service_path,
        config_properties,
        base::Bind(&NetworkConnectionHandler::CallShillConnect,
                   AsWeakPtr(), service_path),
        base::Bind(&NetworkConnectionHandler::HandleConfigurationFailure,
                   AsWeakPtr(), service_path));
   return;
  }

  // Otherwise, we probably still need to configure the network since
  // 'Connectable' is false. If |check_error_state| is true, signal an
  // error, otherwise attempt to connect to possibly gain additional error
  // state from Shill (or in case 'Connectable' is improperly unset).
  if (check_error_state)
    ErrorCallbackForPendingRequest(service_path, kErrorConfigurationRequired);
  else
    CallShillConnect(service_path);
}

void NetworkConnectionHandler::CallShillConnect(
    const std::string& service_path) {
  NET_LOG_EVENT("Sending Connect Request to Shill", service_path);
  DBusThreadManager::Get()->GetShillServiceClient()->Connect(
      dbus::ObjectPath(service_path),
      base::Bind(&NetworkConnectionHandler::HandleShillConnectSuccess,
                 AsWeakPtr(), service_path),
      base::Bind(&NetworkConnectionHandler::HandleShillConnectFailure,
                 AsWeakPtr(), service_path));
}

void NetworkConnectionHandler::HandleConfigurationFailure(
    const std::string& service_path,
    const std::string& error_name,
    scoped_ptr<base::DictionaryValue> error_data) {
  ConnectRequest* request = GetPendingRequest(service_path);
  if (!request) {
    NET_LOG_ERROR("HandleConfigurationFailure called with no pending request.",
                  service_path);
    return;
  }
  network_handler::ErrorCallback error_callback = request->error_callback;
  pending_requests_.erase(service_path);
  if (!error_callback.is_null())
    error_callback.Run(kErrorConfigureFailed, error_data.Pass());
}

void NetworkConnectionHandler::HandleShillConnectSuccess(
    const std::string& service_path) {
  ConnectRequest* request = GetPendingRequest(service_path);
  if (!request) {
    NET_LOG_ERROR("HandleShillConnectSuccess called with no pending request.",
                  service_path);
    return;
  }
  request->connect_state = ConnectRequest::CONNECT_STARTED;
  NET_LOG_EVENT("Connect Request Acknowledged", service_path);
  // Do not call success_callback here, wait for one of the following
  // conditions:
  // * State transitions to a non connecting state indicating succes or failure
  // * Network is no longer in the visible list, indicating failure
  CheckPendingRequest(service_path);
}

void NetworkConnectionHandler::HandleShillConnectFailure(
    const std::string& service_path,
    const std::string& dbus_error_name,
    const std::string& dbus_error_message) {
  ConnectRequest* request = GetPendingRequest(service_path);
  if (!request) {
    NET_LOG_ERROR("HandleShillConnectFailure called with no pending request.",
                  service_path);
    return;
  }
  network_handler::ErrorCallback error_callback = request->error_callback;
  pending_requests_.erase(service_path);
  network_handler::ShillErrorCallbackFunction(
      kErrorConnectFailed, service_path, error_callback,
      dbus_error_name, dbus_error_message);
}

void NetworkConnectionHandler::CheckPendingRequest(
    const std::string service_path) {
  ConnectRequest* request = GetPendingRequest(service_path);
  DCHECK(request);
  if (request->connect_state == ConnectRequest::CONNECT_REQUESTED)
    return;  // Request has not started, ignore update
  const NetworkState* network =
      network_state_handler_->GetNetworkState(service_path);
  if (!network)
    return;  // NetworkState may not be be updated yet.

  if (network->IsConnectingState()) {
    request->connect_state = ConnectRequest::CONNECT_CONNECTING;
    return;
  }
  if (network->IsConnectedState()) {
    NET_LOG_EVENT("Connect Request Succeeded", service_path);
    if (!request->success_callback.is_null())
      request->success_callback.Run();
    pending_requests_.erase(service_path);
    return;
  }
  if (network->connection_state() == flimflam::kStateIdle &&
      request->connect_state != ConnectRequest::CONNECT_CONNECTING) {
    // Connection hasn't started yet, keep waiting.
    return;
  }

  // Network is neither connecting or connected; an error occurred.
  std::string error_name, error_detail;
  if (network->connection_state() == flimflam::kStateIdle &&
      pending_requests_.size() > 1) {
    // Another connect request canceled this one.
    error_name = kErrorConnectCanceled;
    error_detail = "";
  } else {
    error_name = kErrorConnectFailed;
    error_detail = network->error();
    if (error_detail.empty()) {
      if (network->connection_state() == flimflam::kStateFailure)
        error_detail = flimflam::kUnknownString;
      else
        error_detail = "Unexpected State: " + network->connection_state();
    }
  }
  std::string error_msg = error_name + ": " + error_detail;
  NET_LOG_ERROR(error_msg, service_path);

  network_handler::ErrorCallback error_callback = request->error_callback;
  pending_requests_.erase(service_path);
  if (error_callback.is_null())
    return;
  scoped_ptr<base::DictionaryValue> error_data(
      network_handler::CreateErrorData(service_path, error_name, error_msg));
  error_callback.Run(error_name, error_data.Pass());
}

void NetworkConnectionHandler::CheckAllPendingRequests() {
  for (std::map<std::string, ConnectRequest>::iterator iter =
           pending_requests_.begin(); iter != pending_requests_.end(); ++iter) {
    CheckPendingRequest(iter->first);
  }
}

std::string NetworkConnectionHandler::CertificateIsConfigured(
    NetworkUIData* ui_data) {
  if (ui_data->certificate_pattern().Empty())
    return std::string();
  // Find the matching certificate.
  scoped_refptr<net::X509Certificate> matching_cert =
      client_cert::GetCertificateMatch(ui_data->certificate_pattern());
  if (!matching_cert.get())
    return std::string();
  return CertLoader::GetPkcs11IdForCert(*matching_cert.get());
}

void NetworkConnectionHandler::ErrorCallbackForPendingRequest(
    const std::string& service_path,
    const std::string& error_name) {
  ConnectRequest* request = GetPendingRequest(service_path);
  if (!request) {
    NET_LOG_ERROR("ErrorCallbackForPendingRequest with no pending request.",
                  service_path);
    return;
  }
  // Remove the entry before invoking the callback in case it triggers a retry.
  network_handler::ErrorCallback error_callback = request->error_callback;
  pending_requests_.erase(service_path);
  InvokeErrorCallback(service_path, error_callback, error_name);
}

// Disconnect

void NetworkConnectionHandler::CallShillDisconnect(
    const std::string& service_path,
    const base::Closure& success_callback,
    const network_handler::ErrorCallback& error_callback) {
  NET_LOG_USER("Disconnect Request", service_path);
  DBusThreadManager::Get()->GetShillServiceClient()->Disconnect(
      dbus::ObjectPath(service_path),
      base::Bind(&NetworkConnectionHandler::HandleShillDisconnectSuccess,
                 AsWeakPtr(), service_path, success_callback),
      base::Bind(&network_handler::ShillErrorCallbackFunction,
                 kErrorShillError, service_path, error_callback));
}

void NetworkConnectionHandler::HandleShillDisconnectSuccess(
    const std::string& service_path,
    const base::Closure& success_callback) {
  NET_LOG_EVENT("Disconnect Request Sent", service_path);
  if (!success_callback.is_null())
    success_callback.Run();
}

// Activate

void NetworkConnectionHandler::CallShillActivate(
    const std::string& service_path,
    const std::string& carrier,
    const base::Closure& success_callback,
    const network_handler::ErrorCallback& error_callback) {
  NET_LOG_USER("Activate Request", service_path + ": '" + carrier + "'");
  DBusThreadManager::Get()->GetShillServiceClient()->ActivateCellularModem(
      dbus::ObjectPath(service_path),
      carrier,
      base::Bind(&NetworkConnectionHandler::HandleShillActivateSuccess,
                 AsWeakPtr(), service_path, success_callback),
      base::Bind(&network_handler::ShillErrorCallbackFunction,
                 kErrorShillError, service_path, error_callback));
}

void NetworkConnectionHandler::HandleShillActivateSuccess(
    const std::string& service_path,
    const base::Closure& success_callback) {
  NET_LOG_EVENT("Activate Request Sent", service_path);
  if (!success_callback.is_null())
    success_callback.Run();
}

}  // namespace chromeos
