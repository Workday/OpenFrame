// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/test/integration/sync_integration_test_util.h"

#include "chrome/browser/sync/test/integration/single_client_status_change_checker.h"
#include "chrome/browser/sync/test/integration/updated_progress_marker_checker.h"
#include "components/browser_sync/browser/profile_sync_service.h"

namespace sync_integration_test_util {

class PassphraseRequiredChecker : public SingleClientStatusChangeChecker {
 public:
  explicit PassphraseRequiredChecker(ProfileSyncService* service)
      : SingleClientStatusChangeChecker(service) {}

  bool IsExitConditionSatisfied() override {
    return service()->IsPassphraseRequired();
  }

  std::string GetDebugMessage() const override { return "Passhrase Required"; }
};

class PassphraseAcceptedChecker : public SingleClientStatusChangeChecker {
 public:
  explicit PassphraseAcceptedChecker(ProfileSyncService* service)
      : SingleClientStatusChangeChecker(service) {}

  bool IsExitConditionSatisfied() override {
    return !service()->IsPassphraseRequired() &&
        service()->IsUsingSecondaryPassphrase();
  }

  std::string GetDebugMessage() const override { return "Passhrase Accepted"; }
};

bool AwaitPassphraseRequired(ProfileSyncService* service) {
  PassphraseRequiredChecker checker(service);
  checker.Wait();
  return !checker.TimedOut();
}

bool AwaitPassphraseAccepted(ProfileSyncService* service) {
  PassphraseAcceptedChecker checker(service);
  checker.Wait();
  return !checker.TimedOut();
}

bool AwaitCommitActivityCompletion(ProfileSyncService* service) {
  UpdatedProgressMarkerChecker checker(service);
  checker.Wait();
  return !checker.TimedOut();
}

}  // namespace sync_integration_test_util
