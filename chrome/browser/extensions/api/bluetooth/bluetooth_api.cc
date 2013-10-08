// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/bluetooth/bluetooth_api.h"

#include <string>

#include "base/memory/ref_counted.h"
#include "chrome/browser/extensions/api/bluetooth/bluetooth_api_factory.h"
#include "chrome/browser/extensions/api/bluetooth/bluetooth_api_utils.h"
#include "chrome/browser/extensions/api/bluetooth/bluetooth_event_router.h"
#include "chrome/browser/extensions/event_names.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/api/bluetooth.h"
#include "chrome/common/extensions/permissions/bluetooth_permission.h"
#include "chrome/common/extensions/permissions/permissions_data.h"
#include "content/public/browser/browser_thread.h"
#include "device/bluetooth/bluetooth_adapter.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_out_of_band_pairing_data.h"
#include "device/bluetooth/bluetooth_profile.h"
#include "device/bluetooth/bluetooth_service_record.h"
#include "device/bluetooth/bluetooth_socket.h"
#include "device/bluetooth/bluetooth_utils.h"
#include "net/base/io_buffer.h"

using device::BluetoothAdapter;
using device::BluetoothDevice;
using device::BluetoothProfile;
using device::BluetoothServiceRecord;
using device::BluetoothSocket;

namespace {

extensions::ExtensionBluetoothEventRouter* GetEventRouter(Profile* profile) {
  return extensions::BluetoothAPI::Get(profile)->bluetooth_event_router();
}

}  // namespace

namespace {

const char kCouldNotGetLocalOutOfBandPairingData[] =
    "Could not get local Out Of Band Pairing Data";
const char kCouldNotSetOutOfBandPairingData[] =
    "Could not set Out Of Band Pairing Data";
const char kFailedToConnect[] = "Connection failed";
const char kInvalidDevice[] = "Invalid device";
const char kInvalidUuid[] = "Invalid UUID";
const char kPermissionDenied[] = "Permission to add profile denied.";
const char kPlatformNotSupported[] =
    "This operation is not supported on your platform";
const char kProfileAlreadyRegistered[] =
    "This profile has already been registered";
const char kProfileNotFound[] = "Profile not found: invalid uuid";
const char kProfileRegistrationFailed[] = "Profile registration failed";
const char kServiceDiscoveryFailed[] = "Service discovery failed";
const char kSocketNotFoundError[] = "Socket not found: invalid socket id";
const char kStartDiscoveryFailed[] = "Starting discovery failed";
const char kStopDiscoveryFailed[] = "Failed to stop discovery";

}  // namespace

namespace AddProfile = extensions::api::bluetooth::AddProfile;
namespace Connect = extensions::api::bluetooth::Connect;
namespace Disconnect = extensions::api::bluetooth::Disconnect;
namespace GetDevices = extensions::api::bluetooth::GetDevices;
namespace GetProfiles = extensions::api::bluetooth::GetProfiles;
namespace GetServices = extensions::api::bluetooth::GetServices;
namespace Read = extensions::api::bluetooth::Read;
namespace RemoveProfile = extensions::api::bluetooth::RemoveProfile;
namespace SetOutOfBandPairingData =
    extensions::api::bluetooth::SetOutOfBandPairingData;
namespace Write = extensions::api::bluetooth::Write;

namespace extensions {

// static
BluetoothAPI* BluetoothAPI::Get(Profile* profile) {
  return BluetoothAPIFactory::GetForProfile(profile);
}

BluetoothAPI::BluetoothAPI(Profile* profile) : profile_(profile) {
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, extensions::event_names::kBluetoothOnAdapterStateChanged);
}

BluetoothAPI::~BluetoothAPI() {
}

ExtensionBluetoothEventRouter* BluetoothAPI::bluetooth_event_router() {
  if (!bluetooth_event_router_)
    bluetooth_event_router_.reset(new ExtensionBluetoothEventRouter(profile_));

  return bluetooth_event_router_.get();
}

