// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/cros/native_network_constants.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

// Format of the Carrier ID: <carrier name> (<carrier country>).
const char kCarrierIdFormat[] = "%s (%s)";

// Path of the default (shared) shill profile.
const char kSharedProfilePath[] = "/profile/default";

const char* ConnectionStateToString(ConnectionState state) {
  switch (state) {
    case STATE_UNKNOWN:
      return flimflam::kUnknownString;
    case STATE_IDLE:
      return flimflam::kStateIdle;
    case STATE_CARRIER:
      return flimflam::kStateCarrier;
    case STATE_ASSOCIATION:
      return flimflam::kStateAssociation;
    case STATE_CONFIGURATION:
      return flimflam::kStateConfiguration;
    case STATE_READY:
      return flimflam::kStateReady;
    case STATE_DISCONNECT:
      return flimflam::kStateDisconnect;
    case STATE_FAILURE:
      return flimflam::kStateFailure;
    case STATE_ACTIVATION_FAILURE:
      return flimflam::kStateActivationFailure;
    case STATE_PORTAL:
      return flimflam::kStatePortal;
    case STATE_ONLINE:
      return flimflam::kStateOnline;
    case STATE_CONNECT_REQUESTED:
      return "ConnectRequested";  // Internal non-flimflam state
  }
  LOG(ERROR) << "ConnectionStateToString called with unknown state: " << state;
  return flimflam::kUnknownString;
}

const char* ConnectionTypeToString(ConnectionType type) {
  switch (type) {
    case TYPE_UNKNOWN:
      break;
    case TYPE_ETHERNET:
      return flimflam::kTypeEthernet;
    case TYPE_WIFI:
      return flimflam::kTypeWifi;
    case TYPE_WIMAX:
      return flimflam::kTypeWimax;
    case TYPE_BLUETOOTH:
      return flimflam::kTypeBluetooth;
    case TYPE_CELLULAR:
      return flimflam::kTypeCellular;
    case TYPE_VPN:
      return flimflam::kTypeVPN;
  }
  LOG(ERROR) << "ConnectionTypeToString called with unknown type: " << type;
  return flimflam::kUnknownString;
}

const char* SecurityToString(ConnectionSecurity security) {
  switch (security) {
    case SECURITY_NONE:
      return flimflam::kSecurityNone;
    case SECURITY_WEP:
      return flimflam::kSecurityWep;
    case SECURITY_WPA:
      return flimflam::kSecurityWpa;
    case SECURITY_RSN:
      return flimflam::kSecurityRsn;
    case SECURITY_8021X:
      return flimflam::kSecurity8021x;
    case SECURITY_PSK:
      return flimflam::kSecurityPsk;
    case SECURITY_UNKNOWN:
      break;
  }
  LOG(ERROR) << "SecurityToString called with unknown type: " << security;
  return flimflam::kUnknownString;
}

const char* ProviderTypeToString(ProviderType type) {
  switch (type) {
    case PROVIDER_TYPE_L2TP_IPSEC_PSK:
    case PROVIDER_TYPE_L2TP_IPSEC_USER_CERT:
      return flimflam::kProviderL2tpIpsec;
    case PROVIDER_TYPE_OPEN_VPN:
      return flimflam::kProviderOpenVpn;
    case PROVIDER_TYPE_MAX:
      break;
  }
  LOG(ERROR) << "ProviderTypeToString called with unknown type: " << type;
  return flimflam::kUnknownString;
}

}  // namespace chromeos
