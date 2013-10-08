// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_CROS_MOCK_NETWORK_LIBRARY_H_
#define CHROME_BROWSER_CHROMEOS_CROS_MOCK_NETWORK_LIBRARY_H_

#include <string>

#include "chrome/browser/chromeos/cros/network_library.h"
#include "chromeos/network/onc/onc_constants.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace chromeos {

class MockNetworkLibrary : public NetworkLibrary {
 public:
  MockNetworkLibrary();
  virtual ~MockNetworkLibrary();

  MOCK_METHOD0(Init, void(void));
  MOCK_CONST_METHOD0(IsCros, bool(void));

  MOCK_METHOD1(AddNetworkProfileObserver, void(NetworkProfileObserver*));
  MOCK_METHOD1(RemoveNetworkProfileObserver, void(NetworkProfileObserver*));
  MOCK_METHOD1(AddNetworkManagerObserver, void(NetworkManagerObserver*));
  MOCK_METHOD1(RemoveNetworkManagerObserver, void(NetworkManagerObserver*));
  MOCK_METHOD2(AddNetworkObserver, void(const std::string&, NetworkObserver*));
  MOCK_METHOD2(RemoveNetworkObserver, void(const std::string&,
                                           NetworkObserver*));
  MOCK_METHOD1(RemoveObserverForAllNetworks, void(NetworkObserver*));
  MOCK_METHOD2(AddNetworkDeviceObserver, void(const std::string&,
                                              NetworkDeviceObserver*));
  MOCK_METHOD2(RemoveNetworkDeviceObserver, void(const std::string&,
                                                 NetworkDeviceObserver*));
  MOCK_METHOD1(AddPinOperationObserver, void(PinOperationObserver*));
  MOCK_METHOD1(RemovePinOperationObserver, void(PinOperationObserver*));
  MOCK_CONST_METHOD0(ethernet_network, const EthernetNetwork*(void));
  MOCK_CONST_METHOD0(ethernet_connecting, bool(void));
  MOCK_CONST_METHOD0(ethernet_connected, bool(void));

  MOCK_CONST_METHOD0(wifi_network, const WifiNetwork*(void));
  MOCK_CONST_METHOD0(wifi_connecting, bool(void));
  MOCK_CONST_METHOD0(wifi_connected, bool(void));

  MOCK_CONST_METHOD0(cellular_network, const CellularNetwork*(void));
  MOCK_CONST_METHOD0(cellular_connecting, bool(void));
  MOCK_CONST_METHOD0(cellular_connected, bool(void));

  MOCK_CONST_METHOD0(wimax_network, const WimaxNetwork*(void));
  MOCK_CONST_METHOD0(wimax_connecting, bool(void));
  MOCK_CONST_METHOD0(wimax_connected, bool(void));

  MOCK_CONST_METHOD0(virtual_network, const VirtualNetwork*(void));
  MOCK_CONST_METHOD0(virtual_network_connecting, bool(void));
  MOCK_CONST_METHOD0(virtual_network_connected, bool(void));

  MOCK_CONST_METHOD0(Connected, bool(void));
  MOCK_CONST_METHOD0(Connecting, bool(void));

  MOCK_CONST_METHOD0(wifi_networks, const WifiNetworkVector&(void));
  MOCK_CONST_METHOD0(remembered_wifi_networks, const WifiNetworkVector&(void));
  MOCK_CONST_METHOD0(cellular_networks, const CellularNetworkVector&(void));
  MOCK_CONST_METHOD0(virtual_networks, const VirtualNetworkVector&(void));
  MOCK_CONST_METHOD0(wimax_networks, const WimaxNetworkVector&(void));
  MOCK_CONST_METHOD0(remembered_virtual_networks,
                     const VirtualNetworkVector&(void));

  MOCK_CONST_METHOD1(FindNetworkDeviceByPath,
                     NetworkDevice*(const std::string&));
  MOCK_CONST_METHOD0(FindCellularDevice, const NetworkDevice*(void));
  MOCK_CONST_METHOD0(FindWimaxDevice, const NetworkDevice*(void));
  MOCK_CONST_METHOD0(FindMobileDevice, const NetworkDevice*(void));
  MOCK_CONST_METHOD0(FindWifiDevice, const NetworkDevice*(void));
  MOCK_CONST_METHOD0(FindEthernetDevice, const NetworkDevice*(void));
  MOCK_CONST_METHOD1(FindNetworkByPath, Network*(const std::string&));
  MOCK_CONST_METHOD1(FindNetworkByUniqueId, Network*(const std::string&));
  MOCK_CONST_METHOD1(FindWifiNetworkByPath, WifiNetwork*(const std::string&));
  MOCK_CONST_METHOD1(FindCellularNetworkByPath,
                     CellularNetwork*(const std::string&));
  MOCK_CONST_METHOD1(FindWimaxNetworkByPath,
                     WimaxNetwork*(const std::string&));
  MOCK_CONST_METHOD1(FindVirtualNetworkByPath,
                     VirtualNetwork*(const std::string&));
  MOCK_CONST_METHOD1(FindRememberedNetworkByPath, Network*(const std::string&));
  MOCK_CONST_METHOD1(FindOncForNetwork,
                     const base::DictionaryValue*(
                         const std::string& unique_id));

