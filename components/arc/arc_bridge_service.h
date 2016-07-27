// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ARC_ARC_BRIDGE_SERVICE_H_
#define COMPONENTS_ARC_ARC_BRIDGE_SERVICE_H_

#include "base/files/scoped_file.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "components/arc/common/arc_message_types.h"

namespace base {
class CommandLine;
class SequencedTaskRunner;
class SingleThreadTaskRunner;
}  // namespace base

namespace arc {

// The Chrome-side service that handles ARC instances and ARC bridge creation.
// This service handles the lifetime of ARC instances and sets up the
// communication channel (the ARC bridge) used to send and receive messages.
class ArcBridgeService {
 public:
  // The possible states of the bridge.  In the normal flow, the state changes
  // in the following sequence:
  //
  // STOPPED
  //   SetAvailable(true) + HandleStartup() -> SocketConnect() ->
  // CONNECTING
  //   Connect() ->
  // CONNECTED
  //   SocketConnectAfterSetSocketPermissions() ->
  // STARTING
  //   StartInstance() -> OnInstanceReady() ->
  // READY
  //
  // When Shutdown() is called, the state changes depending on the state it was
  // at:
  //
  // CONNECTED/CONNECTING -> STOPPED
  // STARTING/READY -> STOPPING -> StopInstance() -> STOPPED
  enum class State {
    // ARC is not currently running.
    STOPPED,

    // The request to connect has been sent.
    CONNECTING,

    // The bridge has connected to the socket, but has not started the ARC
    // instance.
    CONNECTED,

    // The ARC bridge has been set up and ARC is starting up.
    STARTING,

    // The ARC instance has been fully initialized and is now ready for user
    // interaction.
    READY,

    // The ARC instance has started shutting down.
    STOPPING,
  };

  class Observer {
   public:
    // Called whenever the state of the bridge has changed.
    virtual void OnStateChanged(State state) {}

    // Called when the instance has reached a boot phase
    virtual void OnInstanceBootPhase(InstanceBootPhase phase) {}

    // Called whenever ARC's availability has changed for this system.
    virtual void OnAvailableChanged(bool available) {}

   protected:
    virtual ~Observer() {}
  };

  virtual ~ArcBridgeService();

  // Creates instance of |ArcBridgeService| for normal use.
  static scoped_ptr<ArcBridgeService> Create(
      const scoped_refptr<base::SingleThreadTaskRunner>& ipc_task_runner,
      const scoped_refptr<base::SequencedTaskRunner>& file_task_runner);

  // Gets the global instance of the ARC Bridge Service. This can only be
  // called on the thread that this class was created on.
  static ArcBridgeService* Get();

  // Return true if ARC has been enabled through a commandline
  // switch.
  static bool GetEnabled(const base::CommandLine* command_line);

  // DetectAvailability() should be called once D-Bus is available. It will
  // call CheckArcAvailability() on the session_manager. This can only be
  // called on the thread that this class was created on.
  virtual void DetectAvailability() = 0;

  // HandleStartup() should be called upon profile startup.  This will only
  // launch an instance if the instance service is available and it is enabled.
  // This can only be called on the thread that this class was created on.
  virtual void HandleStartup() = 0;

  // Shutdown() should be called when the browser is shutting down. This can
  // only be called on the thread that this class was created on.
  virtual void Shutdown() = 0;

  // Adds or removes observers. This can only be called on the thread that this
  // class was created on.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Gets the current state of the bridge service.
  State state() const { return state_; }

  // Gets if ARC is available in this system.
  bool available() const { return available_; }

  // Requests registration of an input device on the ARC instance.
  // TODO(denniskempin): Make this interface more typesafe.
  // |name| should be the displayable name of the emulated device (e.g. "Chrome
  // OS Keyboard"), |device_type| the name of the device type (e.g. "keyboard")
  // and |fd| a file descriptor that emulates the kernel events of the device.
  // This can only be called on the thread that this class was created on.
  virtual bool RegisterInputDevice(const std::string& name,
                                   const std::string& device_type,
                                   base::ScopedFD fd) = 0;

 protected:
  ArcBridgeService();

  // Changes the current state and notifies all observers.
  void SetState(State state);

  // Changes the current availability and notifies all observers.
  void SetAvailable(bool availability);

  const scoped_refptr<base::SequencedTaskRunner>& origin_task_runner() const {
    return origin_task_runner_;
  }

  base::ObserverList<Observer>& observer_list() { return observer_list_; }

 private:
  scoped_refptr<base::SequencedTaskRunner> origin_task_runner_;

  base::ObserverList<Observer> observer_list_;

  // If the ARC instance service is available.
  bool available_;

  // The current state of the bridge.
  ArcBridgeService::State state_;

  DISALLOW_COPY_AND_ASSIGN(ArcBridgeService);
};

}  // namespace arc

#endif  // COMPONENTS_ARC_ARC_BRIDGE_SERVICE_H_
