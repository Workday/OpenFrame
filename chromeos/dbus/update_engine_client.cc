// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/update_engine_client.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/strings/string_util.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

const char kReleaseChannelDev[] = "dev-channel";
const char kReleaseChannelBeta[] = "beta-channel";
const char kReleaseChannelStable[] = "stable-channel";

// Returns UPDATE_STATUS_ERROR on error.
UpdateEngineClient::UpdateStatusOperation UpdateStatusFromString(
    const std::string& str) {
  if (str == "UPDATE_STATUS_IDLE")
    return UpdateEngineClient::UPDATE_STATUS_IDLE;
  if (str == "UPDATE_STATUS_CHECKING_FOR_UPDATE")
    return UpdateEngineClient::UPDATE_STATUS_CHECKING_FOR_UPDATE;
  if (str == "UPDATE_STATUS_UPDATE_AVAILABLE")
    return UpdateEngineClient::UPDATE_STATUS_UPDATE_AVAILABLE;
  if (str == "UPDATE_STATUS_DOWNLOADING")
    return UpdateEngineClient::UPDATE_STATUS_DOWNLOADING;
  if (str == "UPDATE_STATUS_VERIFYING")
    return UpdateEngineClient::UPDATE_STATUS_VERIFYING;
  if (str == "UPDATE_STATUS_FINALIZING")
    return UpdateEngineClient::UPDATE_STATUS_FINALIZING;
  if (str == "UPDATE_STATUS_UPDATED_NEED_REBOOT")
    return UpdateEngineClient::UPDATE_STATUS_UPDATED_NEED_REBOOT;
  if (str == "UPDATE_STATUS_REPORTING_ERROR_EVENT")
    return UpdateEngineClient::UPDATE_STATUS_REPORTING_ERROR_EVENT;
  return UpdateEngineClient::UPDATE_STATUS_ERROR;
}

// Used in UpdateEngineClient::EmptyUpdateCheckCallback().
void EmptyUpdateCheckCallbackBody(
    UpdateEngineClient::UpdateCheckResult unused_result) {
}

bool IsValidChannel(const std::string& channel) {
  return channel == kReleaseChannelDev ||
      channel == kReleaseChannelBeta ||
      channel == kReleaseChannelStable;
}

}  // namespace

// The UpdateEngineClient implementation used in production.
class UpdateEngineClientImpl : public UpdateEngineClient {
 public:
  explicit UpdateEngineClientImpl(dbus::Bus* bus)
      : update_engine_proxy_(NULL),
        last_status_(),
        weak_ptr_factory_(this) {
    update_engine_proxy_ = bus->GetObjectProxy(
        update_engine::kUpdateEngineServiceName,
        dbus::ObjectPath(update_engine::kUpdateEngineServicePath));

    // Monitor the D-Bus signal for brightness changes. Only the power
    // manager knows the actual brightness level. We don't cache the
    // brightness level in Chrome as it will make things less reliable.
    update_engine_proxy_->ConnectToSignal(
        update_engine::kUpdateEngineInterface,
        update_engine::kStatusUpdate,
        base::Bind(&UpdateEngineClientImpl::StatusUpdateReceived,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&UpdateEngineClientImpl::StatusUpdateConnected,
                   weak_ptr_factory_.GetWeakPtr()));

    // Get update engine status for the initial status. Update engine won't
    // send StatusUpdate signal unless there is a status change. If chrome
    // crashes after UPDATE_STATUS_UPDATED_NEED_REBOOT status is set,
    // restarted chrome would not get this status. See crbug.com/154104.
    GetUpdateEngineStatus();
  }

  virtual ~UpdateEngineClientImpl() {
  }

  // UpdateEngineClient implementation:
  virtual void AddObserver(Observer* observer) OVERRIDE {
    observers_.AddObserver(observer);
  }

  virtual void RemoveObserver(Observer* observer) OVERRIDE {
    observers_.RemoveObserver(observer);
  }

  virtual bool HasObserver(Observer* observer) OVERRIDE {
    return observers_.HasObserver(observer);
  }