  MOCK_METHOD2(ChangePin, void(const std::string&, const std::string&));
  MOCK_METHOD2(ChangeRequirePin, void(bool, const std::string&));
  MOCK_METHOD1(EnterPin, void(const std::string&));
  MOCK_METHOD2(UnblockPin, void(const std::string&, const std::string&));

  MOCK_METHOD0(RequestCellularScan, void());
  MOCK_METHOD1(RequestCellularRegister, void(const std::string&));
  MOCK_METHOD1(SetCellularDataRoamingAllowed, void(bool));
  MOCK_METHOD2(SetCarrier, void(const std::string&,
                                const NetworkOperationCallback&));
  MOCK_METHOD0(IsCellularAlwaysInRoaming, bool());

  MOCK_METHOD0(RequestNetworkScan, void(void));
  MOCK_CONST_METHOD1(CanConnectToNetwork, bool(const Network*));
  MOCK_METHOD1(ConnectToWifiNetwork, void(WifiNetwork*));
  MOCK_METHOD2(ConnectToWifiNetwork, void(WifiNetwork*, bool));
  MOCK_METHOD1(ConnectToWimaxNetwork, void(WimaxNetwork*));
  MOCK_METHOD2(ConnectToWimaxNetwork, void(WimaxNetwork*, bool));
  MOCK_METHOD1(ConnectToCellularNetwork, void(CellularNetwork*));
  MOCK_METHOD1(ConnectToVirtualNetwork, void(VirtualNetwork*));
  MOCK_METHOD6(ConnectToUnconfiguredWifiNetwork,
               void(const std::string&,
                    ConnectionSecurity,
                    const std::string&,
                    const EAPConfigData*,
                    bool,
                    bool));
  MOCK_METHOD4(ConnectToUnconfiguredVirtualNetwork,
               void(const std::string&,
                    const std::string&,
                    ProviderType,
                    const VPNConfigData&));
  MOCK_METHOD0(SignalCellularPlanPayment, void(void));
  MOCK_METHOD0(HasRecentCellularPlanPayment, bool(void));

  MOCK_METHOD1(DisconnectFromNetwork, void(const Network*));
  MOCK_METHOD1(ForgetNetwork, void(const std::string&));
  MOCK_CONST_METHOD0(GetCellularHomeCarrierId, const std::string&(void));
  MOCK_CONST_METHOD0(CellularDeviceUsesDirectActivation, bool(void));

  MOCK_CONST_METHOD0(ethernet_available, bool(void));
  MOCK_CONST_METHOD0(wifi_available, bool(void));
  MOCK_CONST_METHOD0(cellular_available, bool(void));
  MOCK_CONST_METHOD0(wimax_available, bool(void));

  MOCK_CONST_METHOD0(ethernet_enabled, bool(void));
  MOCK_CONST_METHOD0(wifi_enabled, bool(void));
  MOCK_CONST_METHOD0(cellular_enabled, bool(void));
  MOCK_CONST_METHOD0(wimax_enabled, bool(void));

  MOCK_CONST_METHOD0(active_network, const Network*(void));
  MOCK_CONST_METHOD0(active_nonvirtual_network, const Network*(void));
  MOCK_CONST_METHOD0(connected_network, const Network*(void));
  MOCK_CONST_METHOD0(connecting_network, const Network*(void));

  MOCK_CONST_METHOD0(wifi_scanning, bool(void));
  MOCK_CONST_METHOD0(cellular_initializing, bool(void));

  MOCK_METHOD1(EnableEthernetNetworkDevice, void(bool));
  MOCK_METHOD1(EnableWifiNetworkDevice, void(bool));
  MOCK_METHOD1(EnableCellularNetworkDevice, void(bool));
  MOCK_METHOD1(EnableWimaxNetworkDevice, void(bool));
  MOCK_METHOD3(GetIPConfigs, void(const std::string&,
                                  HardwareAddressFormat,
                                  const NetworkGetIPConfigsCallback&));
  MOCK_METHOD6(SetIPParameters, void(const std::string&,
                                     const std::string&,
                                     const std::string&,
                                     const std::string&,
                                     const std::string&,
                                     int));
  MOCK_METHOD2(RequestNetworkServiceProperties,
               void(const std::string&,
                    const NetworkServicePropertiesCallback&));
  MOCK_METHOD2(LoadOncNetworks, void(const base::ListValue&,
                                     onc::ONCSource));
  MOCK_METHOD2(SetActiveNetwork, bool(ConnectionType, const std::string&));
};

class MockCellularNetwork : public CellularNetwork {
 public:
  explicit MockCellularNetwork(const std::string& service_path);
  virtual ~MockCellularNetwork();

  MOCK_METHOD0(StartActivation, bool(void));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockCellularNetwork);
};


}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_CROS_MOCK_NETWORK_LIBRARY_H_
