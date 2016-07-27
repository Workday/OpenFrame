// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ID Not In Map Note:
// A service, characteristic, or descriptor ID not in the corresponding
// BluetoothDispatcherHost map [service_to_device_, characteristic_to_service_,
// descriptor_to_characteristic_] implies a hostile renderer because a renderer
// obtains the corresponding ID from this class and it will be added to the map
// at that time.

#include "content/browser/bluetooth/bluetooth_dispatcher_host.h"

#include "base/bind.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "base/thread_task_runner_handle.h"
#include "content/browser/bad_message.h"
#include "content/browser/bluetooth/bluetooth_metrics.h"
#include "content/browser/bluetooth/first_device_bluetooth_chooser.h"
#include "content/browser/frame_host/render_frame_host_impl.h"
#include "content/common/bluetooth/bluetooth_messages.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "device/bluetooth/bluetooth_adapter.h"
#include "device/bluetooth/bluetooth_adapter_factory.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_discovery_session.h"
#include "device/bluetooth/bluetooth_gatt_characteristic.h"
#include "device/bluetooth/bluetooth_gatt_service.h"

using blink::WebBluetoothError;
using device::BluetoothAdapter;
using device::BluetoothAdapterFactory;
using device::BluetoothGattCharacteristic;
using device::BluetoothGattService;
using device::BluetoothUUID;

namespace content {

namespace {

// TODO(ortuno): Once we have a chooser for scanning, a way to control that
// chooser from tests, and the right callback for discovered services we should
// delete these constants.
// https://crbug.com/436280 and https://crbug.com/484504
const int kDelayTime = 5;         // 5 seconds for scanning and discovering
const int kTestingDelayTime = 0;  // No need to wait during tests
const size_t kMaxLengthForDeviceName =
    29;  // max length of device name in filter.

bool IsEmptyOrInvalidFilter(const content::BluetoothScanFilter& filter) {
  return filter.name.empty() && filter.namePrefix.empty() &&
         filter.services.empty() &&
         filter.name.length() > kMaxLengthForDeviceName &&
         filter.namePrefix.length() > kMaxLengthForDeviceName;
}

bool HasEmptyOrInvalidFilter(
    const std::vector<content::BluetoothScanFilter>& filters) {
  return filters.empty()
             ? true
             : filters.end() != std::find_if(filters.begin(), filters.end(),
                                             IsEmptyOrInvalidFilter);
}

// Defined at
// https://webbluetoothchrome.github.io/web-bluetooth/#dfn-matches-a-filter
bool MatchesFilter(const device::BluetoothDevice& device,
                   const content::BluetoothScanFilter& filter) {
  DCHECK(!IsEmptyOrInvalidFilter(filter));

  const std::string device_name = base::UTF16ToUTF8(device.GetName());

  if (!filter.name.empty() && (device_name != filter.name)) {
      return false;
  }

  if (!filter.namePrefix.empty() &&
      (!base::StartsWith(device_name, filter.namePrefix,
                         base::CompareCase::SENSITIVE))) {
    return false;
  }

  if (!filter.services.empty()) {
    const auto& device_uuid_list = device.GetUUIDs();
    const std::set<BluetoothUUID> device_uuids(device_uuid_list.begin(),
                                               device_uuid_list.end());
    for (const auto& service : filter.services) {
      if (!ContainsKey(device_uuids, service)) {
        return false;
      }
    }
  }

  return true;
}

bool MatchesFilters(const device::BluetoothDevice& device,
                    const std::vector<content::BluetoothScanFilter>& filters) {
  DCHECK(!HasEmptyOrInvalidFilter(filters));
  for (const content::BluetoothScanFilter& filter : filters) {
    if (MatchesFilter(device, filter)) {
      return true;
    }
  }
  return false;
}

WebBluetoothError TranslateConnectError(
    device::BluetoothDevice::ConnectErrorCode error_code) {
  switch (error_code) {
    case device::BluetoothDevice::ERROR_UNKNOWN:
      RecordConnectGATTOutcome(UMAConnectGATTOutcome::UNKNOWN);
      return WebBluetoothError::ConnectUnknownError;
    case device::BluetoothDevice::ERROR_INPROGRESS:
      RecordConnectGATTOutcome(UMAConnectGATTOutcome::IN_PROGRESS);
      return WebBluetoothError::ConnectAlreadyInProgress;
    case device::BluetoothDevice::ERROR_FAILED:
      RecordConnectGATTOutcome(UMAConnectGATTOutcome::FAILED);
      return WebBluetoothError::ConnectUnknownFailure;
    case device::BluetoothDevice::ERROR_AUTH_FAILED:
      RecordConnectGATTOutcome(UMAConnectGATTOutcome::AUTH_FAILED);
      return WebBluetoothError::ConnectAuthFailed;
    case device::BluetoothDevice::ERROR_AUTH_CANCELED:
      RecordConnectGATTOutcome(UMAConnectGATTOutcome::AUTH_CANCELED);
      return WebBluetoothError::ConnectAuthCanceled;
    case device::BluetoothDevice::ERROR_AUTH_REJECTED:
      RecordConnectGATTOutcome(UMAConnectGATTOutcome::AUTH_REJECTED);
      return WebBluetoothError::ConnectAuthRejected;
    case device::BluetoothDevice::ERROR_AUTH_TIMEOUT:
      RecordConnectGATTOutcome(UMAConnectGATTOutcome::AUTH_TIMEOUT);
      return WebBluetoothError::ConnectAuthTimeout;
    case device::BluetoothDevice::ERROR_UNSUPPORTED_DEVICE:
      RecordConnectGATTOutcome(UMAConnectGATTOutcome::UNSUPPORTED_DEVICE);
      return WebBluetoothError::ConnectUnsupportedDevice;
  }
  NOTREACHED();
  return WebBluetoothError::UntranslatedConnectErrorCode;
}

blink::WebBluetoothError TranslateGATTError(
    BluetoothGattService::GattErrorCode error_code,
    UMAGATTOperation operation) {
  switch (error_code) {
    case BluetoothGattService::GATT_ERROR_UNKNOWN:
      RecordGATTOperationOutcome(operation, UMAGATTOperationOutcome::UNKNOWN);
      return blink::WebBluetoothError::GATTUnknownError;
    case BluetoothGattService::GATT_ERROR_FAILED:
      RecordGATTOperationOutcome(operation, UMAGATTOperationOutcome::FAILED);
      return blink::WebBluetoothError::GATTUnknownFailure;
    case BluetoothGattService::GATT_ERROR_IN_PROGRESS:
      RecordGATTOperationOutcome(operation,
                                 UMAGATTOperationOutcome::IN_PROGRESS);
      return blink::WebBluetoothError::GATTOperationInProgress;
    case BluetoothGattService::GATT_ERROR_INVALID_LENGTH:
      RecordGATTOperationOutcome(operation,
                                 UMAGATTOperationOutcome::INVALID_LENGTH);
      return blink::WebBluetoothError::GATTInvalidAttributeLength;
    case BluetoothGattService::GATT_ERROR_NOT_PERMITTED:
      RecordGATTOperationOutcome(operation,
                                 UMAGATTOperationOutcome::NOT_PERMITTED);
      return blink::WebBluetoothError::GATTNotPermitted;
    case BluetoothGattService::GATT_ERROR_NOT_AUTHORIZED:
      RecordGATTOperationOutcome(operation,
                                 UMAGATTOperationOutcome::NOT_AUTHORIZED);
      return blink::WebBluetoothError::GATTNotAuthorized;
    case BluetoothGattService::GATT_ERROR_NOT_PAIRED:
      RecordGATTOperationOutcome(operation,
                                 UMAGATTOperationOutcome::NOT_PAIRED);
      return blink::WebBluetoothError::GATTNotPaired;
    case BluetoothGattService::GATT_ERROR_NOT_SUPPORTED:
      RecordGATTOperationOutcome(operation,
                                 UMAGATTOperationOutcome::NOT_SUPPORTED);
      return blink::WebBluetoothError::GATTNotSupported;
  }
  NOTREACHED();
  return blink::WebBluetoothError::GATTUntranslatedErrorCode;
}

void StopDiscoverySession(
    scoped_ptr<device::BluetoothDiscoverySession> discovery_session) {
  // Nothing goes wrong if the discovery session fails to stop, and we don't
  // need to wait for it before letting the user's script proceed, so we ignore
  // the results here.
  discovery_session->Stop(base::Bind(&base::DoNothing),
                          base::Bind(&base::DoNothing));
}

// TODO(ortuno): This should really be a BluetoothDevice method.
// Replace when implemented. http://crbug.com/552022
std::vector<BluetoothGattService*> GetPrimaryServicesByUUID(
    device::BluetoothDevice* device,
    const std::string& service_uuid) {
  std::vector<BluetoothGattService*> services;
  VLOG(1) << "Looking for service: " << service_uuid;
  for (BluetoothGattService* service : device->GetGattServices()) {
    VLOG(1) << "Service in cache: " << service->GetUUID().canonical_value();
    if (service->GetUUID().canonical_value() == service_uuid &&
        service->IsPrimary()) {
      services.push_back(service);
    }
  }
  return services;
}

}  //  namespace

BluetoothDispatcherHost::BluetoothDispatcherHost(int render_process_id)
    : BrowserMessageFilter(BluetoothMsgStart),
      render_process_id_(render_process_id),
      current_delay_time_(kDelayTime),
      discovery_session_timer_(
          FROM_HERE,
          // TODO(jyasskin): Add a way for tests to control the dialog
          // directly, and change this to a reasonable discovery timeout.
          base::TimeDelta::FromSecondsD(current_delay_time_),
          base::Bind(&BluetoothDispatcherHost::StopDeviceDiscovery,
                     // base::Timer guarantees it won't call back after its
                     // destructor starts.
                     base::Unretained(this)),
          /*is_repeating=*/false),
      weak_ptr_factory_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Bind all future weak pointers to the UI thread.
  weak_ptr_on_ui_thread_ = weak_ptr_factory_.GetWeakPtr();
  weak_ptr_on_ui_thread_.get();  // Associates with UI thread.

