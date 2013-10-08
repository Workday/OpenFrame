// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_BLUETOOTH_BLUETOOTH_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_BLUETOOTH_BLUETOOTH_API_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/extensions/api/api_function.h"
#include "chrome/browser/extensions/api/bluetooth/bluetooth_extension_function.h"
#include "chrome/browser/extensions/event_router.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_profile.h"

namespace device {

class BluetoothAdapter;
class BluetoothDevice;
class BluetoothSocket;
struct BluetoothOutOfBandPairingData;

}  // namespace device

namespace extensions {

class ExtensionBluetoothEventRouter;

// The profile-keyed service that manages the bluetooth extension API.
class BluetoothAPI : public BrowserContextKeyedService,
                     public EventRouter::Observer {
 public:
  // Convenience method to get the BluetoothAPI for a profile.
  static BluetoothAPI* Get(Profile* profile);

  explicit BluetoothAPI(Profile* profile);
  virtual ~BluetoothAPI();

  ExtensionBluetoothEventRouter* bluetooth_event_router();

  // BrowserContextKeyedService implementation.
  virtual void Shutdown() OVERRIDE;

  // EventRouter::Observer implementation.
  virtual void OnListenerAdded(const EventListenerInfo& details) OVERRIDE;
  virtual void OnListenerRemoved(const EventListenerInfo& details) OVERRIDE;

 private:
  Profile* profile_;

  // Created lazily on first access.
  scoped_ptr<ExtensionBluetoothEventRouter> bluetooth_event_router_;
};

namespace api {

class BluetoothAddProfileFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bluetooth.addProfile", BLUETOOTH_ADDPROFILE)

  BluetoothAddProfileFunction();

 protected:
  virtual ~BluetoothAddProfileFunction() {}
  virtual bool RunImpl() OVERRIDE;

  virtual void RegisterProfile(
      const device::BluetoothProfile::Options& options,
      const device::BluetoothProfile::ProfileCallback& callback);

 private:
  void OnProfileRegistered(device::BluetoothProfile* bluetooth_profile);

  std::string uuid_;
};

class BluetoothRemoveProfileFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bluetooth.removeProfile",
                             BLUETOOTH_REMOVEPROFILE)

 protected:
  virtual ~BluetoothRemoveProfileFunction() {}
  virtual bool RunImpl() OVERRIDE;
};

class BluetoothGetProfilesFunction : public BluetoothExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bluetooth.getProfiles", BLUETOOTH_GETPROFILES)

 protected:
  virtual ~BluetoothGetProfilesFunction() {}

  // BluetoothExtensionFunction:
  virtual bool DoWork(scoped_refptr<device::BluetoothAdapter> adapter) OVERRIDE;
};

class BluetoothGetAdapterStateFunction : public BluetoothExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bluetooth.getAdapterState",
                             BLUETOOTH_GETADAPTERSTATE)

 protected:
  virtual ~BluetoothGetAdapterStateFunction() {}

  // BluetoothExtensionFunction:
  virtual bool DoWork(scoped_refptr<device::BluetoothAdapter> adapter) OVERRIDE;
};

class BluetoothGetDevicesFunction : public BluetoothExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bluetooth.getDevices", BLUETOOTH_GETDEVICES)

  BluetoothGetDevicesFunction();

 protected:
  virtual ~BluetoothGetDevicesFunction() {}

  // BluetoothExtensionFunction:
  virtual bool DoWork(scoped_refptr<device::BluetoothAdapter> adapter) OVERRIDE;

 private:
  void DispatchDeviceSearchResult(const device::BluetoothDevice& device);
  void FinishDeviceSearch();

  int device_events_sent_;
};

class BluetoothGetServicesFunction : public BluetoothExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bluetooth.getServices", BLUETOOTH_GETSERVICES)

 protected:
  virtual ~BluetoothGetServicesFunction() {}

  // BluetoothExtensionFunction:
  virtual bool DoWork(scoped_refptr<device::BluetoothAdapter> adapter) OVERRIDE;

 private:
  void GetServiceRecordsCallback(
      base::ListValue* services,
      const device::BluetoothDevice::ServiceRecordList& records);
  void OnErrorCallback();
};

