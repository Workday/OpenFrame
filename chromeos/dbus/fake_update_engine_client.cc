// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/fake_update_engine_client.h"

namespace chromeos {

FakeUpdateEngineClient::FakeUpdateEngineClient()
  : update_check_result_(UpdateEngineClient::UPDATE_RESULT_SUCCESS),
    reboot_after_update_call_count_(0) {
}

FakeUpdateEngineClient::~FakeUpdateEngineClient() {
}

void FakeUpdateEngineClient::AddObserver(Observer* observer) {
}

void FakeUpdateEngineClient::RemoveObserver(Observer* observer) {
}

bool FakeUpdateEngineClient::HasObserver(Observer* observer) {
  return false;
}

void FakeUpdateEngineClient::RequestUpdateCheck(
    const UpdateCheckCallback& callback) {
  callback.Run(update_check_result_);
}

void FakeUpdateEngineClient::RebootAfterUpdate() {
  reboot_after_update_call_count_++;
}

UpdateEngineClient::Status FakeUpdateEngineClient::GetLastStatus() {
  if (status_queue_.empty())
    return default_status_;

  UpdateEngineClient::Status last_status = status_queue_.front();
  status_queue_.pop();
  return last_status;
}

void FakeUpdateEngineClient::SetChannel(const std::string& target_channel,
                                        bool is_powerwash_allowed) {
}

void FakeUpdateEngineClient::GetChannel(bool get_current_channel,
                                        const GetChannelCallback& callback) {
}

void FakeUpdateEngineClient::set_default_status(
    const UpdateEngineClient::Status& status) {
  default_status_ = status;
}

void FakeUpdateEngineClient::set_update_check_result(
    const UpdateEngineClient::UpdateCheckResult& result) {
  update_check_result_ = result;
}

}  // namespace chromeos