  if (BluetoothAdapterFactory::IsBluetoothAdapterAvailable())
    BluetoothAdapterFactory::GetAdapter(
        base::Bind(&BluetoothDispatcherHost::set_adapter,
                   weak_ptr_factory_.GetWeakPtr()));
}

void BluetoothDispatcherHost::OnDestruct() const {
  // See class comment: UI Thread Note.
  BrowserThread::DeleteOnUIThread::Destruct(this);
}

void BluetoothDispatcherHost::OverrideThreadForMessage(
    const IPC::Message& message,
    content::BrowserThread::ID* thread) {
  // See class comment: UI Thread Note.
  *thread = BrowserThread::UI;
}

bool BluetoothDispatcherHost::OnMessageReceived(const IPC::Message& message) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(BluetoothDispatcherHost, message)
  IPC_MESSAGE_HANDLER(BluetoothHostMsg_RequestDevice, OnRequestDevice)
  IPC_MESSAGE_HANDLER(BluetoothHostMsg_ConnectGATT, OnConnectGATT)
  IPC_MESSAGE_HANDLER(BluetoothHostMsg_GetPrimaryService, OnGetPrimaryService)
  IPC_MESSAGE_HANDLER(BluetoothHostMsg_GetCharacteristic, OnGetCharacteristic)
  IPC_MESSAGE_HANDLER(BluetoothHostMsg_ReadValue, OnReadValue)
  IPC_MESSAGE_HANDLER(BluetoothHostMsg_WriteValue, OnWriteValue)
  IPC_MESSAGE_HANDLER(BluetoothHostMsg_StartNotifications, OnStartNotifications)
  IPC_MESSAGE_HANDLER(BluetoothHostMsg_StopNotifications, OnStopNotifications)
  IPC_MESSAGE_HANDLER(BluetoothHostMsg_RegisterCharacteristic,
                      OnRegisterCharacteristicObject);
  IPC_MESSAGE_HANDLER(BluetoothHostMsg_UnregisterCharacteristic,
                      OnUnregisterCharacteristicObject);
  IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void BluetoothDispatcherHost::SetBluetoothAdapterForTesting(
    scoped_refptr<device::BluetoothAdapter> mock_adapter) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (mock_adapter.get()) {
    current_delay_time_ = kTestingDelayTime;
    // Reset the discovery session timer to use the new delay time.
    discovery_session_timer_.Start(
        FROM_HERE, base::TimeDelta::FromSecondsD(current_delay_time_),
        base::Bind(&BluetoothDispatcherHost::StopDeviceDiscovery,
                   // base::Timer guarantees it won't call back after its
                   // destructor starts.
                   base::Unretained(this)));
  } else {
    // The following data structures are used to store pending operations.
    // They should never contain elements at the end of a test.
    DCHECK(request_device_sessions_.IsEmpty());
    DCHECK(pending_primary_services_requests_.empty());

    // The following data structures are cleaned up when a
    // device/service/characteristic is removed.
    // Since this can happen after the test is done and the cleanup function is
    // called, we clean them here.
    service_to_device_.clear();
    characteristic_to_service_.clear();
    characteristic_id_to_notify_session_.clear();
    active_characteristic_threads_.clear();
    connections_.clear();
    devices_with_discovered_services_.clear();
  }

  set_adapter(mock_adapter.Pass());
}

BluetoothDispatcherHost::~BluetoothDispatcherHost() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Clear adapter, releasing observer references.
  set_adapter(scoped_refptr<device::BluetoothAdapter>());
}

