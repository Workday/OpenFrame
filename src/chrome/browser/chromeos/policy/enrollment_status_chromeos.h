// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_POLICY_ENROLLMENT_STATUS_CHROMEOS_H_
#define CHROME_BROWSER_CHROMEOS_POLICY_ENROLLMENT_STATUS_CHROMEOS_H_

#include "chrome/browser/policy/cloud/cloud_policy_constants.h"
#include "chrome/browser/policy/cloud/cloud_policy_store.h"
#include "chrome/browser/policy/cloud/cloud_policy_validator.h"

namespace policy {

// Describes the result of an enrollment operation, including the relevant error
// codes received from the involved components.
class EnrollmentStatus {
 public:
  // Enrollment status codes.
  enum Status {
    STATUS_SUCCESS,                     // Enrollment succeeded.
    STATUS_REGISTRATION_FAILED,         // DM registration failed.
    STATUS_REGISTRATION_BAD_MODE,       // Bad device mode.
    STATUS_POLICY_FETCH_FAILED,         // DM policy fetch failed.
    STATUS_VALIDATION_FAILED,           // Policy validation failed.
    STATUS_LOCK_ERROR,                  // Cryptohome failed to lock the device.
    STATUS_LOCK_TIMEOUT,                // Timeout while waiting for the lock.
    STATUS_LOCK_WRONG_USER,             // Locked to different domain.
    STATUS_STORE_ERROR,                 // Failed to store the policy.
  };

  // Helpers for constructing errors for relevant cases.
  static EnrollmentStatus ForStatus(Status status);
  static EnrollmentStatus ForRegistrationError(
      DeviceManagementStatus client_status);
  static EnrollmentStatus ForFetchError(DeviceManagementStatus client_status);
  static EnrollmentStatus ForValidationError(
      CloudPolicyValidatorBase::Status validation_status);
  static EnrollmentStatus ForStoreError(
      CloudPolicyStore::Status store_error,
      CloudPolicyValidatorBase::Status validation_status);

  Status status() const { return status_; }
  DeviceManagementStatus client_status() const { return client_status_; }
  CloudPolicyStore::Status store_status() const { return store_status_; }
  CloudPolicyValidatorBase::Status validation_status() const {
    return validation_status_;
  }

 private:
  EnrollmentStatus(Status status,
                   DeviceManagementStatus client_status,
                   CloudPolicyStore::Status store_status,
                   CloudPolicyValidatorBase::Status validation_status);

  Status status_;
  DeviceManagementStatus client_status_;
  CloudPolicyStore::Status store_status_;
  CloudPolicyValidatorBase::Status validation_status_;
};

}  // namespace policy

#endif  // CHROME_BROWSER_CHROMEOS_POLICY_ENROLLMENT_STATUS_CHROMEOS_H_
