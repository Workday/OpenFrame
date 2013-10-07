// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/device_cloud_policy_store_chromeos.h"

#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/run_loop.h"
#include "chrome/browser/chromeos/policy/enterprise_install_attributes.h"
#include "chrome/browser/chromeos/settings/device_settings_test_helper.h"
#include "chrome/browser/policy/proto/chromeos/chrome_device_policy.pb.h"
#include "chromeos/cryptohome/cryptohome_library.h"
#include "chromeos/dbus/cryptohome_client.h"
#include "policy/policy_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace policy {

namespace {

void CopyLockResult(base::RunLoop* loop,
                    EnterpriseInstallAttributes::LockResult* out,
                    EnterpriseInstallAttributes::LockResult result) {
  *out = result;
  loop->Quit();
}

}  // namespace

class DeviceCloudPolicyStoreChromeOSTest
    : public chromeos::DeviceSettingsTestBase {
 protected:
  DeviceCloudPolicyStoreChromeOSTest()
      : cryptohome_library_(chromeos::CryptohomeLibrary::GetTestImpl()),
        stub_cryptohome_client_(chromeos::CryptohomeClient::Create(
            chromeos::STUB_DBUS_CLIENT_IMPLEMENTATION, NULL)),
        install_attributes_(new EnterpriseInstallAttributes(
            cryptohome_library_.get(), stub_cryptohome_client_.get())),
        store_(new DeviceCloudPolicyStoreChromeOS(&device_settings_service_,
                                                  install_attributes_.get())) {}

  virtual void SetUp() OVERRIDE {
    DeviceSettingsTestBase::SetUp();

    base::RunLoop loop;
    EnterpriseInstallAttributes::LockResult result;
    install_attributes_->LockDevice(
        PolicyBuilder::kFakeUsername,
        DEVICE_MODE_ENTERPRISE,
        PolicyBuilder::kFakeDeviceId,
        base::Bind(&CopyLockResult, &loop, &result));
    loop.Run();
    ASSERT_EQ(EnterpriseInstallAttributes::LOCK_SUCCESS, result);
  }

  void ExpectFailure(CloudPolicyStore::Status expected_status) {
    EXPECT_EQ(expected_status, store_->status());
    EXPECT_TRUE(store_->is_initialized());
    EXPECT_FALSE(store_->has_policy());
    EXPECT_FALSE(store_->is_managed());
  }

  void ExpectSuccess() {
    EXPECT_EQ(CloudPolicyStore::STATUS_OK, store_->status());
    EXPECT_TRUE(store_->is_initialized());
    EXPECT_TRUE(store_->has_policy());
    EXPECT_TRUE(store_->is_managed());
    EXPECT_TRUE(store_->policy());
    base::FundamentalValue expected(false);
    EXPECT_TRUE(
        base::Value::Equals(&expected,
                            store_->policy_map().GetValue(
                                key::kDeviceMetricsReportingEnabled)));
  }

  void PrepareExistingPolicy() {
    store_->Load();
    FlushDeviceSettings();
    ExpectSuccess();

    device_policy_.UnsetNewSigningKey();
    device_policy_.Build();
  }

  void PrepareNewSigningKey() {
    device_policy_.SetDefaultNewSigningKey();
    device_policy_.Build();
    owner_key_util_->SetPublicKeyFromPrivateKey(
        *device_policy_.GetNewSigningKey());
  }

  void ResetToNonEnterprise() {
    store_.reset();
    cryptohome_library_->InstallAttributesSet("enterprise.owned",
                                              std::string());
    install_attributes_.reset(new EnterpriseInstallAttributes(
        cryptohome_library_.get(), stub_cryptohome_client_.get()));
    store_.reset(new DeviceCloudPolicyStoreChromeOS(&device_settings_service_,
                                                    install_attributes_.get()));
  }

  scoped_ptr<chromeos::CryptohomeLibrary> cryptohome_library_;
  scoped_ptr<chromeos::CryptohomeClient> stub_cryptohome_client_;
  scoped_ptr<EnterpriseInstallAttributes> install_attributes_;

  scoped_ptr<DeviceCloudPolicyStoreChromeOS> store_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DeviceCloudPolicyStoreChromeOSTest);
};

TEST_F(DeviceCloudPolicyStoreChromeOSTest, LoadNoKey) {
  owner_key_util_->Clear();
  store_->Load();
  FlushDeviceSettings();
  ExpectFailure(CloudPolicyStore::STATUS_BAD_STATE);
}