// Stores information associated with an in-progress requestDevice call. This
// will include the state of the active chooser dialog in a future patch.
struct BluetoothDispatcherHost::RequestDeviceSession {
 public:
  RequestDeviceSession(int thread_id,
                       int request_id,
                       const std::vector<BluetoothScanFilter>& filters,
                       const std::vector<BluetoothUUID>& optional_services)
      : thread_id(thread_id),
        request_id(request_id),
        filters(filters),
        optional_services(optional_services) {}

  void AddFilteredDevice(const device::BluetoothDevice& device) {
    if (chooser && MatchesFilters(device, filters)) {
      chooser->AddDevice(device.GetAddress(), device.GetName());
    }
  }

  scoped_ptr<device::BluetoothDiscoveryFilter> ComputeScanFilter() const {
    std::set<BluetoothUUID> services;
    for (const BluetoothScanFilter& filter : filters) {
      services.insert(filter.services.begin(), filter.services.end());
    }
    scoped_ptr<device::BluetoothDiscoveryFilter> discovery_filter(
        new device::BluetoothDiscoveryFilter(
            device::BluetoothDiscoveryFilter::TRANSPORT_DUAL));
    for (const BluetoothUUID& service : services) {
      discovery_filter->AddUUID(service);
    }
    return discovery_filter.Pass();
  }

  const int thread_id;
  const int request_id;
  const std::vector<BluetoothScanFilter> filters;
  const std::vector<BluetoothUUID> optional_services;
  scoped_ptr<BluetoothChooser> chooser;
  scoped_ptr<device::BluetoothDiscoverySession> discovery_session;
};

struct BluetoothDispatcherHost::CacheQueryResult {
  CacheQueryResult()
      : device(nullptr),
        service(nullptr),
        characteristic(nullptr),
        outcome(CacheQueryOutcome::SUCCESS) {}
  CacheQueryResult(CacheQueryOutcome outcome)
      : device(nullptr),
        service(nullptr),
        characteristic(nullptr),
        outcome(outcome) {}
  ~CacheQueryResult() {}
  WebBluetoothError GetWebError() const {
    switch (outcome) {
      case CacheQueryOutcome::SUCCESS:
      case CacheQueryOutcome::BAD_RENDERER:
        NOTREACHED();
        return WebBluetoothError::DeviceNoLongerInRange;
      case CacheQueryOutcome::NO_DEVICE:
        return WebBluetoothError::DeviceNoLongerInRange;
      case CacheQueryOutcome::NO_SERVICE:
        return WebBluetoothError::ServiceNoLongerExists;
      case CacheQueryOutcome::NO_CHARACTERISTIC:
        return WebBluetoothError::CharacteristicNoLongerExists;
    }
    NOTREACHED();
    return WebBluetoothError::DeviceNoLongerInRange;
  }

  device::BluetoothDevice* device;
  device::BluetoothGattService* service;
  device::BluetoothGattCharacteristic* characteristic;
  CacheQueryOutcome outcome;
};

struct BluetoothDispatcherHost::PrimaryServicesRequest {
  enum CallingFunction { GET_PRIMARY_SERVICE, GET_PRIMARY_SERVICES };

  PrimaryServicesRequest(int thread_id,
                         int request_id,
                         const std::string& service_uuid,
                         PrimaryServicesRequest::CallingFunction func)
      : thread_id(thread_id),
        request_id(request_id),
        service_uuid(service_uuid),
        func(func) {}
  ~PrimaryServicesRequest() {}

  int thread_id;
  int request_id;
  std::string service_uuid;
  CallingFunction func;
};

void BluetoothDispatcherHost::set_adapter(
    scoped_refptr<device::BluetoothAdapter> adapter) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  connections_.clear();
  if (adapter_.get())
    adapter_->RemoveObserver(this);
  adapter_ = adapter;
  if (adapter_.get())
    adapter_->AddObserver(this);
}

void BluetoothDispatcherHost::StartDeviceDiscovery(
    RequestDeviceSession* session,
    int chooser_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (session->discovery_session) {
    // Already running; just increase the timeout.
    discovery_session_timer_.Reset();
  } else {
    session->chooser->ShowDiscoveryState(
        BluetoothChooser::DiscoveryState::DISCOVERING);
    adapter_->StartDiscoverySessionWithFilter(
        session->ComputeScanFilter(),
        base::Bind(&BluetoothDispatcherHost::OnDiscoverySessionStarted,
                   weak_ptr_on_ui_thread_, chooser_id),
        base::Bind(&BluetoothDispatcherHost::OnDiscoverySessionStartedError,
                   weak_ptr_on_ui_thread_, chooser_id));
  }
}

void BluetoothDispatcherHost::StopDeviceDiscovery() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  for (IDMap<RequestDeviceSession, IDMapOwnPointer>::iterator iter(
           &request_device_sessions_);
       !iter.IsAtEnd(); iter.Advance()) {
    RequestDeviceSession* session = iter.GetCurrentValue();
    if (session->discovery_session) {
      StopDiscoverySession(session->discovery_session.Pass());
    }
    if (session->chooser) {
      session->chooser->ShowDiscoveryState(
          BluetoothChooser::DiscoveryState::IDLE);
    }
  }
}

void BluetoothDispatcherHost::AdapterPoweredChanged(
    device::BluetoothAdapter* adapter,
    bool powered) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  const BluetoothChooser::AdapterPresence presence =
      powered ? BluetoothChooser::AdapterPresence::POWERED_ON
              : BluetoothChooser::AdapterPresence::POWERED_OFF;
  for (IDMap<RequestDeviceSession, IDMapOwnPointer>::iterator iter(
           &request_device_sessions_);
       !iter.IsAtEnd(); iter.Advance()) {
    RequestDeviceSession* session = iter.GetCurrentValue();
    if (session->chooser)
      session->chooser->SetAdapterPresence(presence);
  }
}

void BluetoothDispatcherHost::DeviceAdded(device::BluetoothAdapter* adapter,
                                          device::BluetoothDevice* device) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  VLOG(1) << "Adding device to all choosers: " << device->GetAddress();
  for (IDMap<RequestDeviceSession, IDMapOwnPointer>::iterator iter(
           &request_device_sessions_);
       !iter.IsAtEnd(); iter.Advance()) {
    RequestDeviceSession* session = iter.GetCurrentValue();
    session->AddFilteredDevice(*device);
  }
}

void BluetoothDispatcherHost::DeviceRemoved(device::BluetoothAdapter* adapter,
                                            device::BluetoothDevice* device) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  VLOG(1) << "Marking device removed on all choosers: " << device->GetAddress();
  for (IDMap<RequestDeviceSession, IDMapOwnPointer>::iterator iter(
           &request_device_sessions_);
       !iter.IsAtEnd(); iter.Advance()) {
    RequestDeviceSession* session = iter.GetCurrentValue();
    if (session->chooser) {
      session->chooser->RemoveDevice(device->GetAddress());
    }
  }
}

