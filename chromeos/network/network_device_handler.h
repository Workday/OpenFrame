// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_NETWORK_NETWORK_DEVICE_HANDLER_H_
#define CHROMEOS_NETWORK_NETWORK_DEVICE_HANDLER_H_

#include <string>

#include "base/callback.h"
#include "base/gtest_prod_util.h"
#include "base/memory/weak_ptr.h"
#include "chromeos/chromeos_export.h"
#include "chromeos/network/network_handler.h"
#include "chromeos/network/network_handler_callbacks.h"

namespace base {

class Value;

}  // namespace base

namespace chromeos {

// The NetworkDeviceHandler class allows making device specific requests on a
// ChromeOS network device. All calls are asynchronous and interact with the
// Shill device API. No calls will block on DBus calls.
//
// This is owned and its lifetime managed by the Chrome startup code. It's
// basically a singleton, but with explicit lifetime management.
//
// Note on callbacks: Because all the functions here are meant to be
// asynchronous, they all take a |callback| of some type, and an
// |error_callback|. When the operation succeeds, |callback| will be called, and
// when it doesn't, |error_callback| will be called with information about the
// error, including a symbolic name for the error and often some error message
// that is suitable for logging. None of the error message text is meant for
// user consumption.

class CHROMEOS_EXPORT NetworkDeviceHandler {
 public:

  // Constants for |error_name| from |error_callback|.
  static const char kErrorFailure[];
  static const char kErrorIncorrectPin[];
  static const char kErrorNotFound[];
  static const char kErrorNotSupported[];
  static const char kErrorPinBlocked[];
  static const char kErrorPinRequired[];
  static const char kErrorUnknown[];

  virtual ~NetworkDeviceHandler();

  // Gets the properties of the device with id |device_path|. See note on
  // |callback| and |error_callback|, in class description above.
  void GetDeviceProperties(
      const std::string& device_path,
      const network_handler::DictionaryResultCallback& callback,
      const network_handler::ErrorCallback& error_callback) const;

  // Sets the value of property |name| on device with id |device_path| to
  // |value|.
  void SetDeviceProperty(
      const std::string& device_path,
      const std::string& name,
      const base::Value& value,
      const base::Closure& callback,
      const network_handler::ErrorCallback& error_callback);

  // Requests a refresh of the IP configuration for the device specified by
  // |device_path| if it exists. This will apply any newly configured
  // properties and renew the DHCP lease.
  void RequestRefreshIPConfigs(
      const std::string& device_path,
      const base::Closure& callback,
      const network_handler::ErrorCallback& error_callback);

  // Requests a network scan on the device specified by |device_path|.
  // For cellular networks, the result of this call gets asynchronously stored
  // in the corresponding DeviceState object through a property update. For all
  // other technologies a service gets created for each found network, which
  // can be accessed through the corresponding NetworkState object.
  //
  // TODO(armansito): Device.ProposeScan is deprecated and the preferred method
  // of requesting a network scan is Manager.RequestScan, however shill
  // currently doesn't support cellular network scans via Manager.RequestScan.
  // Remove this method once shill supports it (crbug.com/262356).
  void ProposeScan(
      const std::string& device_path,
      const base::Closure& callback,
      const network_handler::ErrorCallback& error_callback);

  // Tells the device specified by |device_path| to register to the cellular
  // network with id |network_id|. If |network_id| is empty then registration
  // will proceed in automatic mode, which will cause the modem to register
  // with the home network.
  // This call is only available on cellular devices and will fail with
  // Error.NotSupported on all other technologies.
  void RegisterCellularNetwork(
      const std::string& device_path,
      const std::string& network_id,
      const base::Closure& callback,
      const network_handler::ErrorCallback& error_callback);

  // Tells the device to set the modem carrier firmware, as specified by
  // |carrier|.
  //
  // See note on |callback| and |error_callback| in the class description
  // above. The operation will fail if:
  //    - Device |device_path| could not be found.
  //    - |carrier| doesn't match one of the supported carriers, as reported by
  //    - Shill.
  //    - Operation is not supported by the device.
  void SetCarrier(
      const std::string& device_path,
      const std::string& carrier,
      const base::Closure& callback,
      const network_handler::ErrorCallback& error_callback);

  // SIM PIN/PUK methods

  // Tells the device whether or not a SIM PIN lock should be enforced by
  // the device referenced by |device_path|. If |require_pin| is true, a PIN
  // code (specified in |pin|) will be required before the next time the device
  // can be enabled. If |require_pin| is false, the existing requirement will
  // be lifted.
  //
  // See note on |callback| and |error_callback| in the class description
  // above. The operation will fail if:
  //    - Device |device_path| could not be found.
  //    - The PIN requirement status already matches |require_pin|.
  //    - |pin| doesn't match the PIN code currently stored by the SIM.
  //    - No SIM exists on the device.
  //
  // This method applies to Cellular devices only. The call will fail with a
  // "not-supported" error if called on a non-cellular device.
  void RequirePin(
      const std::string& device_path,
      bool require_pin,
      const std::string& pin,
      const base::Closure& callback,
      const network_handler::ErrorCallback& error_callback);

  // Sends the PIN code |pin| to the device |device_path|.
  //
  // See note on |callback| and |error_callback| in the class description
  // above. The operation will fail if:
  //    - Device |device_path| could not be found.
  //    - |pin| is incorrect.
  //    - The SIM is blocked.
  //
  // This method applies to Cellular devices only. The call will fail with a
  // "not-supported" error if called on a non-cellular device.
  void EnterPin(
      const std::string& device_path,
      const std::string& pin,
      const base::Closure& callback,
      const network_handler::ErrorCallback& error_callback);

  // Sends the PUK code |puk| to the SIM to unblock a blocked SIM. On success,
  // the SIM will be unblocked and its PIN code will be set to |pin|.
  //
  // See note on |callback| and |error_callback| in the class description
  // above. The operation will fail if:
  //    - Device |device_path| could not be found.
  //    - |puk| is incorrect.
  //
  // This method applies to Cellular devices only. The call will fail with a
  // "not-supported" error if called on a non-cellular device.
  void UnblockPin(
      const std::string& device_path,
      const std::string& puk,
      const std::string& new_pin,
      const base::Closure& callback,
      const network_handler::ErrorCallback& error_callback);

  // Tells the device to change the PIN code used to unlock a locked SIM card.
  //
  // See note on |callback| and |error_callback| in the class description
  // above. The operation will fail if:
  //    - Device |device_path| could not be found.
  //    - |old_pin| does not match the current PIN on the device.
  //    - The SIM is locked.
  //    - The SIM is blocked.
  //
  // This method applies to Cellular devices only. The call will fail with a
  // "not-supported" error if called on a non-cellular device.
  void ChangePin(
      const std::string& device_path,
      const std::string& old_pin,
      const std::string& new_pin,
      const base::Closure& callback,
      const network_handler::ErrorCallback& error_callback);

 private:
  friend class NetworkHandler;
  friend class NetworkDeviceHandlerTest;
  FRIEND_TEST_ALL_PREFIXES(NetworkDeviceHandlerTest, ErrorTranslation);

  NetworkDeviceHandler();

  void HandleShillCallFailureForTest(
      const std::string& device_path,
      const network_handler::ErrorCallback& error_callback,
      const std::string& error_name,
      const std::string& error_message);

  DISALLOW_COPY_AND_ASSIGN(NetworkDeviceHandler);
};

}  // namespace chromeos

#endif  // CHROMEOS_NETWORK_NETWORK_DEVICE_HANDLER_H_
