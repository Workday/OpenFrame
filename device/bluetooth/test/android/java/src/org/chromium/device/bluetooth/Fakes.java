// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.device.bluetooth;

import android.Manifest;
import android.annotation.TargetApi;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.os.Build;
import android.os.ParcelUuid;

import org.chromium.base.Log;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;

/**
 * Fake implementations of android.bluetooth.* classes for testing.
 */
@JNINamespace("device")
@TargetApi(Build.VERSION_CODES.LOLLIPOP)
class Fakes {
    private static final String TAG = "cr.Bluetooth";

    /**
     * Fakes android.bluetooth.BluetoothAdapter.
     */
    static class FakeBluetoothAdapter extends Wrappers.BluetoothAdapterWrapper {
        private final FakeContext mFakeContext;
        private final FakeBluetoothLeScanner mFakeScanner;
        final long mNativeBluetoothTestAndroid;

        /**
         * Creates a FakeBluetoothAdapter.
         */
        @CalledByNative("FakeBluetoothAdapter")
        public static FakeBluetoothAdapter create(long nativeBluetoothTestAndroid) {
            Log.v(TAG, "FakeBluetoothAdapter created.");
            return new FakeBluetoothAdapter(nativeBluetoothTestAndroid);
        }

        private FakeBluetoothAdapter(long nativeBluetoothTestAndroid) {
            super(null, new FakeContext(), new FakeBluetoothLeScanner());
            mNativeBluetoothTestAndroid = nativeBluetoothTestAndroid;
            mFakeContext = (FakeContext) mContext;
            mFakeScanner = (FakeBluetoothLeScanner) mScanner;
        }

        @CalledByNative("FakeBluetoothAdapter")
        public void denyPermission() {
            mFakeContext.mPermissions.clear();
        }

        /**
         * Creates and discovers a new device.
         */
        @CalledByNative("FakeBluetoothAdapter")
        public void discoverLowEnergyDevice(int deviceOrdinal) {
            switch (deviceOrdinal) {
                case 1: {
                    ArrayList<ParcelUuid> uuids = new ArrayList<ParcelUuid>(2);
                    uuids.add(ParcelUuid.fromString("00001800-0000-1000-8000-00805f9b34fb"));
                    uuids.add(ParcelUuid.fromString("00001801-0000-1000-8000-00805f9b34fb"));

                    mFakeScanner.mScanCallback.onScanResult(ScanSettings.CALLBACK_TYPE_ALL_MATCHES,
                            new FakeScanResult(new FakeBluetoothDevice(this, "01:00:00:90:1E:BE",
                                                       "FakeBluetoothDevice"),
                                                                    uuids));
                    break;
                }
                case 2: {
                    ArrayList<ParcelUuid> uuids = new ArrayList<ParcelUuid>(2);
                    uuids.add(ParcelUuid.fromString("00001802-0000-1000-8000-00805f9b34fb"));
                    uuids.add(ParcelUuid.fromString("00001803-0000-1000-8000-00805f9b34fb"));

                    mFakeScanner.mScanCallback.onScanResult(ScanSettings.CALLBACK_TYPE_ALL_MATCHES,
                            new FakeScanResult(new FakeBluetoothDevice(this, "01:00:00:90:1E:BE",
                                                       "FakeBluetoothDevice"),
                                                                    uuids));
                    break;
                }
                case 3: {
                    ArrayList<ParcelUuid> uuids = null;
                    mFakeScanner.mScanCallback.onScanResult(
                            ScanSettings.CALLBACK_TYPE_ALL_MATCHES,
                            new FakeScanResult(
                                    new FakeBluetoothDevice(this, "01:00:00:90:1E:BE", ""), uuids));

                    break;
                }
                case 4: {
                    ArrayList<ParcelUuid> uuids = null;
                    mFakeScanner.mScanCallback.onScanResult(
                            ScanSettings.CALLBACK_TYPE_ALL_MATCHES,
                            new FakeScanResult(
                                    new FakeBluetoothDevice(this, "02:00:00:8B:74:63", ""), uuids));

                    break;
                }
            }
        }

