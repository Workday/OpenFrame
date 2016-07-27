// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/test/bluetooth_test_android.h"

#include <iterator>
#include <sstream>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "device/bluetooth/android/wrappers.h"
#include "device/bluetooth/bluetooth_adapter_android.h"
#include "device/bluetooth/bluetooth_device_android.h"
#include "device/bluetooth/bluetooth_remote_gatt_characteristic_android.h"
#include "device/bluetooth/bluetooth_remote_gatt_service_android.h"
#include "device/bluetooth/test/test_bluetooth_adapter_observer.h"
#include "jni/Fakes_jni.h"

using base::android::AttachCurrentThread;
using base::android::ScopedJavaLocalRef;

namespace device {

BluetoothTestAndroid::BluetoothTestAndroid() {
}

BluetoothTestAndroid::~BluetoothTestAndroid() {
}

void BluetoothTestAndroid::SetUp() {
  // Register in SetUp so that ASSERT can be used.
  ASSERT_TRUE(RegisterNativesImpl(AttachCurrentThread()));
}

bool BluetoothTestAndroid::PlatformSupportsLowEnergy() {
  return true;
}

void BluetoothTestAndroid::InitWithDefaultAdapter() {
  adapter_ =
      BluetoothAdapterAndroid::Create(
          BluetoothAdapterWrapper_CreateWithDefaultAdapter().obj()).get();
}

void BluetoothTestAndroid::InitWithoutDefaultAdapter() {
  adapter_ = BluetoothAdapterAndroid::Create(NULL).get();
}

void BluetoothTestAndroid::InitWithFakeAdapter() {
  j_fake_bluetooth_adapter_.Reset(Java_FakeBluetoothAdapter_create(
      AttachCurrentThread(), reinterpret_cast<intptr_t>(this)));

  adapter_ =
      BluetoothAdapterAndroid::Create(j_fake_bluetooth_adapter_.obj()).get();
}

bool BluetoothTestAndroid::DenyPermission() {
  Java_FakeBluetoothAdapter_denyPermission(AttachCurrentThread(),
                                           j_fake_bluetooth_adapter_.obj());
  return true;
}

BluetoothDevice* BluetoothTestAndroid::DiscoverLowEnergyDevice(
    int device_ordinal) {
  TestBluetoothAdapterObserver observer(adapter_);
  Java_FakeBluetoothAdapter_discoverLowEnergyDevice(
      AttachCurrentThread(), j_fake_bluetooth_adapter_.obj(), device_ordinal);
  return observer.last_device();
}

void BluetoothTestAndroid::SimulateGattConnection(BluetoothDevice* device) {
  BluetoothDeviceAndroid* device_android =
      static_cast<BluetoothDeviceAndroid*>(device);

  Java_FakeBluetoothDevice_connectionStateChange(
      AttachCurrentThread(), device_android->GetJavaObject().obj(),
      0,      // android.bluetooth.BluetoothGatt.GATT_SUCCESS
      true);  // connected
}

void BluetoothTestAndroid::SimulateGattConnectionError(
    BluetoothDevice* device,
    BluetoothDevice::ConnectErrorCode error) {
  int android_error_value = 0;
  switch (error) {  // Constants are from android.bluetooth.BluetoothGatt.
    case BluetoothDevice::ERROR_FAILED:
      android_error_value = 0x00000101;  // GATT_FAILURE
      break;
    case BluetoothDevice::ERROR_AUTH_FAILED:
      android_error_value = 0x00000005;  // GATT_INSUFFICIENT_AUTHENTICATION
      break;
    case BluetoothDevice::ERROR_UNKNOWN:
    case BluetoothDevice::ERROR_INPROGRESS:
    case BluetoothDevice::ERROR_AUTH_CANCELED:
    case BluetoothDevice::ERROR_AUTH_REJECTED:
    case BluetoothDevice::ERROR_AUTH_TIMEOUT:
    case BluetoothDevice::ERROR_UNSUPPORTED_DEVICE:
      NOTREACHED() << "No translation for error code: " << error;
  }

  BluetoothDeviceAndroid* device_android =
      static_cast<BluetoothDeviceAndroid*>(device);

  Java_FakeBluetoothDevice_connectionStateChange(
      AttachCurrentThread(), device_android->GetJavaObject().obj(),
      android_error_value,
      false);  // connected
}

void BluetoothTestAndroid::SimulateGattDisconnection(BluetoothDevice* device) {
  BluetoothDeviceAndroid* device_android =
      static_cast<BluetoothDeviceAndroid*>(device);

  Java_FakeBluetoothDevice_connectionStateChange(
      AttachCurrentThread(), device_android->GetJavaObject().obj(),
      0,       // android.bluetooth.BluetoothGatt.GATT_SUCCESS
      false);  // disconnected
}

void BluetoothTestAndroid::SimulateGattServicesDiscovered(
    BluetoothDevice* device,
    const std::vector<std::string>& uuids) {
  BluetoothDeviceAndroid* device_android =
      static_cast<BluetoothDeviceAndroid*>(device);
  JNIEnv* env = base::android::AttachCurrentThread();

  // Join UUID strings into a single string.
  std::ostringstream uuids_space_delimited;
  std::copy(uuids.begin(), uuids.end(),
            std::ostream_iterator<std::string>(uuids_space_delimited, " "));

  Java_FakeBluetoothDevice_servicesDiscovered(
      env, device_android->GetJavaObject().obj(),
      0,  // android.bluetooth.BluetoothGatt.GATT_SUCCESS
      base::android::ConvertUTF8ToJavaString(env, uuids_space_delimited.str())
          .obj());
}

void BluetoothTestAndroid::SimulateGattServicesDiscoveryError(
    BluetoothDevice* device) {
  BluetoothDeviceAndroid* device_android =
      static_cast<BluetoothDeviceAndroid*>(device);

  Java_FakeBluetoothDevice_servicesDiscovered(
      AttachCurrentThread(), device_android->GetJavaObject().obj(),
      0x00000101,  // android.bluetooth.BluetoothGatt.GATT_FAILURE
      nullptr);
}

void BluetoothTestAndroid::SimulateGattCharacteristic(
    BluetoothGattService* service,
    const std::string& uuid,
    int properties) {
  BluetoothRemoteGattServiceAndroid* service_android =
      static_cast<BluetoothRemoteGattServiceAndroid*>(service);
  JNIEnv* env = base::android::AttachCurrentThread();

  Java_FakeBluetoothGattService_addCharacteristic(
      env, service_android->GetJavaObject().obj(),
      base::android::ConvertUTF8ToJavaString(env, uuid).obj(), properties);
}

void BluetoothTestAndroid::SimulateGattNotifySessionStarted(
    BluetoothGattCharacteristic* characteristic) {
  // Android doesn't provide any sort of callback for when notifications have
  // been enabled. So, just run the message loop to process the success
  // callback.
  base::RunLoop().RunUntilIdle();
}

void BluetoothTestAndroid::
    SimulateGattCharacteristicSetNotifyWillFailSynchronouslyOnce(
        BluetoothGattCharacteristic* characteristic) {
  BluetoothRemoteGattCharacteristicAndroid* characteristic_android =
      static_cast<BluetoothRemoteGattCharacteristicAndroid*>(characteristic);
  JNIEnv* env = base::android::AttachCurrentThread();

  Java_FakeBluetoothGattCharacteristic_setCharacteristicNotificationWillFailSynchronouslyOnce(
      env, characteristic_android->GetJavaObject().obj());
}

void BluetoothTestAndroid::SimulateGattCharacteristicRead(
    BluetoothGattCharacteristic* characteristic,
    const std::vector<uint8>& value) {
  BluetoothRemoteGattCharacteristicAndroid* characteristic_android =
      static_cast<BluetoothRemoteGattCharacteristicAndroid*>(characteristic);
  JNIEnv* env = base::android::AttachCurrentThread();

  Java_FakeBluetoothGattCharacteristic_valueRead(
      env, characteristic_android->GetJavaObject().obj(),
      0,  // android.bluetooth.BluetoothGatt.GATT_SUCCESS
      base::android::ToJavaByteArray(env, value).obj());
}

void BluetoothTestAndroid::SimulateGattCharacteristicReadError(
    BluetoothGattCharacteristic* characteristic,
    BluetoothGattService::GattErrorCode error_code) {
  BluetoothRemoteGattCharacteristicAndroid* characteristic_android =
      static_cast<BluetoothRemoteGattCharacteristicAndroid*>(characteristic);
  JNIEnv* env = base::android::AttachCurrentThread();
  std::vector<uint8> empty_value;

  Java_FakeBluetoothGattCharacteristic_valueRead(
      env, characteristic_android->GetJavaObject().obj(),
      BluetoothRemoteGattServiceAndroid::GetAndroidErrorCode(error_code),
      base::android::ToJavaByteArray(env, empty_value).obj());
}

void BluetoothTestAndroid::
    SimulateGattCharacteristicReadWillFailSynchronouslyOnce(
        BluetoothGattCharacteristic* characteristic) {
  BluetoothRemoteGattCharacteristicAndroid* characteristic_android =
      static_cast<BluetoothRemoteGattCharacteristicAndroid*>(characteristic);
  JNIEnv* env = base::android::AttachCurrentThread();

  Java_FakeBluetoothGattCharacteristic_setReadCharacteristicWillFailSynchronouslyOnce(
      env, characteristic_android->GetJavaObject().obj());
}

void BluetoothTestAndroid::SimulateGattCharacteristicWrite(
    BluetoothGattCharacteristic* characteristic) {
  BluetoothRemoteGattCharacteristicAndroid* characteristic_android =
      static_cast<BluetoothRemoteGattCharacteristicAndroid*>(characteristic);
  Java_FakeBluetoothGattCharacteristic_valueWrite(
      base::android::AttachCurrentThread(),
      characteristic_android->GetJavaObject().obj(),
      0);  // android.bluetooth.BluetoothGatt.GATT_SUCCESS
}

void BluetoothTestAndroid::SimulateGattCharacteristicWriteError(
    BluetoothGattCharacteristic* characteristic,
    BluetoothGattService::GattErrorCode error_code) {
  BluetoothRemoteGattCharacteristicAndroid* characteristic_android =
      static_cast<BluetoothRemoteGattCharacteristicAndroid*>(characteristic);
  Java_FakeBluetoothGattCharacteristic_valueWrite(
      base::android::AttachCurrentThread(),
      characteristic_android->GetJavaObject().obj(),
      BluetoothRemoteGattServiceAndroid::GetAndroidErrorCode(error_code));
}

void BluetoothTestAndroid::
    SimulateGattCharacteristicWriteWillFailSynchronouslyOnce(
        BluetoothGattCharacteristic* characteristic) {
  BluetoothRemoteGattCharacteristicAndroid* characteristic_android =
      static_cast<BluetoothRemoteGattCharacteristicAndroid*>(characteristic);
  Java_FakeBluetoothGattCharacteristic_setWriteCharacteristicWillFailSynchronouslyOnce(
      base::android::AttachCurrentThread(),
      characteristic_android->GetJavaObject().obj());
}

void BluetoothTestAndroid::OnFakeBluetoothDeviceConnectGattCalled(
    JNIEnv* env,
    const JavaParamRef<jobject>& caller) {
  gatt_connection_attempts_++;
}

void BluetoothTestAndroid::OnFakeBluetoothGattDisconnect(
    JNIEnv* env,
    const JavaParamRef<jobject>& caller) {
  gatt_disconnection_attempts_++;
}

void BluetoothTestAndroid::OnFakeBluetoothGattDiscoverServices(
    JNIEnv* env,
    const JavaParamRef<jobject>& caller) {
  gatt_discovery_attempts_++;
}

void BluetoothTestAndroid::OnFakeBluetoothGattSetCharacteristicNotification(
    JNIEnv* env,
    const JavaParamRef<jobject>& caller) {
  gatt_notify_characteristic_attempts_++;
}

void BluetoothTestAndroid::OnFakeBluetoothGattReadCharacteristic(
    JNIEnv* env,
    const JavaParamRef<jobject>& caller) {
  gatt_read_characteristic_attempts_++;
}

void BluetoothTestAndroid::OnFakeBluetoothGattWriteCharacteristic(
    JNIEnv* env,
    const JavaParamRef<jobject>& caller,
    const JavaParamRef<jbyteArray>& value) {
  gatt_write_characteristic_attempts_++;
  base::android::JavaByteArrayToByteVector(env, value, &last_write_value_);
}

}  // namespace device
