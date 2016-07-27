// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_adapter_android.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/sequenced_task_runner.h"
#include "base/single_thread_task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "device/bluetooth/android/wrappers.h"
#include "device/bluetooth/bluetooth_advertisement.h"
#include "device/bluetooth/bluetooth_device_android.h"
#include "device/bluetooth/bluetooth_discovery_session_outcome.h"
#include "jni/ChromeBluetoothAdapter_jni.h"

using base::android::AttachCurrentThread;
using base::android::ConvertJavaStringToUTF8;

namespace device {

// static
base::WeakPtr<BluetoothAdapter> BluetoothAdapter::CreateAdapter(
    const InitCallback& init_callback) {
  return BluetoothAdapterAndroid::Create(
      BluetoothAdapterWrapper_CreateWithDefaultAdapter().obj());
}

// static
base::WeakPtr<BluetoothAdapterAndroid> BluetoothAdapterAndroid::Create(
    jobject bluetooth_adapter_wrapper) {  // Java Type: bluetoothAdapterWrapper
  BluetoothAdapterAndroid* adapter = new BluetoothAdapterAndroid();

  adapter->j_adapter_.Reset(Java_ChromeBluetoothAdapter_create(
      AttachCurrentThread(), reinterpret_cast<intptr_t>(adapter),
      bluetooth_adapter_wrapper));

  return adapter->weak_ptr_factory_.GetWeakPtr();
}

// static
bool BluetoothAdapterAndroid::RegisterJNI(JNIEnv* env) {
  return RegisterNativesImpl(env);  // Generated in BluetoothAdapter_jni.h
}

std::string BluetoothAdapterAndroid::GetAddress() const {
  return ConvertJavaStringToUTF8(Java_ChromeBluetoothAdapter_getAddress(
      AttachCurrentThread(), j_adapter_.obj()));
}

std::string BluetoothAdapterAndroid::GetName() const {
  return ConvertJavaStringToUTF8(Java_ChromeBluetoothAdapter_getName(
      AttachCurrentThread(), j_adapter_.obj()));
}

void BluetoothAdapterAndroid::SetName(const std::string& name,
                                      const base::Closure& callback,
                                      const ErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}

bool BluetoothAdapterAndroid::IsInitialized() const {
  return true;
}

bool BluetoothAdapterAndroid::IsPresent() const {
  return Java_ChromeBluetoothAdapter_isPresent(AttachCurrentThread(),
                                               j_adapter_.obj());
}

bool BluetoothAdapterAndroid::IsPowered() const {
  return Java_ChromeBluetoothAdapter_isPowered(AttachCurrentThread(),
                                               j_adapter_.obj());
}

void BluetoothAdapterAndroid::SetPowered(bool powered,
                                         const base::Closure& callback,
                                         const ErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}

bool BluetoothAdapterAndroid::IsDiscoverable() const {
  return Java_ChromeBluetoothAdapter_isDiscoverable(AttachCurrentThread(),
                                                    j_adapter_.obj());
}

void BluetoothAdapterAndroid::SetDiscoverable(
    bool discoverable,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}

bool BluetoothAdapterAndroid::IsDiscovering() const {
  return Java_ChromeBluetoothAdapter_isDiscovering(AttachCurrentThread(),
                                                   j_adapter_.obj());
}

void BluetoothAdapterAndroid::CreateRfcommService(
    const BluetoothUUID& uuid,
    const ServiceOptions& options,
    const CreateServiceCallback& callback,
    const CreateServiceErrorCallback& error_callback) {
  NOTIMPLEMENTED();
  error_callback.Run("Not Implemented");
}

void BluetoothAdapterAndroid::CreateL2capService(
    const BluetoothUUID& uuid,
    const ServiceOptions& options,
    const CreateServiceCallback& callback,
    const CreateServiceErrorCallback& error_callback) {
  NOTIMPLEMENTED();
  error_callback.Run("Not Implemented");
}

void BluetoothAdapterAndroid::RegisterAudioSink(
    const BluetoothAudioSink::Options& options,
    const AcquiredCallback& callback,
    const BluetoothAudioSink::ErrorCallback& error_callback) {
  error_callback.Run(BluetoothAudioSink::ERROR_UNSUPPORTED_PLATFORM);
}

void BluetoothAdapterAndroid::RegisterAdvertisement(
    scoped_ptr<BluetoothAdvertisement::Data> advertisement_data,
    const CreateAdvertisementCallback& callback,
    const CreateAdvertisementErrorCallback& error_callback) {
  error_callback.Run(BluetoothAdvertisement::ERROR_UNSUPPORTED_PLATFORM);
}

void BluetoothAdapterAndroid::OnScanFailed(
    JNIEnv* env,
    const JavaParamRef<jobject>& caller) {
  MarkDiscoverySessionsAsInactive();
}

void BluetoothAdapterAndroid::CreateOrUpdateDeviceOnScan(
    JNIEnv* env,
    const JavaParamRef<jobject>& caller,
    const JavaParamRef<jstring>& address,
    const JavaParamRef<jobject>&
        bluetooth_device_wrapper,  // Java Type: bluetoothDeviceWrapper
    const JavaParamRef<jobject>&
        advertised_uuids) {  // Java Type: List<ParcelUuid>
  std::string device_address = ConvertJavaStringToUTF8(env, address);
  DevicesMap::const_iterator iter = devices_.find(device_address);

  if (iter == devices_.end()) {
    // New device.
    BluetoothDeviceAndroid* device_android =
        BluetoothDeviceAndroid::Create(this, bluetooth_device_wrapper);
    device_android->UpdateAdvertisedUUIDs(advertised_uuids);
    devices_.add(device_address, scoped_ptr<BluetoothDevice>(device_android));
    FOR_EACH_OBSERVER(BluetoothAdapter::Observer, observers_,
                      DeviceAdded(this, device_android));
  } else {
    // Existing device.
    BluetoothDeviceAndroid* device_android =
        static_cast<BluetoothDeviceAndroid*>(iter->second);
    if (device_android->UpdateAdvertisedUUIDs(advertised_uuids)) {
      FOR_EACH_OBSERVER(BluetoothAdapter::Observer, observers_,
                        DeviceChanged(this, device_android));
    }
  }
}

BluetoothAdapterAndroid::BluetoothAdapterAndroid() : weak_ptr_factory_(this) {
}

BluetoothAdapterAndroid::~BluetoothAdapterAndroid() {
  Java_ChromeBluetoothAdapter_onBluetoothAdapterAndroidDestruction(
      AttachCurrentThread(), j_adapter_.obj());
}

void BluetoothAdapterAndroid::AddDiscoverySession(
    BluetoothDiscoveryFilter* discovery_filter,
    const base::Closure& callback,
    const DiscoverySessionErrorCallback& error_callback) {
  // TODO(scheib): Support filters crbug.com/490401
  if (Java_ChromeBluetoothAdapter_addDiscoverySession(AttachCurrentThread(),
                                                      j_adapter_.obj())) {
    callback.Run();
  } else {
    // TODO(scheib): Eventually wire the SCAN_FAILED result through to here.
    error_callback.Run(UMABluetoothDiscoverySessionOutcome::UNKNOWN);
  }
}

void BluetoothAdapterAndroid::RemoveDiscoverySession(
    BluetoothDiscoveryFilter* discovery_filter,
    const base::Closure& callback,
    const DiscoverySessionErrorCallback& error_callback) {
  if (Java_ChromeBluetoothAdapter_removeDiscoverySession(AttachCurrentThread(),
                                                         j_adapter_.obj())) {
    callback.Run();
  } else {
    // TODO(scheib): Eventually wire the SCAN_FAILED result through to here.
    error_callback.Run(UMABluetoothDiscoverySessionOutcome::UNKNOWN);
  }
}

void BluetoothAdapterAndroid::SetDiscoveryFilter(
    scoped_ptr<BluetoothDiscoveryFilter> discovery_filter,
    const base::Closure& callback,
    const DiscoverySessionErrorCallback& error_callback) {
  // TODO(scheib): Support filters crbug.com/490401
  NOTIMPLEMENTED();
  error_callback.Run(UMABluetoothDiscoverySessionOutcome::NOT_IMPLEMENTED);
}

void BluetoothAdapterAndroid::RemovePairingDelegateInternal(
    device::BluetoothDevice::PairingDelegate* pairing_delegate) {
}

}  // namespace device
