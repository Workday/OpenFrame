// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/bluetooth/bluetooth_event_router.h"

#include <map>
#include <string>

#include "base/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_vector.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/api/bluetooth/bluetooth_api_utils.h"
#include "chrome/browser/extensions/event_names.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/common/extensions/api/bluetooth.h"
#include "device/bluetooth/bluetooth_adapter.h"
#include "device/bluetooth/bluetooth_adapter_factory.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_profile.h"
#include "device/bluetooth/bluetooth_socket.h"

namespace extensions {

ExtensionBluetoothEventRouter::ExtensionBluetoothEventRouter(Profile* profile)
    : send_discovery_events_(false),
      responsible_for_discovery_(false),
      profile_(profile),
      adapter_(NULL),
      num_event_listeners_(0),
      next_socket_id_(1),
      weak_ptr_factory_(this) {
  DCHECK(profile_);
}

ExtensionBluetoothEventRouter::~ExtensionBluetoothEventRouter() {
  if (adapter_.get()) {
    adapter_->RemoveObserver(this);
    adapter_ = NULL;
  }
  DLOG_IF(WARNING, socket_map_.size() != 0)
      << "Bluetooth sockets are still open.";
  socket_map_.clear();

  for (BluetoothProfileMap::iterator iter = bluetooth_profile_map_.begin();
       iter != bluetooth_profile_map_.end();
       ++iter) {
    iter->second->Unregister();
  }
}

bool ExtensionBluetoothEventRouter::IsBluetoothSupported() const {
  return adapter_.get() ||
         device::BluetoothAdapterFactory::IsBluetoothAdapterAvailable();
}

void ExtensionBluetoothEventRouter::GetAdapter(
    const device::BluetoothAdapterFactory::AdapterCallback& callback) {
  if (adapter_.get()) {
    callback.Run(scoped_refptr<device::BluetoothAdapter>(adapter_));
    return;
  }

  device::BluetoothAdapterFactory::GetAdapter(callback);
}

void ExtensionBluetoothEventRouter::OnListenerAdded() {
  num_event_listeners_++;
  InitializeAdapterIfNeeded();
}

void ExtensionBluetoothEventRouter::OnListenerRemoved() {
  if (num_event_listeners_ > 0)
    num_event_listeners_--;
  MaybeReleaseAdapter();
}

int ExtensionBluetoothEventRouter::RegisterSocket(
    scoped_refptr<device::BluetoothSocket> socket) {
  // If there is a socket registered with the same fd, just return it's id
  for (SocketMap::const_iterator i = socket_map_.begin();
      i != socket_map_.end(); ++i) {
    if (i->second.get() == socket.get())
      return i->first;
  }
  int return_id = next_socket_id_++;
  socket_map_[return_id] = socket;
  return return_id;
}

bool ExtensionBluetoothEventRouter::ReleaseSocket(int id) {
  SocketMap::iterator socket_entry = socket_map_.find(id);
  if (socket_entry == socket_map_.end())
    return false;
  socket_map_.erase(socket_entry);
  return true;
}

void ExtensionBluetoothEventRouter::AddProfile(
    const std::string& uuid,
    device::BluetoothProfile* bluetooth_profile) {
  DCHECK(!HasProfile(uuid));
  bluetooth_profile_map_[uuid] = bluetooth_profile;
}

void ExtensionBluetoothEventRouter::RemoveProfile(const std::string& uuid) {
  BluetoothProfileMap::iterator iter = bluetooth_profile_map_.find(uuid);
  if (iter != bluetooth_profile_map_.end()) {
    device::BluetoothProfile* bluetooth_profile = iter->second;
    bluetooth_profile_map_.erase(iter);
    bluetooth_profile->Unregister();
  }
}

bool ExtensionBluetoothEventRouter::HasProfile(const std::string& uuid) const {
  return bluetooth_profile_map_.find(uuid) != bluetooth_profile_map_.end();
}

device::BluetoothProfile* ExtensionBluetoothEventRouter::GetProfile(
    const std::string& uuid) const {
  BluetoothProfileMap::const_iterator iter = bluetooth_profile_map_.find(uuid);
  if (iter != bluetooth_profile_map_.end())
    return iter->second;

  return NULL;
}

scoped_refptr<device::BluetoothSocket>
ExtensionBluetoothEventRouter::GetSocket(int id) {
  SocketMap::iterator socket_entry = socket_map_.find(id);
  if (socket_entry == socket_map_.end())
    return NULL;
  return socket_entry->second;
}

void ExtensionBluetoothEventRouter::SetResponsibleForDiscovery(
    bool responsible) {
  responsible_for_discovery_ = responsible;
}

bool ExtensionBluetoothEventRouter::IsResponsibleForDiscovery() const {
  return responsible_for_discovery_;
}

void ExtensionBluetoothEventRouter::SetSendDiscoveryEvents(bool should_send) {
  // At the transition into sending devices, also send past devices that
  // were discovered as they will not be discovered again.
  if (should_send && !send_discovery_events_) {
    for (DeviceList::const_iterator i = discovered_devices_.begin();
        i != discovered_devices_.end(); ++i) {
      DispatchDeviceEvent(extensions::event_names::kBluetoothOnDeviceDiscovered,
                          **i);
    }
  }

  send_discovery_events_ = should_send;
}

void ExtensionBluetoothEventRouter::DispatchDeviceEvent(
    const char* event_name, const extensions::api::bluetooth::Device& device) {
  scoped_ptr<base::ListValue> args(new base::ListValue());
  args->Append(device.ToValue().release());
  scoped_ptr<Event> event(new Event(event_name, args.Pass()));
  ExtensionSystem::Get(profile_)->event_router()->BroadcastEvent(event.Pass());
}

void ExtensionBluetoothEventRouter::DispatchConnectionEvent(
    const std::string& extension_id,
    const std::string& uuid,
    const device::BluetoothDevice* device,
    scoped_refptr<device::BluetoothSocket> socket) {
  if (!HasProfile(uuid))
    return;

  int socket_id = RegisterSocket(socket);
  api::bluetooth::Socket result_socket;
  api::bluetooth::BluetoothDeviceToApiDevice(*device, &result_socket.device);
  result_socket.profile.uuid = uuid;
  result_socket.id = socket_id;

  scoped_ptr<base::ListValue> args(new base::ListValue());
  args->Append(result_socket.ToValue().release());
  scoped_ptr<Event> event(new Event(
      extensions::event_names::kBluetoothOnConnection, args.Pass()));
  ExtensionSystem::Get(profile_)->event_router()->DispatchEventToExtension(
      extension_id, event.Pass());
}

void ExtensionBluetoothEventRouter::AdapterPresentChanged(
    device::BluetoothAdapter* adapter, bool present) {
  if (adapter != adapter_.get()) {
    DVLOG(1) << "Ignoring event for adapter " << adapter->GetAddress();
    return;
  }
  DispatchAdapterStateEvent();
}

void ExtensionBluetoothEventRouter::AdapterPoweredChanged(
    device::BluetoothAdapter* adapter, bool has_power) {
  if (adapter != adapter_.get()) {
    DVLOG(1) << "Ignoring event for adapter " << adapter->GetAddress();
    return;
  }
  DispatchAdapterStateEvent();
}

void ExtensionBluetoothEventRouter::AdapterDiscoveringChanged(
    device::BluetoothAdapter* adapter, bool discovering) {
  if (adapter != adapter_.get()) {
    DVLOG(1) << "Ignoring event for adapter " << adapter->GetAddress();
    return;
  }

  if (!discovering) {
    send_discovery_events_ = false;
    responsible_for_discovery_ = false;
    discovered_devices_.clear();
  }

  DispatchAdapterStateEvent();
}

void ExtensionBluetoothEventRouter::DeviceAdded(
    device::BluetoothAdapter* adapter,
    device::BluetoothDevice* device) {
  if (adapter != adapter_.get()) {
    DVLOG(1) << "Ignoring event for adapter " << adapter->GetAddress();
    return;
  }

  extensions::api::bluetooth::Device* extension_device =
      new extensions::api::bluetooth::Device();
  extensions::api::bluetooth::BluetoothDeviceToApiDevice(
      *device, extension_device);
  discovered_devices_.push_back(extension_device);

  if (!send_discovery_events_)
    return;

  DispatchDeviceEvent(extensions::event_names::kBluetoothOnDeviceDiscovered,
                      *extension_device);
}

void ExtensionBluetoothEventRouter::InitializeAdapterIfNeeded() {
  if (!adapter_.get()) {
    GetAdapter(base::Bind(&ExtensionBluetoothEventRouter::InitializeAdapter,
                          weak_ptr_factory_.GetWeakPtr()));
  }
}

void ExtensionBluetoothEventRouter::InitializeAdapter(
    scoped_refptr<device::BluetoothAdapter> adapter) {
  if (!adapter_.get()) {
    adapter_ = adapter;
    adapter_->AddObserver(this);
  }
}

void ExtensionBluetoothEventRouter::MaybeReleaseAdapter() {
  if (adapter_.get() && num_event_listeners_ == 0) {
    adapter_->RemoveObserver(this);
    adapter_ = NULL;
  }
}

void ExtensionBluetoothEventRouter::DispatchAdapterStateEvent() {
  api::bluetooth::AdapterState state;
  PopulateAdapterState(*adapter_.get(), &state);

  scoped_ptr<base::ListValue> args(new base::ListValue());
  args->Append(state.ToValue().release());
  scoped_ptr<Event> event(new Event(
      extensions::event_names::kBluetoothOnAdapterStateChanged,
      args.Pass()));
  ExtensionSystem::Get(profile_)->event_router()->BroadcastEvent(event.Pass());
}

}  // namespace extensions