TEST_F(DeviceCloudPolicyStoreChromeOSTest, LoadNoPolicy) {
  device_settings_test_helper_.set_policy_blob(std::string());
  store_->Load();
  FlushDeviceSettings();
  ExpectFailure(CloudPolicyStore::STATUS_LOAD_ERROR);
}

TEST_F(DeviceCloudPolicyStoreChromeOSTest, LoadNotEnterprise) {
  ResetToNonEnterprise();
  store_->Load();
  FlushDeviceSettings();
  ExpectFailure(CloudPolicyStore::STATUS_BAD_STATE);
}

TEST_F(DeviceCloudPolicyStoreChromeOSTest, LoadSuccess) {
  store_->Load();
  FlushDeviceSettings();
  ExpectSuccess();
}

TEST_F(DeviceCloudPolicyStoreChromeOSTest, StoreSuccess) {
  PrepareExistingPolicy();
  store_->Store(device_policy_.policy());
  FlushDeviceSettings();
  ExpectSuccess();
}

TEST_F(DeviceCloudPolicyStoreChromeOSTest, StoreNoSignature) {
  PrepareExistingPolicy();
  device_policy_.policy().clear_policy_data_signature();
  store_->Store(device_policy_.policy());
  FlushDeviceSettings();
  EXPECT_EQ(CloudPolicyStore::STATUS_VALIDATION_ERROR, store_->status());
  EXPECT_EQ(CloudPolicyValidatorBase::VALIDATION_BAD_SIGNATURE,
            store_->validation_status());
}

TEST_F(DeviceCloudPolicyStoreChromeOSTest, StoreBadSignature) {
  PrepareExistingPolicy();
  device_policy_.policy().set_policy_data_signature("invalid");
  store_->Store(device_policy_.policy());
  FlushDeviceSettings();
  EXPECT_EQ(CloudPolicyStore::STATUS_VALIDATION_ERROR, store_->status());
  EXPECT_EQ(CloudPolicyValidatorBase::VALIDATION_BAD_SIGNATURE,
            store_->validation_status());
}

TEST_F(DeviceCloudPolicyStoreChromeOSTest, StoreKeyRotation) {
  PrepareExistingPolicy();
  device_policy_.SetDefaultNewSigningKey();
  device_policy_.Build();
  store_->Store(device_policy_.policy());
  device_settings_test_helper_.FlushLoops();
  device_settings_test_helper_.FlushStore();
  owner_key_util_->SetPublicKeyFromPrivateKey(
      *device_policy_.GetNewSigningKey());
  ReloadDeviceSettings();
  ExpectSuccess();
}

TEST_F(DeviceCloudPolicyStoreChromeOSTest, InstallInitialPolicySuccess) {
  PrepareNewSigningKey();
  store_->InstallInitialPolicy(device_policy_.policy());
  FlushDeviceSettings();
  ExpectSuccess();
}

TEST_F(DeviceCloudPolicyStoreChromeOSTest, InstallInitialPolicyNoSignature) {
  PrepareNewSigningKey();
  device_policy_.policy().clear_policy_data_signature();
  store_->InstallInitialPolicy(device_policy_.policy());
  FlushDeviceSettings();
  ExpectFailure(CloudPolicyStore::STATUS_VALIDATION_ERROR);
  EXPECT_EQ(CloudPolicyValidatorBase::VALIDATION_BAD_INITIAL_SIGNATURE,
            store_->validation_status());
}

TEST_F(DeviceCloudPolicyStoreChromeOSTest, InstallInitialPolicyNoKey) {
  PrepareNewSigningKey();
  device_policy_.policy().clear_new_public_key();
  store_->InstallInitialPolicy(device_policy_.policy());
  FlushDeviceSettings();
  ExpectFailure(CloudPolicyStore::STATUS_VALIDATION_ERROR);
  EXPECT_EQ(CloudPolicyValidatorBase::VALIDATION_BAD_INITIAL_SIGNATURE,
            store_->validation_status());
}

TEST_F(DeviceCloudPolicyStoreChromeOSTest, InstallInitialPolicyNotEnterprise) {
  PrepareNewSigningKey();
  ResetToNonEnterprise();
  store_->InstallInitialPolicy(device_policy_.policy());
  FlushDeviceSettings();
  ExpectFailure(CloudPolicyStore::STATUS_BAD_STATE);
}

}  // namespace policy