void BluetoothDispatcherHost::GattServicesDiscovered(
    device::BluetoothAdapter* adapter,
    device::BluetoothDevice* device) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  const std::string& device_id = device->GetAddress();
  VLOG(1) << "Services discovered for device: " << device_id;

  devices_with_discovered_services_.insert(device_id);

  auto iter = pending_primary_services_requests_.find(device_id);
  if (iter == pending_primary_services_requests_.end()) {
    return;
  }
  std::vector<PrimaryServicesRequest> requests;
  requests.swap(iter->second);
  pending_primary_services_requests_.erase(iter);

  for (const PrimaryServicesRequest& request : requests) {
    std::vector<BluetoothGattService*> services =
        GetPrimaryServicesByUUID(device, request.service_uuid);
    switch (request.func) {
      case PrimaryServicesRequest::GET_PRIMARY_SERVICE:
        if (!services.empty()) {
          AddToServicesMapAndSendGetPrimaryServiceSuccess(
              *services[0], request.thread_id, request.request_id);
        } else {
          VLOG(1) << "No service found";
          RecordGetPrimaryServiceOutcome(
              UMAGetPrimaryServiceOutcome::NOT_FOUND);
          Send(new BluetoothMsg_GetPrimaryServiceError(
              request.thread_id, request.request_id,
              WebBluetoothError::ServiceNotFound));
        }
        break;
      case PrimaryServicesRequest::GET_PRIMARY_SERVICES:
        NOTIMPLEMENTED();
        break;
    }
  }
  DCHECK(!ContainsKey(pending_primary_services_requests_, device_id))
      << "Sending get-service responses unexpectedly queued another request.";
}

void BluetoothDispatcherHost::GattCharacteristicValueChanged(
    device::BluetoothAdapter* adapter,
    device::BluetoothGattCharacteristic* characteristic,
    const std::vector<uint8>& value) {
  VLOG(1) << "Characteristic updated: " << characteristic->GetIdentifier();
  auto iter =
      active_characteristic_threads_.find(characteristic->GetIdentifier());

  if (iter == active_characteristic_threads_.end()) {
    return;
  }

  for (int thread_id : iter->second) {
    // Yield to the event loop so that the event gets dispatched after the
    // readValue promise resolves.
    // TODO(ortuno): Make sure the order of fulfulling promises and triggering
    // events matches the spec and that events don't get lost.
    // https://crbug.com/543882
    if (!base::ThreadTaskRunnerHandle::Get()->PostTask(
            FROM_HERE,
            base::Bind(&BluetoothDispatcherHost::NotifyActiveCharacteristic,
                       weak_ptr_on_ui_thread_, thread_id,
                       characteristic->GetIdentifier(), value))) {
      LOG(WARNING) << "No TaskRunner.";
    }
  }
}

void BluetoothDispatcherHost::NotifyActiveCharacteristic(
    int thread_id,
    const std::string& characteristic_instance_id,
    const std::vector<uint8>& value) {
  Send(new BluetoothMsg_CharacteristicValueChanged(
      thread_id, characteristic_instance_id, value));
}

void BluetoothDispatcherHost::OnRequestDevice(
    int thread_id,
    int request_id,
    int frame_routing_id,
    const std::vector<BluetoothScanFilter>& filters,
    const std::vector<BluetoothUUID>& optional_services) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  RecordWebBluetoothFunctionCall(UMAWebBluetoothFunction::REQUEST_DEVICE);
  RecordRequestDeviceArguments(filters, optional_services);

  VLOG(1) << "requestDevice called with the following filters: ";
  for (const BluetoothScanFilter& filter : filters) {
    VLOG(1) << "Name: " << filter.name;
    VLOG(1) << "Name Prefix: " << filter.namePrefix;
    VLOG(1) << "Services:";
    VLOG(1) << "\t[";
    for (const BluetoothUUID& service : filter.services)
      VLOG(1) << "\t\t" << service.value();
    VLOG(1) << "\t]";
  }

  VLOG(1) << "requestDevice called with the following optional services: ";
  for (const BluetoothUUID& service : optional_services)
    VLOG(1) << "\t" << service.value();

  RenderFrameHostImpl* render_frame_host =
      RenderFrameHostImpl::FromID(render_process_id_, frame_routing_id);

  if (!render_frame_host) {
    DLOG(WARNING)
        << "Got a requestDevice IPC without a matching RenderFrameHost: "
        << render_process_id_ << ", " << frame_routing_id;
    RecordRequestDeviceOutcome(UMARequestDeviceOutcome::NO_RENDER_FRAME);
    Send(new BluetoothMsg_RequestDeviceError(
        thread_id, request_id, WebBluetoothError::RequestDeviceWithoutFrame));
    return;
  }

  if (!adapter_) {
    VLOG(1) << "No BluetoothAdapter. Can't serve requestDevice.";
    RecordRequestDeviceOutcome(UMARequestDeviceOutcome::NO_BLUETOOTH_ADAPTER);
    Send(new BluetoothMsg_RequestDeviceError(
        thread_id, request_id, WebBluetoothError::NoBluetoothAdapter));
    return;
  }

  if (!adapter_->IsPresent()) {
    VLOG(1) << "Bluetooth Adapter not present. Can't serve requestDevice.";
    RecordRequestDeviceOutcome(
        UMARequestDeviceOutcome::BLUETOOTH_ADAPTER_NOT_PRESENT);
    Send(new BluetoothMsg_RequestDeviceError(
        thread_id, request_id, WebBluetoothError::NoBluetoothAdapter));
    return;
  }

  // The renderer should never send empty filters.
  if (HasEmptyOrInvalidFilter(filters)) {
    bad_message::ReceivedBadMessage(this,
                                    bad_message::BDH_EMPTY_OR_INVALID_FILTERS);
    return;
  }

  // Create storage for the information that backs the chooser, and show the
  // chooser.
  RequestDeviceSession* const session = new RequestDeviceSession(
      thread_id, request_id, filters, optional_services);
  int chooser_id = request_device_sessions_.Add(session);

  BluetoothChooser::EventHandler chooser_event_handler =
      base::Bind(&BluetoothDispatcherHost::OnBluetoothChooserEvent,
                 weak_ptr_on_ui_thread_, chooser_id);
  if (WebContents* web_contents =
          WebContents::FromRenderFrameHost(render_frame_host)) {
    if (WebContentsDelegate* delegate = web_contents->GetDelegate()) {
      session->chooser = delegate->RunBluetoothChooser(
          web_contents, chooser_event_handler,
          render_frame_host->GetLastCommittedURL().GetOrigin());
    }
  }
  if (!session->chooser) {
    LOG(WARNING)
        << "No Bluetooth chooser implementation; falling back to first device.";
    session->chooser.reset(
        new FirstDeviceBluetoothChooser(chooser_event_handler));
  }

  if (!session->chooser->CanAskForScanningPermission()) {
    VLOG(1) << "Closing immediately because Chooser cannot obtain permission.";
    OnBluetoothChooserEvent(chooser_id,
                            BluetoothChooser::Event::DENIED_PERMISSION, "");
    return;
  }

  // Populate the initial list of devices.
  VLOG(1) << "Populating " << adapter_->GetDevices().size()
          << " devices in chooser " << chooser_id;
  for (const device::BluetoothDevice* device : adapter_->GetDevices()) {
    VLOG(1) << "\t" << device->GetAddress();
    session->AddFilteredDevice(*device);
  }

  if (!session->chooser) {
    // If the dialog's closing, no need to do any of the rest of this.
    return;
  }

  if (!adapter_->IsPowered()) {
    session->chooser->SetAdapterPresence(
        BluetoothChooser::AdapterPresence::POWERED_OFF);
    return;
  }

  StartDeviceDiscovery(session, chooser_id);
}