void BluetoothAPI::Shutdown() {
  ExtensionSystem::Get(profile_)->event_router()->UnregisterObserver(this);
}

void BluetoothAPI::OnListenerAdded(const EventListenerInfo& details) {
  if (bluetooth_event_router()->IsBluetoothSupported())
    bluetooth_event_router()->OnListenerAdded();
}

void BluetoothAPI::OnListenerRemoved(const EventListenerInfo& details) {
  if (bluetooth_event_router()->IsBluetoothSupported())
    bluetooth_event_router()->OnListenerRemoved();
}

namespace api {

BluetoothAddProfileFunction::BluetoothAddProfileFunction() {
}

bool BluetoothAddProfileFunction::RunImpl() {
  scoped_ptr<AddProfile::Params> params(AddProfile::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get() != NULL);

  if (!BluetoothDevice::IsUUIDValid(params->profile.uuid)) {
    SetError(kInvalidUuid);
    return false;
  }

  BluetoothPermission::CheckParam param(params->profile.uuid);
  if (!PermissionsData::CheckAPIPermissionWithParam(
          GetExtension(), APIPermission::kBluetooth, &param)) {
    SetError(kPermissionDenied);
    return false;
  }

  uuid_ = device::bluetooth_utils::CanonicalUuid(params->profile.uuid);

  if (GetEventRouter(profile())->HasProfile(uuid_)) {
    SetError(kProfileAlreadyRegistered);
    return false;
  }

  BluetoothProfile::Options options;
  if (params->profile.name.get())
    options.name = *params->profile.name.get();
  if (params->profile.channel.get())
    options.channel = *params->profile.channel.get();
  if (params->profile.psm.get())
    options.psm = *params->profile.psm.get();
  if (params->profile.require_authentication.get()) {
    options.require_authentication =
        *params->profile.require_authentication.get();
  }
  if (params->profile.require_authorization.get()) {
    options.require_authorization =
        *params->profile.require_authorization.get();
  }
  if (params->profile.auto_connect.get())
    options.auto_connect = *params->profile.auto_connect.get();
  if (params->profile.version.get())
    options.version = *params->profile.version.get();
  if (params->profile.features.get())
    options.features = *params->profile.features.get();

  RegisterProfile(
      options,
      base::Bind(&BluetoothAddProfileFunction::OnProfileRegistered, this));

  return true;
}

void BluetoothAddProfileFunction::RegisterProfile(
    const BluetoothProfile::Options& options,
    const BluetoothProfile::ProfileCallback& callback) {
  BluetoothProfile::Register(uuid_, options, callback);
}

void BluetoothAddProfileFunction::OnProfileRegistered(
    BluetoothProfile* bluetooth_profile) {
  if (!bluetooth_profile) {
    SetError(kProfileRegistrationFailed);
    SendResponse(false);
    return;
  }

  if (GetEventRouter(profile())->HasProfile(uuid_)) {
    bluetooth_profile->Unregister();
    SetError(kProfileAlreadyRegistered);
    SendResponse(false);
    return;
  }

  bluetooth_profile->SetConnectionCallback(
      base::Bind(&ExtensionBluetoothEventRouter::DispatchConnectionEvent,
                 base::Unretained(GetEventRouter(profile())),
                 extension_id(),
                 uuid_));
  GetEventRouter(profile())->AddProfile(uuid_, bluetooth_profile);
  SendResponse(true);
}

bool BluetoothRemoveProfileFunction::RunImpl() {
  scoped_ptr<RemoveProfile::Params> params(
      RemoveProfile::Params::Create(*args_));

  if (!BluetoothDevice::IsUUIDValid(params->profile.uuid)) {
    SetError(kInvalidUuid);
    return false;
  }

  std::string uuid =
      device::bluetooth_utils::CanonicalUuid(params->profile.uuid);

  if (!GetEventRouter(profile())->HasProfile(uuid)) {
    SetError(kProfileNotFound);
    return false;
  }

  GetEventRouter(profile())->RemoveProfile(uuid);
  return true;
}

// TODO(youngki): Implement.
bool BluetoothGetProfilesFunction::DoWork(
    scoped_refptr<device::BluetoothAdapter> adapter) {
  scoped_ptr<GetProfiles::Params> params(GetProfiles::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get() != NULL);
  const bluetooth::GetProfilesOptions& options = params->options;

  BluetoothDevice* device = adapter->GetDevice(options.device.address);
  if (!device) {
    SetError(kInvalidDevice);
    SendResponse(false);
    return false;
  }

  BluetoothDevice::ServiceList service_list = device->GetServices();

  base::ListValue* profiles = new base::ListValue;
  for (BluetoothDevice::ServiceList::const_iterator iter = service_list.begin();
       iter != service_list.end();
       ++iter) {
    bluetooth::Profile api_profile;
    api_profile.uuid = *iter;
    profiles->Append(api_profile.ToValue().release());
  }

  SetResult(profiles);
  SendResponse(true);

  return true;
}

bool BluetoothGetAdapterStateFunction::DoWork(
    scoped_refptr<BluetoothAdapter> adapter) {
  bluetooth::AdapterState state;
  PopulateAdapterState(*adapter.get(), &state);
  SetResult(state.ToValue().release());
  SendResponse(true);
  return true;
}

BluetoothGetDevicesFunction::BluetoothGetDevicesFunction()
    : device_events_sent_(0) {}

void BluetoothGetDevicesFunction::DispatchDeviceSearchResult(
    const BluetoothDevice& device) {
  bluetooth::Device extension_device;
  bluetooth::BluetoothDeviceToApiDevice(device, &extension_device);
  GetEventRouter(profile())->DispatchDeviceEvent(
      extensions::event_names::kBluetoothOnDeviceSearchResult,
      extension_device);

  device_events_sent_++;
}

void BluetoothGetDevicesFunction::FinishDeviceSearch() {
  scoped_ptr<base::ListValue> args(new base::ListValue());
  scoped_ptr<base::DictionaryValue> info(new base::DictionaryValue());
  info->SetInteger("expectedEventCount", device_events_sent_);
  args->Append(info.release());

  scoped_ptr<extensions::Event> event(new extensions::Event(
      extensions::event_names::kBluetoothOnDeviceSearchFinished, args.Pass()));
  extensions::ExtensionSystem::Get(profile())->event_router()->
      BroadcastEvent(event.Pass());

  SendResponse(true);
}

bool BluetoothGetDevicesFunction::DoWork(
    scoped_refptr<BluetoothAdapter> adapter) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  scoped_ptr<GetDevices::Params> params(GetDevices::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get() != NULL);
  const bluetooth::GetDevicesOptions& options = params->options;