        // -----------------------------------------------------------------------------------------
        // BluetoothAdapterWrapper overrides:

        @Override
        public boolean isEnabled() {
            return true;
        }

        @Override
        public String getAddress() {
            return "A1:B2:C3:D4:E5:F6";
        }

        @Override
        public String getName() {
            return "FakeBluetoothAdapter";
        }

        @Override
        public int getScanMode() {
            return android.bluetooth.BluetoothAdapter.SCAN_MODE_NONE;
        }

        @Override
        public boolean isDiscovering() {
            return false;
        }
    }

    /**
     * Fakes android.content.Context.
     */
    static class FakeContext extends Wrappers.ContextWrapper {
        public final Set<String> mPermissions = new HashSet<String>();

        public FakeContext() {
            super(null);
            mPermissions.add(Manifest.permission.ACCESS_COARSE_LOCATION);
        }

        @Override
        public boolean checkPermission(String permission) {
            return mPermissions.contains(permission);
        }
    }

    /**
     * Fakes android.bluetooth.le.BluetoothLeScanner.
     */
    static class FakeBluetoothLeScanner extends Wrappers.BluetoothLeScannerWrapper {
        public Wrappers.ScanCallbackWrapper mScanCallback;

        private FakeBluetoothLeScanner() {
            super(null);
        }

        @Override
        public void startScan(List<ScanFilter> filters, int scanSettingsScanMode,
                Wrappers.ScanCallbackWrapper callback) {
            if (mScanCallback != null) {
                throw new IllegalArgumentException(
                        "FakeBluetoothLeScanner does not support multiple scans.");
            }
            mScanCallback = callback;
        }

        @Override
        public void stopScan(Wrappers.ScanCallbackWrapper callback) {
            if (mScanCallback != callback) {
                throw new IllegalArgumentException("No scan in progress.");
            }
            mScanCallback = null;
        }
    }

    /**
     * Fakes android.bluetooth.le.ScanResult
     */
    static class FakeScanResult extends Wrappers.ScanResultWrapper {
        private final FakeBluetoothDevice mDevice;
        private final ArrayList<ParcelUuid> mUuids;

        FakeScanResult(FakeBluetoothDevice device, ArrayList<ParcelUuid> uuids) {
            super(null);
            mDevice = device;
            mUuids = uuids;
        }

        @Override
        public Wrappers.BluetoothDeviceWrapper getDevice() {
            return mDevice;
        }

        @Override
        public List<ParcelUuid> getScanRecord_getServiceUuids() {
            return mUuids;
        }
    }

    /**
     * Fakes android.bluetooth.BluetoothDevice.
     */
    static class FakeBluetoothDevice extends Wrappers.BluetoothDeviceWrapper {
        final FakeBluetoothAdapter mAdapter;
        private String mAddress;
        private String mName;
        final FakeBluetoothGatt mGatt;
        private Wrappers.BluetoothGattCallbackWrapper mGattCallback;

        public FakeBluetoothDevice(FakeBluetoothAdapter adapter, String address, String name) {
            super(null);
            mAdapter = adapter;
            mAddress = address;
            mName = name;
            mGatt = new FakeBluetoothGatt(this);
        }

        // Create a call to onConnectionStateChange on the |chrome_device| using parameters
        // |status| & |connected|.
        @CalledByNative("FakeBluetoothDevice")
        private static void connectionStateChange(
                ChromeBluetoothDevice chromeDevice, int status, boolean connected) {
            FakeBluetoothDevice fakeDevice = (FakeBluetoothDevice) chromeDevice.mDevice;
            fakeDevice.mGattCallback.onConnectionStateChange(status, connected
                            ? android.bluetooth.BluetoothProfile.STATE_CONNECTED
                            : android.bluetooth.BluetoothProfile.STATE_DISCONNECTED);
        }