void BluetoothDispatcherHost::OnConnectGATT(int thread_id,
                                            int request_id,
                                            const std::string& device_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  RecordWebBluetoothFunctionCall(UMAWebBluetoothFunction::CONNECT_GATT);
  const base::TimeTicks start_time = base::TimeTicks::Now();

  // TODO(ortuno): Right now it's pointless to check if the domain has access to
  // the device, because any domain can connect to any device. But once
  // permissions are implemented we should check that the domain has access to
  // the device. https://crbug.com/484745

  const CacheQueryResult query_result = QueryCacheForDevice(device_id);

  if (query_result.outcome != CacheQueryOutcome::SUCCESS) {
    RecordConnectGATTOutcome(query_result.outcome);
    Send(new BluetoothMsg_ConnectGATTError(thread_id, request_id,
                                           query_result.GetWebError()));
    return;
  }

  query_result.device->CreateGattConnection(
      base::Bind(&BluetoothDispatcherHost::OnGATTConnectionCreated,
                 weak_ptr_on_ui_thread_, thread_id, request_id, device_id,
                 start_time),
      base::Bind(&BluetoothDispatcherHost::OnCreateGATTConnectionError,
                 weak_ptr_on_ui_thread_, thread_id, request_id, device_id,
                 start_time));
}

void BluetoothDispatcherHost::OnGetPrimaryService(
    int thread_id,
    int request_id,
    const std::string& device_id,
    const std::string& service_uuid) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  RecordWebBluetoothFunctionCall(UMAWebBluetoothFunction::GET_PRIMARY_SERVICE);
  RecordGetPrimaryServiceService(BluetoothUUID(service_uuid));

  // TODO(ortuno): Check if device_id is in "allowed devices"
  // https://crbug.com/493459
  // TODO(ortuno): Check if service_uuid is in "allowed services"
  // https://crbug.com/493460

  const CacheQueryResult query_result = QueryCacheForDevice(device_id);

  if (query_result.outcome != CacheQueryOutcome::SUCCESS) {
    RecordGetPrimaryServiceOutcome(query_result.outcome);
    Send(new BluetoothMsg_GetPrimaryServiceError(thread_id, request_id,
                                                 query_result.GetWebError()));
    return;
  }

  // There are four possibilities here:
  // 1. Services not discovered and service present in |device|: Send back the
  //    service to the renderer.
  // 2. Services discovered and service present in |device|: Send back the
  //    service to the renderer.
  // 3. Services discovered and service not present in |device|: Send back not
  //    found error.
  // 4. Services not discovered and service not present in |device|: Add request
  //    to map of pending getPrimaryService requests.

  std::vector<BluetoothGattService*> services =
      GetPrimaryServicesByUUID(query_result.device, service_uuid);

  // 1. & 2.
  if (!services.empty()) {
    VLOG(1) << "Service found in device.";
    const BluetoothGattService& service = *services[0];
    DCHECK(service.IsPrimary());
    AddToServicesMapAndSendGetPrimaryServiceSuccess(service, thread_id,
                                                    request_id);
    return;
  }

  // 3.
  if (IsServicesDiscoveryCompleteForDevice(device_id)) {
    VLOG(1) << "Service not found in device.";
    RecordGetPrimaryServiceOutcome(UMAGetPrimaryServiceOutcome::NOT_FOUND);
    Send(new BluetoothMsg_GetPrimaryServiceError(
        thread_id, request_id, WebBluetoothError::ServiceNotFound));
    return;
  }

  VLOG(1) << "Adding service request to pending requests.";
  // 4.
  AddToPendingPrimaryServicesRequest(
      device_id,
      PrimaryServicesRequest(thread_id, request_id, service_uuid,
                             PrimaryServicesRequest::GET_PRIMARY_SERVICE));
}

void BluetoothDispatcherHost::OnGetCharacteristic(
    int thread_id,
    int request_id,
    const std::string& service_instance_id,
    const std::string& characteristic_uuid) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  RecordWebBluetoothFunctionCall(UMAWebBluetoothFunction::GET_CHARACTERISTIC);
  RecordGetCharacteristicCharacteristic(characteristic_uuid);

  const CacheQueryResult query_result =
      QueryCacheForService(service_instance_id);

  if (query_result.outcome == CacheQueryOutcome::BAD_RENDERER) {
    return;
  }

  if (query_result.outcome != CacheQueryOutcome::SUCCESS) {
    RecordGetCharacteristicOutcome(query_result.outcome);
    Send(new BluetoothMsg_GetCharacteristicError(thread_id, request_id,
                                                 query_result.GetWebError()));
    return;
  }

  for (BluetoothGattCharacteristic* characteristic :
       query_result.service->GetCharacteristics()) {
    if (characteristic->GetUUID().canonical_value() == characteristic_uuid) {
      const std::string& characteristic_instance_id =
          characteristic->GetIdentifier();

      auto insert_result = characteristic_to_service_.insert(
          make_pair(characteristic_instance_id, service_instance_id));

      // If  value is already in map, DCHECK it's valid.
      if (!insert_result.second)
        DCHECK(insert_result.first->second == service_instance_id);

      RecordGetCharacteristicOutcome(UMAGetCharacteristicOutcome::SUCCESS);
      // TODO(ortuno): Use generated instance ID instead.
      // https://crbug.com/495379
      Send(new BluetoothMsg_GetCharacteristicSuccess(
          thread_id, request_id, characteristic_instance_id,
          static_cast<uint32>(characteristic->GetProperties())));
      return;
    }
  }
  RecordGetCharacteristicOutcome(UMAGetCharacteristicOutcome::NOT_FOUND);
  Send(new BluetoothMsg_GetCharacteristicError(
      thread_id, request_id, WebBluetoothError::CharacteristicNotFound));
}