  std::string uuid;
  if (options.profile.get() != NULL) {
    uuid = options.profile->uuid;
    if (!BluetoothDevice::IsUUIDValid(uuid)) {
      SetError(kInvalidUuid);
      SendResponse(false);
      return false;
    }
  }

  BluetoothAdapter::DeviceList devices = adapter->GetDevices();
  for (BluetoothAdapter::DeviceList::const_iterator iter = devices.begin();
       iter != devices.end();
       ++iter) {
    const BluetoothDevice* device = *iter;
    DCHECK(device);
    if (uuid.empty() || device->ProvidesServiceWithUUID(uuid))
      DispatchDeviceSearchResult(*device);
  }

  FinishDeviceSearch();

  return true;
}

void BluetoothGetServicesFunction::GetServiceRecordsCallback(
    base::ListValue* services,
    const BluetoothDevice::ServiceRecordList& records) {
  for (BluetoothDevice::ServiceRecordList::const_iterator i = records.begin();
      i != records.end(); ++i) {
    const BluetoothServiceRecord& record = **i;
    bluetooth::ServiceRecord api_record;
    api_record.name = record.name();
    if (!record.uuid().empty())
      api_record.uuid.reset(new std::string(record.uuid()));
    services->Append(api_record.ToValue().release());
  }

  SendResponse(true);
}