  virtual void RequestUpdateCheck(
      const UpdateCheckCallback& callback) OVERRIDE {
    dbus::MethodCall method_call(
        update_engine::kUpdateEngineInterface,
        update_engine::kAttemptUpdate);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString("");  // Unused.
    writer.AppendString("");  // Unused.

    VLOG(1) << "Requesting an update check";
    update_engine_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&UpdateEngineClientImpl::OnRequestUpdateCheck,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  virtual void RebootAfterUpdate() OVERRIDE {
    dbus::MethodCall method_call(
        update_engine::kUpdateEngineInterface,
        update_engine::kRebootIfNeeded);

    VLOG(1) << "Requesting a reboot";
    update_engine_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&UpdateEngineClientImpl::OnRebootAfterUpdate,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  virtual Status GetLastStatus() OVERRIDE {
    return last_status_;
  }

  virtual void SetChannel(const std::string& target_channel,
                          bool is_powerwash_allowed) OVERRIDE {
    if (!IsValidChannel(target_channel)) {
      LOG(ERROR) << "Invalid channel name: " << target_channel;
      return;
    }

    dbus::MethodCall method_call(
        update_engine::kUpdateEngineInterface,
        update_engine::kSetChannel);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(target_channel);
    writer.AppendBool(is_powerwash_allowed);

    VLOG(1) << "Requesting to set channel: "
            << "target_channel=" << target_channel << ", "
            << "is_powerwash_allowed=" << is_powerwash_allowed;
    update_engine_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&UpdateEngineClientImpl::OnSetChannel,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  virtual void GetChannel(bool get_current_channel,
                          const GetChannelCallback& callback) OVERRIDE {
    dbus::MethodCall method_call(
        update_engine::kUpdateEngineInterface,
        update_engine::kGetChannel);
    dbus::MessageWriter writer(&method_call);
    writer.AppendBool(get_current_channel);

    VLOG(1) << "Requesting to get channel, get_current_channel="
            << get_current_channel;
    update_engine_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&UpdateEngineClientImpl::OnGetChannel,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

 private:
  void GetUpdateEngineStatus() {
    dbus::MethodCall method_call(
        update_engine::kUpdateEngineInterface,
        update_engine::kGetStatus);
    update_engine_proxy_->CallMethodWithErrorCallback(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&UpdateEngineClientImpl::OnGetStatus,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&UpdateEngineClientImpl::OnGetStatusError,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  // Called when a response for RequestUpdateCheck() is received.
  void OnRequestUpdateCheck(const UpdateCheckCallback& callback,
                            dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Failed to request update check";
      callback.Run(UPDATE_RESULT_FAILED);
      return;
    }
    callback.Run(UPDATE_RESULT_SUCCESS);
  }

  // Called when a response for RebootAfterUpdate() is received.
  void OnRebootAfterUpdate(dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Failed to request rebooting after update";
      return;
    }
  }

  // Called when a response for GetStatus is received.
  void OnGetStatus(dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Failed to get response for GetStatus request.";
      return;
    }

    dbus::MessageReader reader(response);
    std::string current_operation;
    Status status;
    if (!(reader.PopInt64(&status.last_checked_time) &&
          reader.PopDouble(&status.download_progress) &&
          reader.PopString(&current_operation) &&
          reader.PopString(&status.new_version) &&
          reader.PopInt64(&status.new_size))) {
      LOG(ERROR) << "GetStatus had incorrect response: "
                 << response->ToString();
      return;
    }
    status.status = UpdateStatusFromString(current_operation);
    last_status_ = status;
    FOR_EACH_OBSERVER(Observer, observers_, UpdateStatusChanged(status));
  }

  // Called when GetStatus call failed.
  void OnGetStatusError(dbus::ErrorResponse* error) {
    LOG(ERROR) << "GetStatus request failed with error: " << error->ToString();
  }

  // Called when a response for SetReleaseChannel() is received.
  void OnSetChannel(dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Failed to request setting channel";
      return;
    }
    VLOG(1) << "Succeeded to set channel";
  }

  // Called when a response for GetChannel() is received.
  void OnGetChannel(const GetChannelCallback& callback,
                    dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Failed to request getting channel";
      callback.Run("");
      return;
    }
    dbus::MessageReader reader(response);
    std::string channel;
    if (!reader.PopString(&channel)) {
      LOG(ERROR) << "Incorrect response: " << response->ToString();
      callback.Run("");
      return;
    }
    VLOG(1) << "The channel received: " << channel;
    callback.Run(channel);
  }

  // Called when a status update signal is received.
  void StatusUpdateReceived(dbus::Signal* signal) {
    VLOG(1) << "Status update signal received: " << signal->ToString();
    dbus::MessageReader reader(signal);
    int64 last_checked_time = 0;
    double progress = 0.0;
    std::string current_operation;
    std::string new_version;
    int64_t new_size = 0;
    if (!(reader.PopInt64(&last_checked_time) &&
          reader.PopDouble(&progress) &&
          reader.PopString(&current_operation) &&
          reader.PopString(&new_version) &&
          reader.PopInt64(&new_size))) {
      LOG(ERROR) << "Status changed signal had incorrect parameters: "
                 << signal->ToString();
      return;
    }
    Status status;
    status.last_checked_time = last_checked_time;
    status.download_progress = progress;
    status.status = UpdateStatusFromString(current_operation);
    status.new_version = new_version;
    status.new_size = new_size;

    last_status_ = status;
    FOR_EACH_OBSERVER(Observer, observers_, UpdateStatusChanged(status));
  }

  // Called when the status update signal is initially connected.
  void StatusUpdateConnected(const std::string& interface_name,
                             const std::string& signal_name,
                             bool success) {
    LOG_IF(WARNING, !success)
        << "Failed to connect to status updated signal.";
  }

  dbus::ObjectProxy* update_engine_proxy_;
  ObserverList<Observer> observers_;
  Status last_status_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<UpdateEngineClientImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(UpdateEngineClientImpl);
};

// The UpdateEngineClient implementation used on Linux desktop,
// which does nothing.
class UpdateEngineClientStubImpl : public UpdateEngineClient {
  // UpdateEngineClient implementation:
  virtual void AddObserver(Observer* observer) OVERRIDE {}
  virtual void RemoveObserver(Observer* observer) OVERRIDE {}
  virtual bool HasObserver(Observer* observer) OVERRIDE { return false; }

