// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_FAKE_SESSION_MANAGER_CLIENT_H_
#define CHROMEOS_DBUS_FAKE_SESSION_MANAGER_CLIENT_H_

#include <map>
#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/observer_list.h"
#include "chromeos/dbus/session_manager_client.h"

namespace chromeos {

// A fake implementation of session_manager. Accepts policy blobs to be set and
// returns them unmodified.
class FakeSessionManagerClient : public chromeos::SessionManagerClient {
 public:
  FakeSessionManagerClient();
  virtual ~FakeSessionManagerClient();

  // SessionManagerClient:
  virtual void AddObserver(Observer* observer) OVERRIDE;
  virtual void RemoveObserver(Observer* observer) OVERRIDE;
  virtual bool HasObserver(Observer* observer) OVERRIDE;
  virtual void EmitLoginPromptReady() OVERRIDE;
  virtual void EmitLoginPromptVisible() OVERRIDE;
  virtual void RestartJob(int pid, const std::string& command_line) OVERRIDE;
  virtual void RestartEntd() OVERRIDE;
  virtual void StartSession(const std::string& user_email) OVERRIDE;
  virtual void StopSession() OVERRIDE;
  virtual void StartDeviceWipe() OVERRIDE;
  virtual void RequestLockScreen() OVERRIDE;
  virtual void NotifyLockScreenShown() OVERRIDE;
  virtual void RequestUnlockScreen() OVERRIDE;
  virtual void NotifyLockScreenDismissed() OVERRIDE;
  virtual void RetrieveActiveSessions(
      const ActiveSessionsCallback& callback) OVERRIDE;
  virtual void RetrieveDevicePolicy(
      const RetrievePolicyCallback& callback) OVERRIDE;
  virtual void RetrievePolicyForUser(
      const std::string& username,
      const RetrievePolicyCallback& callback) OVERRIDE;
  virtual std::string BlockingRetrievePolicyForUser(
      const std::string& username) OVERRIDE;
  virtual void RetrieveDeviceLocalAccountPolicy(
      const std::string& account_id,
      const RetrievePolicyCallback& callback) OVERRIDE;
  virtual void StoreDevicePolicy(const std::string& policy_blob,
                                 const StorePolicyCallback& callback) OVERRIDE;
  virtual void StorePolicyForUser(const std::string& username,
                                  const std::string& policy_blob,
                                  const std::string& policy_key,
                                  const StorePolicyCallback& callback) OVERRIDE;
  virtual void StoreDeviceLocalAccountPolicy(
      const std::string& account_id,
      const std::string& policy_blob,
      const StorePolicyCallback& callback) OVERRIDE;
  virtual void SetFlagsForUser(const std::string& username,
                               const std::vector<std::string>& flags) OVERRIDE;

  const std::string& device_policy() const;
  void set_device_policy(const std::string& policy_blob);

  const std::string& user_policy(const std::string& username) const;
  void set_user_policy(const std::string& username,
                       const std::string& policy_blob);

  const std::string& device_local_account_policy(
      const std::string& account_id) const;
  void set_device_local_account_policy(const std::string& account_id,
                                       const std::string& policy_blob);

  // Notify observers about a property change completion.
  void OnPropertyChangeComplete(bool success);

  // Returns how many times EmitLoginPromptReady() is called.
  int emit_login_prompt_ready_call_count() {
    return emit_login_prompt_ready_call_count_;
  }

  // Returns how many times LockScreenShown() was called.
  int notify_lock_screen_shown_call_count() {
    return notify_lock_screen_shown_call_count_;
  }

  // Returns how many times LockScreenDismissed() was called.
  int notify_lock_screen_dismissed_call_count() {
    return notify_lock_screen_dismissed_call_count_;
  }

 private:
  std::string device_policy_;
  std::map<std::string, std::string> user_policies_;
  std::map<std::string, std::string> device_local_account_policy_;
  ObserverList<Observer> observers_;
  SessionManagerClient::ActiveSessionsMap user_sessions_;

  int emit_login_prompt_ready_call_count_;
  int notify_lock_screen_shown_call_count_;
  int notify_lock_screen_dismissed_call_count_;

  DISALLOW_COPY_AND_ASSIGN(FakeSessionManagerClient);
};

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_FAKE_SESSION_MANAGER_CLIENT_H_