        // Create a call to onServicesDiscovered on the |chrome_device| using parameter
        // |status|.
        @CalledByNative("FakeBluetoothDevice")
        private static void servicesDiscovered(
                ChromeBluetoothDevice chromeDevice, int status, String uuidsSpaceDelimited) {
            FakeBluetoothDevice fakeDevice = (FakeBluetoothDevice) chromeDevice.mDevice;

            if (status == android.bluetooth.BluetoothGatt.GATT_SUCCESS) {
                fakeDevice.mGatt.mServices.clear();
                HashMap<String, Integer> uuidsToInstanceIdMap = new HashMap<String, Integer>();
                for (String uuid : uuidsSpaceDelimited.split(" ")) {
                    Integer previousId = uuidsToInstanceIdMap.get(uuid);
                    int instanceId = (previousId == null) ? 0 : previousId + 1;
                    uuidsToInstanceIdMap.put(uuid, instanceId);
                    fakeDevice.mGatt.mServices.add(new FakeBluetoothGattService(
                            fakeDevice, UUID.fromString(uuid), instanceId));
                }
            }

            fakeDevice.mGattCallback.onServicesDiscovered(status);
        }

        // -----------------------------------------------------------------------------------------
        // Wrappers.BluetoothDeviceWrapper overrides:

        @Override
        public Wrappers.BluetoothGattWrapper connectGatt(Context context, boolean autoConnect,
                Wrappers.BluetoothGattCallbackWrapper callback) {
            if (mGattCallback != null && mGattCallback != callback) {
                throw new IllegalArgumentException(
                        "BluetoothGattWrapper doesn't support calls to connectGatt() with "
                        + "multiple distinct callbacks.");
            }
            nativeOnFakeBluetoothDeviceConnectGattCalled(mAdapter.mNativeBluetoothTestAndroid);
            mGattCallback = callback;
            return mGatt;
        }

        @Override
        public String getAddress() {
            return mAddress;
        }

        @Override
        public int getBluetoothClass_getDeviceClass() {
            return 0x1F00; // Unspecified Device Class
        }

        @Override
        public int getBondState() {
            return BluetoothDevice.BOND_NONE;
        }

        @Override
        public String getName() {
            return mName;
        }
    }

    /**
     * Fakes android.bluetooth.BluetoothGatt.
     */
    static class FakeBluetoothGatt extends Wrappers.BluetoothGattWrapper {
        final FakeBluetoothDevice mDevice;
        final ArrayList<Wrappers.BluetoothGattServiceWrapper> mServices;
        boolean mReadCharacteristicWillFailSynchronouslyOnce = false;
        boolean mSetCharacteristicNotificationWillFailSynchronouslyOnce = false;
        boolean mWriteCharacteristicWillFailSynchronouslyOnce = false;

        public FakeBluetoothGatt(FakeBluetoothDevice device) {
            super(null, null);
            mDevice = device;
            mServices = new ArrayList<Wrappers.BluetoothGattServiceWrapper>();
        }

        @Override
        public void disconnect() {
            nativeOnFakeBluetoothGattDisconnect(mDevice.mAdapter.mNativeBluetoothTestAndroid);
        }

        @Override
        public void discoverServices() {
            nativeOnFakeBluetoothGattDiscoverServices(mDevice.mAdapter.mNativeBluetoothTestAndroid);
        }

        @Override
        public List<Wrappers.BluetoothGattServiceWrapper> getServices() {
            return mServices;
        }

        @Override
        boolean readCharacteristic(Wrappers.BluetoothGattCharacteristicWrapper characteristic) {
            if (mReadCharacteristicWillFailSynchronouslyOnce) {
                mReadCharacteristicWillFailSynchronouslyOnce = false;
                return false;
            }
            nativeOnFakeBluetoothGattReadCharacteristic(
                    mDevice.mAdapter.mNativeBluetoothTestAndroid);
            return true;
        }

        @Override
        boolean setCharacteristicNotification(
                Wrappers.BluetoothGattCharacteristicWrapper characteristic, boolean enable) {
            if (mSetCharacteristicNotificationWillFailSynchronouslyOnce) {
                mSetCharacteristicNotificationWillFailSynchronouslyOnce = false;
                return false;
            }
            nativeOnFakeBluetoothGattSetCharacteristicNotification(
                    mDevice.mAdapter.mNativeBluetoothTestAndroid);
            return true;
        }

