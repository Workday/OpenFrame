// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_BLUETOOTH_PROFILE_SERVICE_PROVIDER_H_
#define CHROMEOS_DBUS_BLUETOOTH_PROFILE_SERVICE_PROVIDER_H_

#include <string>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "chromeos/chromeos_export.h"
#include "dbus/bus.h"
#include "dbus/file_descriptor.h"
#include "dbus/object_path.h"

namespace chromeos {

// BluetoothProfileServiceProvider is used to provide a D-Bus object that the
// Bluetooth daemon can communicate with to connect application profiles.
//
// Instantiate with a chosen D-Bus object path and delegate object, and pass
// the D-Bus object path as the |agent_path| argument to the
// chromeos::BluetoothProfileManagerClient::RegisterProfile() method.
//
// When an incoming profile connection occurs, or after initiating a connection
// using the chromeos::BluetoothDeviceClient::ConnectProfile() method, the
// Bluetooth daemon will make calls to this profile object and they will be
// passed on to your Delegate object for handling. Responses should be returned
// using the callbacks supplied to those methods.
class CHROMEOS_EXPORT BluetoothProfileServiceProvider {
 public:
  // Interface for reacting to profile requests.
  class Delegate {
   public:
    virtual ~Delegate() {}

    // Possible status values that may be returned to callbacks on a new
    // connection or a requested disconnection. Success indicates acceptance,
    // reject indicates the user rejected or denied the request; cancelled
    // means the user cancelled the request without confirming either way.
    enum Status {
      SUCCESS,
      REJECTED,
      CANCELLED
    };

    // Connection-specific options.
    struct CHROMEOS_EXPORT Options {
      Options() {}
      ~Options() {}

      // Profile version.
      uint16 version;

      // Profile features.
      uint16 features;
    };

    // The ConfirmationCallback is used for methods which require confirmation;
    // it should be called with one argument, the |status| of the request
    // (success, rejected or cancelled).
    typedef base::Callback<void(Status)> ConfirmationCallback;

    // This method will be called when the profile is unregistered from the
    // Bluetooth daemon, generally at shutdown or at the applications' request.
    // It may be used to perform cleanup tasks.
    virtual void Release() = 0;

    // This method will be called when a profile connection to the device
    // with object path |device_path| is established. |callback| must be called
    // to confirm the connection, or indicate rejection or cancellation.
    //
    // A file descriptor for the connection socket is provided in |fd|, and
    // details about the specific implementation of the profile in |options|.
    //
    // IMPORTANT: Ownership of the file descriptor object |fd| is passed to
    // the delegate by this call. The delegate is responsible for checking the
    // validity of |fd| on a thread where I/O is permitted before taking the
    // value. If the value is not taken, the file descriptor is closed.
    //
    // Ownership of |options| is NOT passed so information out of it must be
    // copied if required.
    virtual void NewConnection(const dbus::ObjectPath& device_path,
                               scoped_ptr<dbus::FileDescriptor> fd,
                               const Options& options,
                               const ConfirmationCallback& callback) = 0;

    // This method will be called when a profile connection to the device
    // with object path |device_path| is disconnected. Any file descriptors
    // owned by the service should be cleaned up and |callback| called to
    // confirm, or indicate rejection or cancellation of the disconnection.
    virtual void RequestDisconnection(const dbus::ObjectPath& device_path,
                                      const ConfirmationCallback& callback) = 0;

    // This method will be called by the Bluetooth daemon to indicate that
    // a profile request failed before a reply was returned from the device.
    virtual void Cancel() = 0;
  };

  virtual ~BluetoothProfileServiceProvider();

  // Creates the instance where |bus| is the D-Bus bus connection to export
  // the object onto, |object_path| is the object path that it should have
  // and |delegate| is the object to which all method calls will be passed
  // and responses generated from.
  static BluetoothProfileServiceProvider* Create(
      dbus::Bus* bus,
      const dbus::ObjectPath& object_path,
      Delegate* delegate);

 protected:
  BluetoothProfileServiceProvider();

 private:
  DISALLOW_COPY_AND_ASSIGN(BluetoothProfileServiceProvider);
};

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_BLUETOOTH_PROFILE_SERVICE_PROVIDER_H_