void BluetoothGetServicesFunction::OnErrorCallback() {
  SetError(kServiceDiscoveryFailed);
  SendResponse(false);
}

bool BluetoothGetServicesFunction::DoWork(
    scoped_refptr<BluetoothAdapter> adapter) {
  scoped_ptr<GetServices::Params> params(GetServices::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get() != NULL);
  const bluetooth::GetServicesOptions& options = params->options;

  BluetoothDevice* device = adapter->GetDevice(options.device_address);
  if (!device) {
    SetError(kInvalidDevice);
    SendResponse(false);
    return false;
  }

  base::ListValue* services = new base::ListValue;
  SetResult(services);

  device->GetServiceRecords(
      base::Bind(&BluetoothGetServicesFunction::GetServiceRecordsCallback,
                 this,
                 services),
      base::Bind(&BluetoothGetServicesFunction::OnErrorCallback,
                 this));

  return true;
}

void BluetoothConnectFunction::OnSuccessCallback() {
  SendResponse(true);
}

void BluetoothConnectFunction::OnErrorCallback() {
  SetError(kFailedToConnect);
  SendResponse(false);
}

bool BluetoothConnectFunction::DoWork(scoped_refptr<BluetoothAdapter> adapter) {
  scoped_ptr<Connect::Params> params(Connect::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get() != NULL);
  const bluetooth::ConnectOptions& options = params->options;

  if (!BluetoothDevice::IsUUIDValid(options.profile.uuid)) {
    SetError(kInvalidUuid);
    SendResponse(false);
    return false;
  }

  BluetoothDevice* device = adapter->GetDevice(options.device.address);
  if (!device) {
    SetError(kInvalidDevice);
    SendResponse(false);
    return false;
  }

  std::string uuid = device::bluetooth_utils::CanonicalUuid(
      options.profile.uuid);

  BluetoothProfile* bluetooth_profile =
      GetEventRouter(profile())->GetProfile(uuid);
  if (!bluetooth_profile) {
    SetError(kProfileNotFound);
    SendResponse(false);
    return false;
  }

  device->ConnectToProfile(
      bluetooth_profile,
      base::Bind(&BluetoothConnectFunction::OnSuccessCallback, this),
      base::Bind(&BluetoothConnectFunction::OnErrorCallback, this));

  return true;
}

bool BluetoothDisconnectFunction::RunImpl() {
  scoped_ptr<Disconnect::Params> params(Disconnect::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get() != NULL);
  const bluetooth::DisconnectOptions& options = params->options;
  return GetEventRouter(profile())->ReleaseSocket(options.socket.id);
}

BluetoothReadFunction::BluetoothReadFunction() : success_(false) {}
BluetoothReadFunction::~BluetoothReadFunction() {}

bool BluetoothReadFunction::Prepare() {
  scoped_ptr<Read::Params> params(Read::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get() != NULL);
  const bluetooth::ReadOptions& options = params->options;

  socket_ = GetEventRouter(profile())->GetSocket(options.socket.id);
  if (socket_.get() == NULL) {
    SetError(kSocketNotFoundError);
    return false;
  }

  success_ = false;
  return true;
}

void BluetoothReadFunction::Work() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));

  if (!socket_.get())
    return;

  scoped_refptr<net::GrowableIOBuffer> buffer(new net::GrowableIOBuffer);
  success_ = socket_->Receive(buffer.get());
  if (success_)
    SetResult(base::BinaryValue::CreateWithCopiedBuffer(buffer->StartOfBuffer(),
                                                        buffer->offset()));
  else
    SetError(socket_->GetLastErrorMessage());
}

bool BluetoothReadFunction::Respond() {
  return success_;
}

BluetoothWriteFunction::BluetoothWriteFunction()
    : success_(false),
      data_to_write_(NULL) {
}

BluetoothWriteFunction::~BluetoothWriteFunction() {}