void BluetoothDispatcherHost::OnReadValue(
    int thread_id,
    int request_id,
    const std::string& characteristic_instance_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  RecordWebBluetoothFunctionCall(
      UMAWebBluetoothFunction::CHARACTERISTIC_READ_VALUE);

  const CacheQueryResult query_result =
      QueryCacheForCharacteristic(characteristic_instance_id);

  if (query_result.outcome == CacheQueryOutcome::BAD_RENDERER) {
    return;
  }

  if (query_result.outcome != CacheQueryOutcome::SUCCESS) {
    RecordCharacteristicReadValueOutcome(query_result.outcome);
    Send(new BluetoothMsg_ReadCharacteristicValueError(
        thread_id, request_id, query_result.GetWebError()));
    return;
  }

  query_result.characteristic->ReadRemoteCharacteristic(
      base::Bind(&BluetoothDispatcherHost::OnCharacteristicValueRead,
                 weak_ptr_on_ui_thread_, thread_id, request_id),
      base::Bind(&BluetoothDispatcherHost::OnCharacteristicReadValueError,
                 weak_ptr_on_ui_thread_, thread_id, request_id));
}

void BluetoothDispatcherHost::OnWriteValue(
    int thread_id,
    int request_id,
    const std::string& characteristic_instance_id,
    const std::vector<uint8_t>& value) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  RecordWebBluetoothFunctionCall(
      UMAWebBluetoothFunction::CHARACTERISTIC_WRITE_VALUE);

  // Length check per step 3 of writeValue algorithm:
  // https://webbluetoothchrome.github.io/web-bluetooth/#dom-bluetoothgattcharacteristic-writevalue
  // We perform the length check on the renderer side. So if we
  // get a value with length > 512, we can assume it's a hostile
  // renderer and kill it.
  if (value.size() > 512) {
    bad_message::ReceivedBadMessage(
        this, bad_message::BDH_INVALID_WRITE_VALUE_LENGTH);
    return;
  }

  const CacheQueryResult query_result =
      QueryCacheForCharacteristic(characteristic_instance_id);

  if (query_result.outcome == CacheQueryOutcome::BAD_RENDERER) {
    return;
  }

  if (query_result.outcome != CacheQueryOutcome::SUCCESS) {
    RecordCharacteristicWriteValueOutcome(query_result.outcome);
    Send(new BluetoothMsg_WriteCharacteristicValueError(
        thread_id, request_id, query_result.GetWebError()));
    return;
  }

  query_result.characteristic->WriteRemoteCharacteristic(
      value, base::Bind(&BluetoothDispatcherHost::OnWriteValueSuccess,
                        weak_ptr_on_ui_thread_, thread_id, request_id),
      base::Bind(&BluetoothDispatcherHost::OnWriteValueFailed,
                 weak_ptr_on_ui_thread_, thread_id, request_id));
}

void BluetoothDispatcherHost::OnStartNotifications(
    int thread_id,
    int request_id,
    const std::string& characteristic_instance_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  RecordWebBluetoothFunctionCall(
      UMAWebBluetoothFunction::CHARACTERISTIC_START_NOTIFICATIONS);

  // BluetoothDispatcher will never send a request for a characteristic
  // already subscribed to notifications.
  if (characteristic_id_to_notify_session_.find(characteristic_instance_id) !=
      characteristic_id_to_notify_session_.end()) {
    bad_message::ReceivedBadMessage(
        this, bad_message::BDH_CHARACTERISTIC_ALREADY_SUBSCRIBED);
    return;
  }

  // TODO(ortuno): Check if notify/indicate bit is set.
  // http://crbug.com/538869

  const CacheQueryResult query_result =
      QueryCacheForCharacteristic(characteristic_instance_id);

  if (query_result.outcome == CacheQueryOutcome::BAD_RENDERER) {
    return;
  }

  if (query_result.outcome != CacheQueryOutcome::SUCCESS) {
    RecordStartNotificationsOutcome(query_result.outcome);
    Send(new BluetoothMsg_StartNotificationsError(thread_id, request_id,
                                                  query_result.GetWebError()));
    return;
  }

  query_result.characteristic->StartNotifySession(
      base::Bind(&BluetoothDispatcherHost::OnStartNotifySessionSuccess,
                 weak_ptr_factory_.GetWeakPtr(), thread_id, request_id),
      base::Bind(&BluetoothDispatcherHost::OnStartNotifySessionFailed,
                 weak_ptr_factory_.GetWeakPtr(), thread_id, request_id));
}

void BluetoothDispatcherHost::OnStopNotifications(
    int thread_id,
    int request_id,
    const std::string& characteristic_instance_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  RecordWebBluetoothFunctionCall(
      UMAWebBluetoothFunction::CHARACTERISTIC_STOP_NOTIFICATIONS);

  auto notify_session_iter =
      characteristic_id_to_notify_session_.find(characteristic_instance_id);
  if (notify_session_iter == characteristic_id_to_notify_session_.end()) {
    Send(new BluetoothMsg_StopNotificationsSuccess(thread_id, request_id));
    return;
  }
  notify_session_iter->second->Stop(
      base::Bind(&BluetoothDispatcherHost::OnStopNotifySession,
                 weak_ptr_factory_.GetWeakPtr(), thread_id, request_id,
                 characteristic_instance_id));
}

void BluetoothDispatcherHost::OnRegisterCharacteristicObject(
    int thread_id,
    const std::string& characteristic_instance_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  active_characteristic_threads_[characteristic_instance_id].insert(thread_id);
}

void BluetoothDispatcherHost::OnUnregisterCharacteristicObject(
    int thread_id,
    const std::string& characteristic_instance_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  auto active_iter =
      active_characteristic_threads_.find(characteristic_instance_id);
  if (active_iter == active_characteristic_threads_.end()) {
    return;
  }
  std::set<int>& thread_ids_set = active_iter->second;
  thread_ids_set.erase(thread_id);
  if (thread_ids_set.empty()) {
    active_characteristic_threads_.erase(active_iter);
  }
}

void BluetoothDispatcherHost::OnDiscoverySessionStarted(
    int chooser_id,
    scoped_ptr<device::BluetoothDiscoverySession> discovery_session) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  VLOG(1) << "Started discovery session for " << chooser_id;
  if (RequestDeviceSession* session =
          request_device_sessions_.Lookup(chooser_id)) {
    session->discovery_session = discovery_session.Pass();

    // Arrange to stop discovery later.
    discovery_session_timer_.Reset();
  } else {
    VLOG(1) << "Chooser " << chooser_id
            << " was closed before the session finished starting. Stopping.";
    StopDiscoverySession(discovery_session.Pass());
  }
}

void BluetoothDispatcherHost::OnDiscoverySessionStartedError(int chooser_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  VLOG(1) << "Failed to start discovery session for " << chooser_id;
  if (RequestDeviceSession* session =
          request_device_sessions_.Lookup(chooser_id)) {
    if (session->chooser && !session->discovery_session) {
      session->chooser->ShowDiscoveryState(
          BluetoothChooser::DiscoveryState::FAILED_TO_START);
    }
  }
  // Ignore discovery session start errors when the dialog was already closed by
  // the time they happen.
}

