// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_POLICY_DEVICE_CLOUD_POLICY_MANAGER_CHROMEOS_H_
#define CHROME_BROWSER_CHROMEOS_POLICY_DEVICE_CLOUD_POLICY_MANAGER_CHROMEOS_H_

#include <bitset>
#include <string>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/chromeos/policy/enrollment_status_chromeos.h"
#include "chrome/browser/policy/cloud/cloud_policy_client.h"
#include "chrome/browser/policy/cloud/cloud_policy_manager.h"
#include "chrome/browser/policy/cloud/cloud_policy_store.h"

namespace chromeos {
namespace attestation {
class AttestationPolicyObserver;
}
}

class PrefRegistrySimple;
class PrefService;

namespace policy {

class DeviceCloudPolicyStoreChromeOS;
class DeviceManagementService;
class EnrollmentHandlerChromeOS;
class EnterpriseInstallAttributes;

// CloudPolicyManager specialization for device policy on Chrome OS. The most
// significant addition is support for device enrollment.
class DeviceCloudPolicyManagerChromeOS : public CloudPolicyManager {
 public:
  typedef std::bitset<32> AllowedDeviceModes;
  typedef base::Callback<void(EnrollmentStatus)> EnrollmentCallback;

  DeviceCloudPolicyManagerChromeOS(
      scoped_ptr<DeviceCloudPolicyStoreChromeOS> store,
      EnterpriseInstallAttributes* install_attributes);
  virtual ~DeviceCloudPolicyManagerChromeOS();

  // Establishes the connection to the cloud, updating policy as necessary.
  void Connect(
      PrefService* local_state,
      DeviceManagementService* device_management_service,
      scoped_ptr<CloudPolicyClient::StatusProvider> device_status_provider);

  // Starts enrollment or re-enrollment. Once the enrollment process completes,
  // |callback| is invoked and gets passed the status of the operation.
  // |allowed_modes| specifies acceptable DEVICE_MODE_* constants for
  // enrollment.
  void StartEnrollment(const std::string& auth_token,
                       bool is_auto_enrollment,
                       const AllowedDeviceModes& allowed_modes,
                       const EnrollmentCallback& callback);

  // Cancels a pending enrollment operation, if any.
  void CancelEnrollment();

  // Gets/Sets the device requisition.
  std::string GetDeviceRequisition() const;
  void SetDeviceRequisition(const std::string& requisition);

  // Checks whether enterprise enrollment should be a regular step during OOBE.
  bool ShouldAutoStartEnrollment() const;

  // Checks whether the user can cancel enrollment.
  bool CanExitEnrollment() const;

  // CloudPolicyManager:
  virtual void Shutdown() OVERRIDE;

  // CloudPolicyStore::Observer:
  virtual void OnStoreLoaded(CloudPolicyStore* store) OVERRIDE;

  // Pref registration helper.
  static void RegisterPrefs(PrefRegistrySimple* registry);

  // Returns the device serial number, or an empty string if not available.
  static std::string GetMachineID();

  // Returns the machine model, or an empty string if not available.
  static std::string GetMachineModel();

  // Returns the robot 'email address' associated with the device robot
  // account (sometimes called a service account) associated with this device
  // during enterprise enrollment.
  std::string GetRobotAccountId();

 private:
  // Creates a new CloudPolicyClient.
  scoped_ptr<CloudPolicyClient> CreateClient();

  // Starts policy refreshes if |store_| indicates a managed device and the
  // necessary dependencies have been provided via Initialize().
  void StartIfManaged();

  // Handles completion signaled by |enrollment_handler_|.
  void EnrollmentCompleted(const EnrollmentCallback& callback,
                           EnrollmentStatus status);

  // Points to the same object as the base CloudPolicyManager::store(), but with
  // actual device policy specific type.
  scoped_ptr<DeviceCloudPolicyStoreChromeOS> device_store_;
  EnterpriseInstallAttributes* install_attributes_;

  DeviceManagementService* device_management_service_;
  scoped_ptr<CloudPolicyClient::StatusProvider> device_status_provider_;

  // PrefService instance to read the policy refresh rate from.
  PrefService* local_state_;

  // Non-null if there is an enrollment operation pending.
  scoped_ptr<EnrollmentHandlerChromeOS> enrollment_handler_;

  scoped_ptr<chromeos::attestation::AttestationPolicyObserver>
      attestation_policy_observer_;

  DISALLOW_COPY_AND_ASSIGN(DeviceCloudPolicyManagerChromeOS);
};

}  // namespace policy

#endif  // CHROME_BROWSER_CHROMEOS_POLICY_DEVICE_CLOUD_POLICY_MANAGER_CHROMEOS_H_