        @Override
        boolean writeCharacteristic(Wrappers.BluetoothGattCharacteristicWrapper characteristic) {
            if (mWriteCharacteristicWillFailSynchronouslyOnce) {
                mWriteCharacteristicWillFailSynchronouslyOnce = false;
                return false;
            }
            nativeOnFakeBluetoothGattWriteCharacteristic(
                    mDevice.mAdapter.mNativeBluetoothTestAndroid, characteristic.getValue());
            return true;
        }
    }

    /**
     * Fakes android.bluetooth.BluetoothGattService.
     */
    static class FakeBluetoothGattService extends Wrappers.BluetoothGattServiceWrapper {
        final FakeBluetoothDevice mDevice;
        final int mInstanceId;
        final UUID mUuid;
        final ArrayList<Wrappers.BluetoothGattCharacteristicWrapper> mCharacteristics;

        public FakeBluetoothGattService(FakeBluetoothDevice device, UUID uuid, int instanceId) {
            super(null, null);
            mDevice = device;
            mUuid = uuid;
            mInstanceId = instanceId;
            mCharacteristics = new ArrayList<Wrappers.BluetoothGattCharacteristicWrapper>();
        }

        // Create a characteristic and add it to this service.
        @CalledByNative("FakeBluetoothGattService")
        private static void addCharacteristic(
                ChromeBluetoothRemoteGattService chromeService, String uuidString, int properties) {
            FakeBluetoothGattService fakeService =
                    (FakeBluetoothGattService) chromeService.mService;
            UUID uuid = UUID.fromString(uuidString);

            int countOfDuplicateUUID = 0;
            for (Wrappers.BluetoothGattCharacteristicWrapper characteristic :
                    fakeService.mCharacteristics) {
                if (characteristic.getUuid().equals(uuid)) {
                    countOfDuplicateUUID++;
                }
            }
            fakeService.mCharacteristics.add(new FakeBluetoothGattCharacteristic(fakeService,
                    /* instanceId */ countOfDuplicateUUID, properties, uuid));
        }

        // -----------------------------------------------------------------------------------------
        // Wrappers.BluetoothGattServiceWrapper overrides:

        @Override
        public List<Wrappers.BluetoothGattCharacteristicWrapper> getCharacteristics() {
            return mCharacteristics;
        }

        @Override
        public int getInstanceId() {
            return mInstanceId;
        }

        @Override
        public UUID getUuid() {
            return mUuid;
        }
    }