bool BluetoothWriteFunction::Prepare() {
  // TODO(bryeung): update to new-style parameter passing when ArrayBuffer
  // support is added
  base::DictionaryValue* options;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(0, &options));

  base::DictionaryValue* socket;
  EXTENSION_FUNCTION_VALIDATE(options->GetDictionary("socket", &socket));

  int socket_id;
  EXTENSION_FUNCTION_VALIDATE(socket->GetInteger("id", &socket_id));

  socket_ = GetEventRouter(profile())->GetSocket(socket_id);
  if (socket_.get() == NULL) {
    SetError(kSocketNotFoundError);
    return false;
  }

  base::BinaryValue* tmp_data;
  EXTENSION_FUNCTION_VALIDATE(options->GetBinary("data", &tmp_data));
  data_to_write_ = tmp_data;

  success_ = false;
  return socket_.get() != NULL;
}

void BluetoothWriteFunction::Work() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));

  if (socket_.get() == NULL)
    return;

  scoped_refptr<net::WrappedIOBuffer> wrapped_io_buffer(
      new net::WrappedIOBuffer(data_to_write_->GetBuffer()));
  scoped_refptr<net::DrainableIOBuffer> drainable_io_buffer(
      new net::DrainableIOBuffer(wrapped_io_buffer.get(),
                                 data_to_write_->GetSize()));
  success_ = socket_->Send(drainable_io_buffer.get());
  if (success_) {
    if (drainable_io_buffer->BytesConsumed() > 0)
      SetResult(base::Value::CreateIntegerValue(
          drainable_io_buffer->BytesConsumed()));
    else
      results_.reset();
  } else {
    SetError(socket_->GetLastErrorMessage());
  }
}

bool BluetoothWriteFunction::Respond() {
  return success_;
}

void BluetoothSetOutOfBandPairingDataFunction::OnSuccessCallback() {
  SendResponse(true);
}

void BluetoothSetOutOfBandPairingDataFunction::OnErrorCallback() {
  SetError(kCouldNotSetOutOfBandPairingData);
  SendResponse(false);
}

bool BluetoothSetOutOfBandPairingDataFunction::DoWork(
    scoped_refptr<BluetoothAdapter> adapter) {
  // TODO(bryeung): update to new-style parameter passing when ArrayBuffer
  // support is added
  base::DictionaryValue* options;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(0, &options));
  std::string address;
  EXTENSION_FUNCTION_VALIDATE(options->GetString("deviceAddress", &address));

  BluetoothDevice* device = adapter->GetDevice(address);
  if (!device) {
    SetError(kInvalidDevice);
    SendResponse(false);
    return false;
  }

  if (options->HasKey("data")) {
    base::DictionaryValue* data_in;
    EXTENSION_FUNCTION_VALIDATE(options->GetDictionary("data", &data_in));

    device::BluetoothOutOfBandPairingData data_out;

    base::BinaryValue* tmp_data;
    EXTENSION_FUNCTION_VALIDATE(data_in->GetBinary("hash", &tmp_data));
    EXTENSION_FUNCTION_VALIDATE(
        tmp_data->GetSize() == device::kBluetoothOutOfBandPairingDataSize);
    memcpy(data_out.hash,
        reinterpret_cast<uint8_t*>(tmp_data->GetBuffer()),
        device::kBluetoothOutOfBandPairingDataSize);

    EXTENSION_FUNCTION_VALIDATE(data_in->GetBinary("randomizer", &tmp_data));
    EXTENSION_FUNCTION_VALIDATE(
        tmp_data->GetSize() == device::kBluetoothOutOfBandPairingDataSize);
    memcpy(data_out.randomizer,
        reinterpret_cast<uint8_t*>(tmp_data->GetBuffer()),
        device::kBluetoothOutOfBandPairingDataSize);

    device->SetOutOfBandPairingData(
        data_out,
        base::Bind(&BluetoothSetOutOfBandPairingDataFunction::OnSuccessCallback,
            this),
        base::Bind(&BluetoothSetOutOfBandPairingDataFunction::OnErrorCallback,
            this));
  } else {
    device->ClearOutOfBandPairingData(
        base::Bind(&BluetoothSetOutOfBandPairingDataFunction::OnSuccessCallback,
            this),
        base::Bind(&BluetoothSetOutOfBandPairingDataFunction::OnErrorCallback,
            this));
  }

  return true;
}