  virtual void RequestUpdateCheck(
      const UpdateCheckCallback& callback) OVERRIDE {
    callback.Run(UPDATE_RESULT_NOTIMPLEMENTED);
  }
  virtual void RebootAfterUpdate() OVERRIDE {}
  virtual Status GetLastStatus() OVERRIDE { return Status(); }
  virtual void SetChannel(const std::string& target_channel,
                          bool is_powerwash_allowed) OVERRIDE {
    LOG(INFO) << "Requesting to set channel: "
              << "target_channel=" << target_channel << ", "
              << "is_powerwash_allowed=" << is_powerwash_allowed;
  }
  virtual void GetChannel(bool get_current_channel,
                          const GetChannelCallback& callback) OVERRIDE {
    LOG(INFO) << "Requesting to get channel, get_current_channel="
              << get_current_channel;
    callback.Run("beta-channel");
  }
};

UpdateEngineClient::UpdateEngineClient() {
}

UpdateEngineClient::~UpdateEngineClient() {
}

// static
UpdateEngineClient::UpdateCheckCallback
UpdateEngineClient::EmptyUpdateCheckCallback() {
  return base::Bind(&EmptyUpdateCheckCallbackBody);
}

// static
UpdateEngineClient* UpdateEngineClient::Create(
    DBusClientImplementationType type,
    dbus::Bus* bus) {
  if (type == REAL_DBUS_CLIENT_IMPLEMENTATION)
    return new UpdateEngineClientImpl(bus);
  DCHECK_EQ(STUB_DBUS_CLIENT_IMPLEMENTATION, type);
  return new UpdateEngineClientStubImpl();
}

}  // namespace chromeos