void BluetoothDispatcherHost::OnBluetoothChooserEvent(
    int chooser_id,
    BluetoothChooser::Event event,
    const std::string& device_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  RequestDeviceSession* session = request_device_sessions_.Lookup(chooser_id);
  DCHECK(session) << "Shouldn't receive an event (" << static_cast<int>(event)
                  << ") from a closed chooser.";
  CHECK(session->chooser) << "Shouldn't receive an event ("
                          << static_cast<int>(event)
                          << ") from a closed chooser.";
  switch (event) {
    case BluetoothChooser::Event::RESCAN:
      StartDeviceDiscovery(session, chooser_id);
      break;
    case BluetoothChooser::Event::DENIED_PERMISSION:
    case BluetoothChooser::Event::CANCELLED:
    case BluetoothChooser::Event::SELECTED: {
      // Synchronously ensure nothing else calls into the chooser after it has
      // asked to be closed.
      session->chooser.reset();

      // Yield to the event loop to make sure we don't destroy the session
      // within a BluetoothDispatcherHost stack frame.
      if (!base::ThreadTaskRunnerHandle::Get()->PostTask(
              FROM_HERE,
              base::Bind(&BluetoothDispatcherHost::FinishClosingChooser,
                         weak_ptr_on_ui_thread_, chooser_id, event,
                         device_id))) {
        LOG(WARNING) << "No TaskRunner; not closing requestDevice dialog.";
      }
      break;
    }
    case BluetoothChooser::Event::SHOW_OVERVIEW_HELP:
      ShowBluetoothOverviewLink();
      break;
    case BluetoothChooser::Event::SHOW_PAIRING_HELP:
      ShowBluetoothPairingLink();
      break;
    case BluetoothChooser::Event::SHOW_ADAPTER_OFF_HELP:
      ShowBluetoothAdapterOffLink();
      break;
    case BluetoothChooser::Event::SHOW_NEED_LOCATION_HELP:
      ShowNeedLocationLink();
      break;
  }
}

void BluetoothDispatcherHost::FinishClosingChooser(
    int chooser_id,
    BluetoothChooser::Event event,
    const std::string& device_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  RequestDeviceSession* session = request_device_sessions_.Lookup(chooser_id);
  DCHECK(session) << "Session removed unexpectedly.";

  if (event == BluetoothChooser::Event::CANCELLED) {
    RecordRequestDeviceOutcome(
        UMARequestDeviceOutcome::BLUETOOTH_CHOOSER_CANCELLED);
    VLOG(1) << "Bluetooth chooser cancelled";
    Send(new BluetoothMsg_RequestDeviceError(
        session->thread_id, session->request_id,
        WebBluetoothError::ChooserCancelled));
    request_device_sessions_.Remove(chooser_id);
    return;
  }
  if (event == BluetoothChooser::Event::DENIED_PERMISSION) {
    RecordRequestDeviceOutcome(
        UMARequestDeviceOutcome::BLUETOOTH_CHOOSER_DENIED_PERMISSION);
    VLOG(1) << "Bluetooth chooser denied permission";
    Send(new BluetoothMsg_RequestDeviceError(
        session->thread_id, session->request_id,
        WebBluetoothError::ChooserDeniedPermission));
    request_device_sessions_.Remove(chooser_id);
    return;
  }
  DCHECK_EQ(static_cast<int>(event),
            static_cast<int>(BluetoothChooser::Event::SELECTED));

  const device::BluetoothDevice* const device = adapter_->GetDevice(device_id);
  if (device == nullptr) {
    VLOG(1) << "Device " << device_id << " no longer in adapter";
    RecordRequestDeviceOutcome(UMARequestDeviceOutcome::CHOSEN_DEVICE_VANISHED);
    Send(new BluetoothMsg_RequestDeviceError(
        session->thread_id, session->request_id,
        WebBluetoothError::ChosenDeviceVanished));
    request_device_sessions_.Remove(chooser_id);
    return;
  }

  VLOG(1) << "Device: " << device->GetName();
  VLOG(1) << "UUIDs: ";
  for (BluetoothUUID uuid : device->GetUUIDs())
    VLOG(1) << "\t" << uuid.canonical_value();

  content::BluetoothDevice device_ipc(
      device->GetAddress(),  // id
      device->GetName(),     // name
      content::BluetoothDevice::ValidatePower(
          device->GetInquiryTxPower()),  // tx_power
      content::BluetoothDevice::ValidatePower(
          device->GetInquiryRSSI()),  // rssi
      device->GetBluetoothClass(),    // device_class
      device->GetVendorIDSource(),    // vendor_id_source
      device->GetVendorID(),          // vendor_id
      device->GetProductID(),         // product_id
      device->GetDeviceID(),          // product_version
      device->IsPaired(),             // paired
      content::BluetoothDevice::UUIDsFromBluetoothUUIDs(
          device->GetUUIDs()));  // uuids
  RecordRequestDeviceOutcome(UMARequestDeviceOutcome::SUCCESS);
  Send(new BluetoothMsg_RequestDeviceSuccess(session->thread_id,
                                             session->request_id, device_ipc));
  request_device_sessions_.Remove(chooser_id);
}

void BluetoothDispatcherHost::OnGATTConnectionCreated(
    int thread_id,
    int request_id,
    const std::string& device_id,
    base::TimeTicks start_time,
    scoped_ptr<device::BluetoothGattConnection> connection) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  connections_.push_back(connection.Pass());
  RecordConnectGATTTimeSuccess(base::TimeTicks::Now() - start_time);
  RecordConnectGATTOutcome(UMAConnectGATTOutcome::SUCCESS);
  Send(new BluetoothMsg_ConnectGATTSuccess(thread_id, request_id, device_id));
}

void BluetoothDispatcherHost::OnCreateGATTConnectionError(
    int thread_id,
    int request_id,
    const std::string& device_id,
    base::TimeTicks start_time,
    device::BluetoothDevice::ConnectErrorCode error_code) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // There was an error creating the ATT Bearer so we reject with
  // NetworkError.
  // https://webbluetoothchrome.github.io/web-bluetooth/#dom-bluetoothdevice-connectgatt
  RecordConnectGATTTimeFailed(base::TimeTicks::Now() - start_time);
  // RecordConnectGATTOutcome is called by TranslateConnectError.
  Send(new BluetoothMsg_ConnectGATTError(thread_id, request_id,
                                         TranslateConnectError(error_code)));
}