class BluetoothConnectFunction : public BluetoothExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bluetooth.connect", BLUETOOTH_CONNECT)

 protected:
  virtual ~BluetoothConnectFunction() {}

  // BluetoothExtensionFunction:
  virtual bool DoWork(scoped_refptr<device::BluetoothAdapter> adapter) OVERRIDE;

 private:
  void OnSuccessCallback();
  void OnErrorCallback();
};

class BluetoothDisconnectFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bluetooth.disconnect", BLUETOOTH_DISCONNECT)

 protected:
  virtual ~BluetoothDisconnectFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class BluetoothReadFunction : public AsyncApiFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bluetooth.read", BLUETOOTH_READ)
  BluetoothReadFunction();

 protected:
  virtual ~BluetoothReadFunction();

  // AsyncApiFunction:
  virtual bool Prepare() OVERRIDE;
  virtual bool Respond() OVERRIDE;
  virtual void Work() OVERRIDE;

 private:
  bool success_;
  scoped_refptr<device::BluetoothSocket> socket_;
};

class BluetoothWriteFunction : public AsyncApiFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bluetooth.write", BLUETOOTH_WRITE)
  BluetoothWriteFunction();

 protected:
  virtual ~BluetoothWriteFunction();

  // AsyncApiFunction:
  virtual bool Prepare() OVERRIDE;
  virtual bool Respond() OVERRIDE;
  virtual void Work() OVERRIDE;

 private:
  bool success_;
  const base::BinaryValue* data_to_write_;  // memory is owned by args_
  scoped_refptr<device::BluetoothSocket> socket_;
};

class BluetoothSetOutOfBandPairingDataFunction
    : public BluetoothExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bluetooth.setOutOfBandPairingData",
                             BLUETOOTH_SETOUTOFBANDPAIRINGDATA)

 protected:
  virtual ~BluetoothSetOutOfBandPairingDataFunction() {}

  void OnSuccessCallback();
  void OnErrorCallback();

  // BluetoothExtensionFunction:
  virtual bool DoWork(scoped_refptr<device::BluetoothAdapter> adapter) OVERRIDE;
};

class BluetoothGetLocalOutOfBandPairingDataFunction
    : public BluetoothExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bluetooth.getLocalOutOfBandPairingData",
                             BLUETOOTH_GETLOCALOUTOFBANDPAIRINGDATA)

 protected:
  virtual ~BluetoothGetLocalOutOfBandPairingDataFunction() {}

  void ReadCallback(
      const device::BluetoothOutOfBandPairingData& data);
  void ErrorCallback();

  // BluetoothExtensionFunction:
  virtual bool DoWork(scoped_refptr<device::BluetoothAdapter> adapter) OVERRIDE;
};

class BluetoothStartDiscoveryFunction : public BluetoothExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bluetooth.startDiscovery",
                             BLUETOOTH_STARTDISCOVERY)

 protected:
  virtual ~BluetoothStartDiscoveryFunction() {}

  // BluetoothExtensionFunction:
  virtual bool DoWork(scoped_refptr<device::BluetoothAdapter> adapter) OVERRIDE;

 private:
  void OnSuccessCallback();
  void OnErrorCallback();
};

class BluetoothStopDiscoveryFunction : public BluetoothExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bluetooth.stopDiscovery", BLUETOOTH_STOPDISCOVERY)

 protected:
  virtual ~BluetoothStopDiscoveryFunction() {}

  // BluetoothExtensionFunction:
  virtual bool DoWork(scoped_refptr<device::BluetoothAdapter> adapter) OVERRIDE;

 private:
  void OnSuccessCallback();
  void OnErrorCallback();
};

}  // namespace api
}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_BLUETOOTH_BLUETOOTH_API_H_
