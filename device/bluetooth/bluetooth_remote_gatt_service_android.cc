// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_remote_gatt_service_android.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "device/bluetooth/bluetooth_adapter_android.h"
#include "device/bluetooth/bluetooth_device_android.h"
#include "device/bluetooth/bluetooth_remote_gatt_characteristic_android.h"
#include "jni/ChromeBluetoothRemoteGattService_jni.h"

using base::android::AttachCurrentThread;

namespace device {

// static
scoped_ptr<BluetoothRemoteGattServiceAndroid>
BluetoothRemoteGattServiceAndroid::Create(
    BluetoothAdapterAndroid* adapter,
    BluetoothDeviceAndroid* device,
    jobject /* BluetoothGattServiceWrapper */ bluetooth_gatt_service_wrapper,
    const std::string& instanceId,
    jobject /* ChromeBluetoothDevice */ chrome_bluetooth_device) {
  scoped_ptr<BluetoothRemoteGattServiceAndroid> service(
      new BluetoothRemoteGattServiceAndroid(adapter, device, instanceId));

  JNIEnv* env = AttachCurrentThread();
  service->j_service_.Reset(Java_ChromeBluetoothRemoteGattService_create(
      env, reinterpret_cast<intptr_t>(service.get()),
      bluetooth_gatt_service_wrapper,
      base::android::ConvertUTF8ToJavaString(env, instanceId).obj(),
      chrome_bluetooth_device));

  return service;
}

BluetoothRemoteGattServiceAndroid::~BluetoothRemoteGattServiceAndroid() {
  Java_ChromeBluetoothRemoteGattService_onBluetoothRemoteGattServiceAndroidDestruction(
      AttachCurrentThread(), j_service_.obj());
}

// static
bool BluetoothRemoteGattServiceAndroid::RegisterJNI(JNIEnv* env) {
  return RegisterNativesImpl(
      env);  // Generated in ChromeBluetoothRemoteGattService_jni.h
}

base::android::ScopedJavaLocalRef<jobject>
BluetoothRemoteGattServiceAndroid::GetJavaObject() {
  return base::android::ScopedJavaLocalRef<jobject>(j_service_);
}

// static
BluetoothGattService::GattErrorCode
BluetoothRemoteGattServiceAndroid::GetGattErrorCode(int bluetooth_gatt_code) {
  DCHECK(bluetooth_gatt_code != 0) << "Only errors valid. 0 == GATT_SUCCESS.";

  // TODO(scheib) Create new BluetoothGattService::GattErrorCode enums for
  // android values not yet represented. http://crbug.com/548498
  switch (bluetooth_gatt_code) {  // android.bluetooth.BluetoothGatt values:
    case 0x00000101:              // GATT_FAILURE
      return GATT_ERROR_FAILED;
    case 0x0000000d:  // GATT_INVALID_ATTRIBUTE_LENGTH
      return GATT_ERROR_INVALID_LENGTH;
    case 0x00000002:  // GATT_READ_NOT_PERMITTED
      return GATT_ERROR_NOT_PERMITTED;
    case 0x00000006:  // GATT_REQUEST_NOT_SUPPORTED
      return GATT_ERROR_NOT_SUPPORTED;
    case 0x00000003:  // GATT_WRITE_NOT_PERMITTED
      return GATT_ERROR_NOT_PERMITTED;
    default:
      VLOG(1) << "Unhandled status: " << bluetooth_gatt_code;
      return BluetoothGattService::GATT_ERROR_UNKNOWN;
  }
}

// static
int BluetoothRemoteGattServiceAndroid::GetAndroidErrorCode(
    BluetoothGattService::GattErrorCode error_code) {
  // TODO(scheib) Create new BluetoothGattService::GattErrorCode enums for
  // android values not yet represented. http://crbug.com/548498
  switch (error_code) {  // Return values from android.bluetooth.BluetoothGatt:
    case GATT_ERROR_UNKNOWN:
      return 0x00000101;  // GATT_FAILURE. No good match.
    case GATT_ERROR_FAILED:
      return 0x00000101;  // GATT_FAILURE
    case GATT_ERROR_IN_PROGRESS:
      return 0x00000101;  // GATT_FAILURE. No good match.
    case GATT_ERROR_INVALID_LENGTH:
      return 0x0000000d;  // GATT_INVALID_ATTRIBUTE_LENGTH
    case GATT_ERROR_NOT_PERMITTED:
      // Can't distinguish between:
      // 0x00000002:  // GATT_READ_NOT_PERMITTED
      // 0x00000003:  // GATT_WRITE_NOT_PERMITTED
      return 0x00000101;  // GATT_FAILURE. No good match.
    case GATT_ERROR_NOT_AUTHORIZED:
      return 0x00000101;  // GATT_FAILURE. No good match.
    case GATT_ERROR_NOT_PAIRED:
      return 0x00000101;  // GATT_FAILURE. No good match.
    case GATT_ERROR_NOT_SUPPORTED:
      return 0x00000006;  // GATT_REQUEST_NOT_SUPPORTED
  }
  VLOG(1) << "Unhandled error_code: " << error_code;
  return 0x00000101;  // GATT_FAILURE. No good match.
}

std::string BluetoothRemoteGattServiceAndroid::GetIdentifier() const {
  return instanceId_;
}

device::BluetoothUUID BluetoothRemoteGattServiceAndroid::GetUUID() const {
  return device::BluetoothUUID(
      ConvertJavaStringToUTF8(Java_ChromeBluetoothRemoteGattService_getUUID(
          AttachCurrentThread(), j_service_.obj())));
}

bool BluetoothRemoteGattServiceAndroid::IsLocal() const {
  return false;
}

bool BluetoothRemoteGattServiceAndroid::IsPrimary() const {
  NOTIMPLEMENTED();
  return true;
}

device::BluetoothDevice* BluetoothRemoteGattServiceAndroid::GetDevice() const {
  return device_;
}

std::vector<device::BluetoothGattCharacteristic*>
BluetoothRemoteGattServiceAndroid::GetCharacteristics() const {
  EnsureCharacteristicsCreated();
  std::vector<device::BluetoothGattCharacteristic*> characteristics;
  for (const auto& map_iter : characteristics_)
    characteristics.push_back(map_iter.second);
  return characteristics;
}

std::vector<device::BluetoothGattService*>
BluetoothRemoteGattServiceAndroid::GetIncludedServices() const {
  NOTIMPLEMENTED();
  return std::vector<device::BluetoothGattService*>();
}

device::BluetoothGattCharacteristic*
BluetoothRemoteGattServiceAndroid::GetCharacteristic(
    const std::string& identifier) const {
  EnsureCharacteristicsCreated();
  const auto& iter = characteristics_.find(identifier);
  if (iter == characteristics_.end())
    return nullptr;
  return iter->second;
}

bool BluetoothRemoteGattServiceAndroid::AddCharacteristic(
    device::BluetoothGattCharacteristic* characteristic) {
  return false;
}

bool BluetoothRemoteGattServiceAndroid::AddIncludedService(
    device::BluetoothGattService* service) {
  return false;
}

void BluetoothRemoteGattServiceAndroid::Register(
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  error_callback.Run();
}

void BluetoothRemoteGattServiceAndroid::Unregister(
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  error_callback.Run();
}

void BluetoothRemoteGattServiceAndroid::CreateGattRemoteCharacteristic(
    JNIEnv* env,
    const JavaParamRef<jobject>& caller,
    const JavaParamRef<jstring>& instanceId,
    const JavaParamRef<jobject>& /* BluetoothGattCharacteristicWrapper */
    bluetooth_gatt_characteristic_wrapper,
    const JavaParamRef<
        jobject>& /* ChromeBluetoothDevice */ chrome_bluetooth_device) {
  std::string instanceIdString =
      base::android::ConvertJavaStringToUTF8(env, instanceId);

  DCHECK(!characteristics_.contains(instanceIdString));

  characteristics_.set(
      instanceIdString,
      BluetoothRemoteGattCharacteristicAndroid::Create(
          instanceIdString, bluetooth_gatt_characteristic_wrapper,
          chrome_bluetooth_device));
}

BluetoothRemoteGattServiceAndroid::BluetoothRemoteGattServiceAndroid(
    BluetoothAdapterAndroid* adapter,
    BluetoothDeviceAndroid* device,
    const std::string& instanceId)
    : adapter_(adapter), device_(device), instanceId_(instanceId) {}

void BluetoothRemoteGattServiceAndroid::EnsureCharacteristicsCreated() const {
  if (!characteristics_.empty())
    return;

  // Java call
  Java_ChromeBluetoothRemoteGattService_ensureCharacteristicsCreated(
      AttachCurrentThread(), j_service_.obj());
}

}  // namespace device