void BluetoothDispatcherHost::AddToServicesMapAndSendGetPrimaryServiceSuccess(
    const device::BluetoothGattService& service,
    int thread_id,
    int request_id) {
  const std::string& service_identifier = service.GetIdentifier();
  const std::string& device_id = service.GetDevice()->GetAddress();
  auto insert_result =
      service_to_device_.insert(make_pair(service_identifier, device_id));

  // If a value is already in map, DCHECK it's valid.
  if (!insert_result.second)
    DCHECK_EQ(insert_result.first->second, device_id);

  RecordGetPrimaryServiceOutcome(UMAGetPrimaryServiceOutcome::SUCCESS);
  Send(new BluetoothMsg_GetPrimaryServiceSuccess(thread_id, request_id,
                                                 service_identifier));
}

void BluetoothDispatcherHost::OnCharacteristicValueRead(
    int thread_id,
    int request_id,
    const std::vector<uint8>& value) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  RecordCharacteristicReadValueOutcome(UMAGATTOperationOutcome::SUCCESS);
  Send(new BluetoothMsg_ReadCharacteristicValueSuccess(thread_id, request_id,
                                                       value));
}

void BluetoothDispatcherHost::OnCharacteristicReadValueError(
    int thread_id,
    int request_id,
    device::BluetoothGattService::GattErrorCode error_code) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // TranslateGATTError calls RecordGATTOperationOutcome.
  Send(new BluetoothMsg_ReadCharacteristicValueError(
      thread_id, request_id,
      TranslateGATTError(error_code, UMAGATTOperation::CHARACTERISTIC_READ)));
}

void BluetoothDispatcherHost::OnWriteValueSuccess(int thread_id,
                                                  int request_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  RecordCharacteristicWriteValueOutcome(UMAGATTOperationOutcome::SUCCESS);
  Send(new BluetoothMsg_WriteCharacteristicValueSuccess(thread_id, request_id));
}

void BluetoothDispatcherHost::OnWriteValueFailed(
    int thread_id,
    int request_id,
    device::BluetoothGattService::GattErrorCode error_code) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // TranslateGATTError calls RecordGATTOperationOutcome.
  Send(new BluetoothMsg_WriteCharacteristicValueError(
      thread_id, request_id,
      TranslateGATTError(error_code, UMAGATTOperation::CHARACTERISTIC_WRITE)));
}

void BluetoothDispatcherHost::OnStartNotifySessionSuccess(
    int thread_id,
    int request_id,
    scoped_ptr<device::BluetoothGattNotifySession> notify_session) {
  RecordStartNotificationsOutcome(UMAGATTOperationOutcome::SUCCESS);

  // Copy Characteristic Instance ID before passing scoped pointer because
  // compilers may evaluate arguments in any order.
  const std::string characteristic_instance_id =
      notify_session->GetCharacteristicIdentifier();
  characteristic_id_to_notify_session_.insert(
      std::make_pair(characteristic_instance_id, notify_session.Pass()));

  Send(new BluetoothMsg_StartNotificationsSuccess(thread_id, request_id));
}

void BluetoothDispatcherHost::OnStartNotifySessionFailed(
    int thread_id,
    int request_id,
    device::BluetoothGattService::GattErrorCode error_code) {
  // TranslateGATTError calls RecordGATTOperationOutcome.
  Send(new BluetoothMsg_StartNotificationsError(
      thread_id, request_id,
      TranslateGATTError(error_code, UMAGATTOperation::START_NOTIFICATIONS)));
}

void BluetoothDispatcherHost::OnStopNotifySession(
    int thread_id,
    int request_id,
    const std::string& characteristic_instance_id) {
  characteristic_id_to_notify_session_.erase(characteristic_instance_id);
  Send(new BluetoothMsg_StopNotificationsSuccess(thread_id, request_id));
}

BluetoothDispatcherHost::CacheQueryResult
BluetoothDispatcherHost::QueryCacheForDevice(const std::string& device_id) {
  CacheQueryResult result;
  result.device = adapter_->GetDevice(device_id);
  // When a device can't be found in the BluetoothAdapter, that generally
  // indicates that it's gone out of range. We reject with a NetworkError in
  // that case.
  // https://webbluetoothchrome.github.io/web-bluetooth/#dom-bluetoothdevice-connectgatt
  if (result.device == nullptr) {
    result.outcome = CacheQueryOutcome::NO_DEVICE;
  }
  return result;
}

BluetoothDispatcherHost::CacheQueryResult
BluetoothDispatcherHost::QueryCacheForService(
    const std::string& service_instance_id) {
  auto device_iter = service_to_device_.find(service_instance_id);

  // Kill the renderer, see "ID Not In Map Note" above.
  if (device_iter == service_to_device_.end()) {
    bad_message::ReceivedBadMessage(this, bad_message::BDH_INVALID_SERVICE_ID);
    return CacheQueryResult(CacheQueryOutcome::BAD_RENDERER);
  }

  // TODO(ortuno): Check if domain has access to device.
  // https://crbug.com/493459

  CacheQueryResult result = QueryCacheForDevice(device_iter->second);

  if (result.outcome != CacheQueryOutcome::SUCCESS) {
    return result;
  }

  result.service = result.device->GetGattService(service_instance_id);
  if (result.service == nullptr) {
    result.outcome = CacheQueryOutcome::NO_SERVICE;
  }
  return result;
}

BluetoothDispatcherHost::CacheQueryResult
BluetoothDispatcherHost::QueryCacheForCharacteristic(
    const std::string& characteristic_instance_id) {
  auto characteristic_iter =
      characteristic_to_service_.find(characteristic_instance_id);

  // Kill the renderer, see "ID Not In Map Note" above.
  if (characteristic_iter == characteristic_to_service_.end()) {
    bad_message::ReceivedBadMessage(this,
                                    bad_message::BDH_INVALID_CHARACTERISTIC_ID);
    return CacheQueryResult(CacheQueryOutcome::BAD_RENDERER);
  }

  CacheQueryResult result = QueryCacheForService(characteristic_iter->second);
  if (result.outcome != CacheQueryOutcome::SUCCESS) {
    return result;
  }

  result.characteristic =
      result.service->GetCharacteristic(characteristic_instance_id);

  if (result.characteristic == nullptr) {
    result.outcome = CacheQueryOutcome::NO_CHARACTERISTIC;
  }

  return result;
}

bool BluetoothDispatcherHost::IsServicesDiscoveryCompleteForDevice(
    const std::string& device_id) {
  return ContainsKey(devices_with_discovered_services_, device_id);
}

void BluetoothDispatcherHost::AddToPendingPrimaryServicesRequest(
    const std::string& device_id,
    const PrimaryServicesRequest& request) {
  pending_primary_services_requests_[device_id].push_back(request);
}

void BluetoothDispatcherHost::ShowBluetoothOverviewLink() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  NOTIMPLEMENTED();
}

void BluetoothDispatcherHost::ShowBluetoothPairingLink() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  NOTIMPLEMENTED();
}

void BluetoothDispatcherHost::ShowBluetoothAdapterOffLink() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  NOTIMPLEMENTED();
}

void BluetoothDispatcherHost::ShowNeedLocationLink() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  NOTIMPLEMENTED();
}

}  // namespace content