void BluetoothGetLocalOutOfBandPairingDataFunction::ReadCallback(
    const device::BluetoothOutOfBandPairingData& data) {
  base::BinaryValue* hash = base::BinaryValue::CreateWithCopiedBuffer(
      reinterpret_cast<const char*>(data.hash),
      device::kBluetoothOutOfBandPairingDataSize);
  base::BinaryValue* randomizer = base::BinaryValue::CreateWithCopiedBuffer(
      reinterpret_cast<const char*>(data.randomizer),
      device::kBluetoothOutOfBandPairingDataSize);

  // TODO(bryeung): convert to bluetooth::OutOfBandPairingData
  // when ArrayBuffer support within objects is completed.
  base::DictionaryValue* result = new base::DictionaryValue();
  result->Set("hash", hash);
  result->Set("randomizer", randomizer);

  SetResult(result);

  SendResponse(true);
}

void BluetoothGetLocalOutOfBandPairingDataFunction::ErrorCallback() {
  SetError(kCouldNotGetLocalOutOfBandPairingData);
  SendResponse(false);
}

bool BluetoothGetLocalOutOfBandPairingDataFunction::DoWork(
    scoped_refptr<BluetoothAdapter> adapter) {
  adapter->ReadLocalOutOfBandPairingData(
      base::Bind(&BluetoothGetLocalOutOfBandPairingDataFunction::ReadCallback,
          this),
      base::Bind(&BluetoothGetLocalOutOfBandPairingDataFunction::ErrorCallback,
          this));

  return true;
}

void BluetoothStartDiscoveryFunction::OnSuccessCallback() {
  SendResponse(true);
}

void BluetoothStartDiscoveryFunction::OnErrorCallback() {
  SetError(kStartDiscoveryFailed);
  GetEventRouter(profile())->SetResponsibleForDiscovery(false);
  SendResponse(false);
  GetEventRouter(profile())->OnListenerRemoved();
}

bool BluetoothStartDiscoveryFunction::DoWork(
    scoped_refptr<BluetoothAdapter> adapter) {
  GetEventRouter(profile())->SetSendDiscoveryEvents(true);

  // If this profile is already discovering devices, there should be nothing
  // else to do.
  if (!GetEventRouter(profile())->IsResponsibleForDiscovery()) {
    GetEventRouter(profile())->SetResponsibleForDiscovery(true);
    GetEventRouter(profile())->OnListenerAdded();
    adapter->StartDiscovering(
        base::Bind(&BluetoothStartDiscoveryFunction::OnSuccessCallback, this),
        base::Bind(&BluetoothStartDiscoveryFunction::OnErrorCallback, this));
  }

  return true;
}

void BluetoothStopDiscoveryFunction::OnSuccessCallback() {
  SendResponse(true);
  GetEventRouter(profile())->OnListenerRemoved();
}

void BluetoothStopDiscoveryFunction::OnErrorCallback() {
  SetError(kStopDiscoveryFailed);
  GetEventRouter(profile())->SetResponsibleForDiscovery(true);
  SendResponse(false);
  GetEventRouter(profile())->OnListenerRemoved();
}

bool BluetoothStopDiscoveryFunction::DoWork(
    scoped_refptr<BluetoothAdapter> adapter) {
  GetEventRouter(profile())->SetSendDiscoveryEvents(false);
  if (GetEventRouter(profile())->IsResponsibleForDiscovery()) {
    adapter->StopDiscovering(
        base::Bind(&BluetoothStopDiscoveryFunction::OnSuccessCallback, this),
        base::Bind(&BluetoothStopDiscoveryFunction::OnErrorCallback, this));
  }

  return true;
}

}  // namespace api
}  // namespace extensions