    /**
     * Fakes android.bluetooth.BluetoothGattCharacteristic.
     */
    static class FakeBluetoothGattCharacteristic
            extends Wrappers.BluetoothGattCharacteristicWrapper {
        final FakeBluetoothGattService mService;
        final int mInstanceId;
        final int mProperties;
        final UUID mUuid;
        byte[] mValue;

        public FakeBluetoothGattCharacteristic(
                FakeBluetoothGattService service, int instanceId, int properties, UUID uuid) {
            super(null);
            mService = service;
            mInstanceId = instanceId;
            mProperties = properties;
            mUuid = uuid;
            mValue = new byte[0];
        }

        // Simulate a value being read from a characteristic.
        @CalledByNative("FakeBluetoothGattCharacteristic")
        private static void valueRead(ChromeBluetoothRemoteGattCharacteristic chromeCharacteristic,
                int status, byte[] value) {
            FakeBluetoothGattCharacteristic fakeCharacteristic =
                    (FakeBluetoothGattCharacteristic) chromeCharacteristic.mCharacteristic;

            fakeCharacteristic.mValue = value;
            fakeCharacteristic.mService.mDevice.mGattCallback.onCharacteristicRead(
                    fakeCharacteristic, status);
        }

        // Simulate a value being written to a characteristic.
        @CalledByNative("FakeBluetoothGattCharacteristic")
        private static void valueWrite(
                ChromeBluetoothRemoteGattCharacteristic chromeCharacteristic, int status) {
            FakeBluetoothGattCharacteristic fakeCharacteristic =
                    (FakeBluetoothGattCharacteristic) chromeCharacteristic.mCharacteristic;

            fakeCharacteristic.mService.mDevice.mGattCallback.onCharacteristicWrite(
                    fakeCharacteristic, status);
        }

        // Cause subsequent value reads of a characteristic to fail synchronously.
        @CalledByNative("FakeBluetoothGattCharacteristic")
        private static void setCharacteristicNotificationWillFailSynchronouslyOnce(
                ChromeBluetoothRemoteGattCharacteristic chromeCharacteristic) {
            FakeBluetoothGattCharacteristic fakeCharacteristic =
                    (FakeBluetoothGattCharacteristic) chromeCharacteristic.mCharacteristic;

            fakeCharacteristic.mService.mDevice.mGatt
                    .mSetCharacteristicNotificationWillFailSynchronouslyOnce = true;
        }

        // Cause subsequent value reads of a characteristic to fail synchronously.
        @CalledByNative("FakeBluetoothGattCharacteristic")
        private static void setReadCharacteristicWillFailSynchronouslyOnce(
                ChromeBluetoothRemoteGattCharacteristic chromeCharacteristic) {
            FakeBluetoothGattCharacteristic fakeCharacteristic =
                    (FakeBluetoothGattCharacteristic) chromeCharacteristic.mCharacteristic;

            fakeCharacteristic.mService.mDevice.mGatt.mReadCharacteristicWillFailSynchronouslyOnce =
                    true;
        }

        // Cause subsequent value writes of a characteristic to fail synchronously.
        @CalledByNative("FakeBluetoothGattCharacteristic")
        private static void setWriteCharacteristicWillFailSynchronouslyOnce(
                ChromeBluetoothRemoteGattCharacteristic chromeCharacteristic) {
            FakeBluetoothGattCharacteristic fakeCharacteristic =
                    (FakeBluetoothGattCharacteristic) chromeCharacteristic.mCharacteristic;

            fakeCharacteristic.mService.mDevice.mGatt
                    .mWriteCharacteristicWillFailSynchronouslyOnce = true;
        }

        // -----------------------------------------------------------------------------------------
        // Wrappers.BluetoothGattCharacteristicWrapper overrides:

        @Override
        public int getInstanceId() {
            return mInstanceId;
        }

        @Override
        public int getProperties() {
            return mProperties;
        }

        @Override
        public UUID getUuid() {
            return mUuid;
        }

        @Override
        public byte[] getValue() {
            return mValue;
        }

        @Override
        public boolean setValue(byte[] value) {
            mValue = value;
            return true;
        }
    }

    // ---------------------------------------------------------------------------------------------
    // BluetoothTestAndroid C++ methods declared for access from java:

    // Binds to BluetoothTestAndroid::OnFakeBluetoothDeviceConnectGattCalled.
    private static native void nativeOnFakeBluetoothDeviceConnectGattCalled(
            long nativeBluetoothTestAndroid);

    // Binds to BluetoothTestAndroid::OnFakeBluetoothGattDisconnect.
    private static native void nativeOnFakeBluetoothGattDisconnect(long nativeBluetoothTestAndroid);

    // Binds to BluetoothTestAndroid::OnFakeBluetoothGattDiscoverServices.
    private static native void nativeOnFakeBluetoothGattDiscoverServices(
            long nativeBluetoothTestAndroid);

    // Binds to BluetoothTestAndroid::OnFakeBluetoothGattSetCharacteristicNotification.
    private static native void nativeOnFakeBluetoothGattSetCharacteristicNotification(
            long nativeBluetoothTestAndroid);

    // Binds to BluetoothTestAndroid::OnFakeBluetoothGattReadCharacteristic.
    private static native void nativeOnFakeBluetoothGattReadCharacteristic(
            long nativeBluetoothTestAndroid);

    // Binds to BluetoothTestAndroid::OnFakeBluetoothGattWriteCharacteristic.
    private static native void nativeOnFakeBluetoothGattWriteCharacteristic(
            long nativeBluetoothTestAndroid, byte[] value);
}
