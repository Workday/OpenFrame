// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/power_manager_client.h"

#include <algorithm>

#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/observer_list.h"
#include "base/strings/stringprintf.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/dbus/power_manager/input_event.pb.h"
#include "chromeos/dbus/power_manager/peripheral_battery_status.pb.h"
#include "chromeos/dbus/power_manager/policy.pb.h"
#include "chromeos/dbus/power_manager/power_supply_properties.pb.h"
#include "chromeos/dbus/power_manager/suspend.pb.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"

namespace chromeos {

// Maximum amount of time that the power manager will wait for Chrome to
// say that it's ready for the system to be suspended, in milliseconds.
const int kSuspendDelayTimeoutMs = 5000;

// Human-readable description of Chrome's suspend delay.
const char kSuspendDelayDescription[] = "chrome";

// The PowerManagerClient implementation used in production.
class PowerManagerClientImpl : public PowerManagerClient {
 public:
  explicit PowerManagerClientImpl(dbus::Bus* bus)
      : origin_thread_id_(base::PlatformThread::CurrentId()),
        power_manager_proxy_(NULL),
        suspend_delay_id_(-1),
        has_suspend_delay_id_(false),
        pending_suspend_id_(-1),
        suspend_is_pending_(false),
        num_pending_suspend_readiness_callbacks_(0),
        last_is_projecting_(false),
        weak_ptr_factory_(this) {
    power_manager_proxy_ = bus->GetObjectProxy(
        power_manager::kPowerManagerServiceName,
        dbus::ObjectPath(power_manager::kPowerManagerServicePath));

    power_manager_proxy_->SetNameOwnerChangedCallback(
        base::Bind(&PowerManagerClientImpl::NameOwnerChangedReceived,
                   weak_ptr_factory_.GetWeakPtr()));

    // Monitor the D-Bus signal for brightness changes. Only the power
    // manager knows the actual brightness level. We don't cache the
    // brightness level in Chrome as it'll make things less reliable.
    power_manager_proxy_->ConnectToSignal(
        power_manager::kPowerManagerInterface,
        power_manager::kBrightnessChangedSignal,
        base::Bind(&PowerManagerClientImpl::BrightnessChangedReceived,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&PowerManagerClientImpl::SignalConnected,
                   weak_ptr_factory_.GetWeakPtr()));

    power_manager_proxy_->ConnectToSignal(
        power_manager::kPowerManagerInterface,
        power_manager::kPeripheralBatteryStatusSignal,
        base::Bind(&PowerManagerClientImpl::PeripheralBatteryStatusReceived,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&PowerManagerClientImpl::SignalConnected,
                   weak_ptr_factory_.GetWeakPtr()));

    power_manager_proxy_->ConnectToSignal(
        power_manager::kPowerManagerInterface,
        power_manager::kPowerSupplyPollSignal,
        base::Bind(&PowerManagerClientImpl::PowerSupplyPollReceived,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&PowerManagerClientImpl::SignalConnected,
                   weak_ptr_factory_.GetWeakPtr()));

    power_manager_proxy_->ConnectToSignal(
        power_manager::kPowerManagerInterface,
        power_manager::kIdleNotifySignal,
        base::Bind(&PowerManagerClientImpl::IdleNotifySignalReceived,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&PowerManagerClientImpl::SignalConnected,
                   weak_ptr_factory_.GetWeakPtr()));

    power_manager_proxy_->ConnectToSignal(
        power_manager::kPowerManagerInterface,
        power_manager::kInputEventSignal,
        base::Bind(&PowerManagerClientImpl::InputEventReceived,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&PowerManagerClientImpl::SignalConnected,
                   weak_ptr_factory_.GetWeakPtr()));

    power_manager_proxy_->ConnectToSignal(
        power_manager::kPowerManagerInterface,
        power_manager::kSuspendStateChangedSignal,
        base::Bind(&PowerManagerClientImpl::SuspendStateChangedReceived,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&PowerManagerClientImpl::SignalConnected,
                   weak_ptr_factory_.GetWeakPtr()));

    power_manager_proxy_->ConnectToSignal(
        power_manager::kPowerManagerInterface,
        power_manager::kSuspendImminentSignal,
        base::Bind(
            &PowerManagerClientImpl::SuspendImminentReceived,
            weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&PowerManagerClientImpl::SignalConnected,
                   weak_ptr_factory_.GetWeakPtr()));

    power_manager_proxy_->ConnectToSignal(
        power_manager::kPowerManagerInterface,
        power_manager::kIdleActionImminentSignal,
        base::Bind(
            &PowerManagerClientImpl::IdleActionImminentReceived,
            weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&PowerManagerClientImpl::SignalConnected,
                   weak_ptr_factory_.GetWeakPtr()));

    power_manager_proxy_->ConnectToSignal(
        power_manager::kPowerManagerInterface,
        power_manager::kIdleActionDeferredSignal,
        base::Bind(
            &PowerManagerClientImpl::IdleActionDeferredReceived,
            weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&PowerManagerClientImpl::SignalConnected,
                   weak_ptr_factory_.GetWeakPtr()));

    RegisterSuspendDelay();
  }

  virtual ~PowerManagerClientImpl() {
    // Here we should unregister suspend notifications from powerd,
    // however:
    // - The lifetime of the PowerManagerClientImpl can extend past that of
    //   the objectproxy,
    // - power_manager can already detect that the client is gone and
    //   unregister our suspend delay.
  }

  // PowerManagerClient overrides:

  virtual void AddObserver(Observer* observer) OVERRIDE {
    CHECK(observer);  // http://crbug.com/119976
    observers_.AddObserver(observer);
  }

  virtual void RemoveObserver(Observer* observer) OVERRIDE {
    observers_.RemoveObserver(observer);
  }

  virtual bool HasObserver(Observer* observer) OVERRIDE {
    return observers_.HasObserver(observer);
  }

  virtual void DecreaseScreenBrightness(bool allow_off) OVERRIDE {
    dbus::MethodCall method_call(
        power_manager::kPowerManagerInterface,
        power_manager::kDecreaseScreenBrightness);
    dbus::MessageWriter writer(&method_call);
    writer.AppendBool(allow_off);
    power_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        dbus::ObjectProxy::EmptyResponseCallback());
  }

  virtual void IncreaseScreenBrightness() OVERRIDE {
    SimpleMethodCallToPowerManager(power_manager::kIncreaseScreenBrightness);
  }

  virtual void DecreaseKeyboardBrightness() OVERRIDE {
    SimpleMethodCallToPowerManager(power_manager::kDecreaseKeyboardBrightness);
  }

  virtual void IncreaseKeyboardBrightness() OVERRIDE {
    SimpleMethodCallToPowerManager(power_manager::kIncreaseKeyboardBrightness);
  }

  virtual void SetScreenBrightnessPercent(double percent,
                                          bool gradual) OVERRIDE {
    dbus::MethodCall method_call(
        power_manager::kPowerManagerInterface,
        power_manager::kSetScreenBrightnessPercent);
    dbus::MessageWriter writer(&method_call);
    writer.AppendDouble(percent);
    writer.AppendInt32(
        gradual ?
        power_manager::kBrightnessTransitionGradual :
        power_manager::kBrightnessTransitionInstant);
    power_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        dbus::ObjectProxy::EmptyResponseCallback());
  }

  virtual void GetScreenBrightnessPercent(
      const GetScreenBrightnessPercentCallback& callback) OVERRIDE {
    dbus::MethodCall method_call(power_manager::kPowerManagerInterface,
                                 power_manager::kGetScreenBrightnessPercent);
    power_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&PowerManagerClientImpl::OnGetScreenBrightnessPercent,
                   weak_ptr_factory_.GetWeakPtr(), callback));
  }

  virtual void RequestStatusUpdate() OVERRIDE {
    dbus::MethodCall method_call(
        power_manager::kPowerManagerInterface,
        power_manager::kGetPowerSupplyPropertiesMethod);
    power_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&PowerManagerClientImpl::OnGetPowerSupplyPropertiesMethod,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  virtual void RequestRestart() OVERRIDE {
    SimpleMethodCallToPowerManager(power_manager::kRequestRestartMethod);
  };

  virtual void RequestShutdown() OVERRIDE {
    SimpleMethodCallToPowerManager(power_manager::kRequestShutdownMethod);
  }

  virtual void RequestIdleNotification(int64 threshold) OVERRIDE {
    dbus::MethodCall method_call(power_manager::kPowerManagerInterface,
                                 power_manager::kRequestIdleNotification);
    dbus::MessageWriter writer(&method_call);
    writer.AppendInt64(threshold);

    power_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        dbus::ObjectProxy::EmptyResponseCallback());
  }

  virtual void NotifyUserActivity(
      power_manager::UserActivityType type) OVERRIDE {
    dbus::MethodCall method_call(
        power_manager::kPowerManagerInterface,
        power_manager::kHandleUserActivityMethod);
    dbus::MessageWriter writer(&method_call);
    writer.AppendInt32(type);

    power_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        dbus::ObjectProxy::EmptyResponseCallback());
  }

  virtual void NotifyVideoActivity(bool is_fullscreen) OVERRIDE {
    dbus::MethodCall method_call(
        power_manager::kPowerManagerInterface,
        power_manager::kHandleVideoActivityMethod);
    dbus::MessageWriter writer(&method_call);
    writer.AppendBool(is_fullscreen);

    power_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        dbus::ObjectProxy::EmptyResponseCallback());
  }

  virtual void SetPolicy(
      const power_manager::PowerManagementPolicy& policy) OVERRIDE {
    dbus::MethodCall method_call(
        power_manager::kPowerManagerInterface,
        power_manager::kSetPolicyMethod);
    dbus::MessageWriter writer(&method_call);
    if (!writer.AppendProtoAsArrayOfBytes(policy)) {
      LOG(ERROR) << "Error calling " << power_manager::kSetPolicyMethod;
      return;
    }
    power_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        dbus::ObjectProxy::EmptyResponseCallback());
  }

  virtual void SetIsProjecting(bool is_projecting) OVERRIDE {
    dbus::MethodCall method_call(
        power_manager::kPowerManagerInterface,
        power_manager::kSetIsProjectingMethod);
    dbus::MessageWriter writer(&method_call);
    writer.AppendBool(is_projecting);
    power_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        dbus::ObjectProxy::EmptyResponseCallback());
    last_is_projecting_ = is_projecting;
  }

  virtual base::Closure GetSuspendReadinessCallback() OVERRIDE {
    DCHECK(OnOriginThread());
    DCHECK(suspend_is_pending_);
    num_pending_suspend_readiness_callbacks_++;
    return base::Bind(&PowerManagerClientImpl::HandleObserverSuspendReadiness,
                      weak_ptr_factory_.GetWeakPtr(), pending_suspend_id_);
  }

 private:
  // Returns true if the current thread is the origin thread.
  bool OnOriginThread() {
    return base::PlatformThread::CurrentId() == origin_thread_id_;
  }

  // Called when a dbus signal is initially connected.
  void SignalConnected(const std::string& interface_name,
                       const std::string& signal_name,
                       bool success) {
    LOG_IF(WARNING, !success) << "Failed to connect to signal "
                              << signal_name << ".";
  }

  // Make a method call to power manager with no arguments and no response.
  void SimpleMethodCallToPowerManager(const std::string& method_name) {
    dbus::MethodCall method_call(power_manager::kPowerManagerInterface,
                                 method_name);
    power_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        dbus::ObjectProxy::EmptyResponseCallback());
  }

  void NameOwnerChangedReceived(dbus::Signal* signal) {
    VLOG(1) << "Power manager restarted";
    RegisterSuspendDelay();
    SetIsProjecting(last_is_projecting_);
    FOR_EACH_OBSERVER(Observer, observers_, PowerManagerRestarted());
  }

  void BrightnessChangedReceived(dbus::Signal* signal) {
    dbus::MessageReader reader(signal);
    int32 brightness_level = 0;
    bool user_initiated = 0;
    if (!(reader.PopInt32(&brightness_level) &&
          reader.PopBool(&user_initiated))) {
      LOG(ERROR) << "Brightness changed signal had incorrect parameters: "
                 << signal->ToString();
      return;
    }
    VLOG(1) << "Brightness changed to " << brightness_level
            << ": user initiated " << user_initiated;
    FOR_EACH_OBSERVER(Observer, observers_,
                      BrightnessChanged(brightness_level, user_initiated));
  }

  void PeripheralBatteryStatusReceived(dbus::Signal* signal) {
    dbus::MessageReader reader(signal);
    power_manager::PeripheralBatteryStatus protobuf_status;
    if (!reader.PopArrayOfBytesAsProto(&protobuf_status)) {
      LOG(ERROR) << "Unable to decode protocol buffer from "
                 << power_manager::kPeripheralBatteryStatusSignal << " signal";
      return;
    }

    std::string path = protobuf_status.path();
    std::string name = protobuf_status.name();
    int level = protobuf_status.has_level() ? protobuf_status.level() : -1;

    VLOG(1) << "Device battery status received " << level
            << " for " << name << " at " << path;

    FOR_EACH_OBSERVER(Observer, observers_,
                      PeripheralBatteryStatusReceived(path, name, level));
  }

  void PowerSupplyPollReceived(dbus::Signal* signal) {
    VLOG(1) << "Received power supply poll signal.";
    dbus::MessageReader reader(signal);
    power_manager::PowerSupplyProperties protobuf;
    if (reader.PopArrayOfBytesAsProto(&protobuf)) {
      FOR_EACH_OBSERVER(Observer, observers_, PowerChanged(protobuf));
    } else {
      LOG(ERROR) << "Unable to decode "
                 << power_manager::kPowerSupplyPollSignal << "signal";
    }
  }

  void OnGetPowerSupplyPropertiesMethod(dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Error calling "
                 << power_manager::kGetPowerSupplyPropertiesMethod;
      return;
    }

    dbus::MessageReader reader(response);
    power_manager::PowerSupplyProperties protobuf;
    if (reader.PopArrayOfBytesAsProto(&protobuf)) {
      FOR_EACH_OBSERVER(Observer, observers_, PowerChanged(protobuf));
    } else {
      LOG(ERROR) << "Unable to decode "
                 << power_manager::kGetPowerSupplyPropertiesMethod
                 << " response";
    }
  }

  void OnGetScreenBrightnessPercent(
      const GetScreenBrightnessPercentCallback& callback,
      dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Error calling "
                 << power_manager::kGetScreenBrightnessPercent;
      return;
    }
    dbus::MessageReader reader(response);
    double percent = 0.0;
    if (!reader.PopDouble(&percent))
      LOG(ERROR) << "Error reading response from powerd: "
                 << response->ToString();
    callback.Run(percent);
  }

  void OnRegisterSuspendDelayReply(dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Error calling "
                 << power_manager::kRegisterSuspendDelayMethod;
      return;
    }

    dbus::MessageReader reader(response);
    power_manager::RegisterSuspendDelayReply protobuf;
    if (!reader.PopArrayOfBytesAsProto(&protobuf)) {
      LOG(ERROR) << "Unable to parse reply from "
                 << power_manager::kRegisterSuspendDelayMethod;
      return;
    }

    suspend_delay_id_ = protobuf.delay_id();
    has_suspend_delay_id_ = true;
    VLOG(1) << "Registered suspend delay " << suspend_delay_id_;
  }

  void IdleNotifySignalReceived(dbus::Signal* signal) {
    dbus::MessageReader reader(signal);
    int64 threshold = 0;
    if (!reader.PopInt64(&threshold)) {
      LOG(ERROR) << "Idle Notify signal had incorrect parameters: "
                 << signal->ToString();
      return;
    }
    DCHECK_GT(threshold, 0);

    VLOG(1) << "Idle Notify: " << threshold;
    FOR_EACH_OBSERVER(Observer, observers_, IdleNotify(threshold));
  }

  void SuspendImminentReceived(dbus::Signal* signal) {
    if (!has_suspend_delay_id_) {
      LOG(ERROR) << "Received unrequested "
                 << power_manager::kSuspendImminentSignal << " signal";
      return;
    }

    dbus::MessageReader reader(signal);
    power_manager::SuspendImminent protobuf_imminent;
    if (!reader.PopArrayOfBytesAsProto(&protobuf_imminent)) {
      LOG(ERROR) << "Unable to decode protocol buffer from "
                 << power_manager::kSuspendImminentSignal << " signal";
      return;
    }

    if (suspend_is_pending_) {
      LOG(WARNING) << "Got " << power_manager::kSuspendImminentSignal
                   << " signal about pending suspend attempt "
                   << protobuf_imminent.suspend_id() << " while still waiting "
                   << "on attempt " << pending_suspend_id_;
    }

    pending_suspend_id_ = protobuf_imminent.suspend_id();
    suspend_is_pending_ = true;
    num_pending_suspend_readiness_callbacks_ = 0;
    FOR_EACH_OBSERVER(Observer, observers_, SuspendImminent());
    MaybeReportSuspendReadiness();
  }

  void IdleActionImminentReceived(dbus::Signal* signal) {
    FOR_EACH_OBSERVER(Observer, observers_, IdleActionImminent());
  }

  void IdleActionDeferredReceived(dbus::Signal* signal) {
    FOR_EACH_OBSERVER(Observer, observers_, IdleActionDeferred());
  }

  void InputEventReceived(dbus::Signal* signal) {
    dbus::MessageReader reader(signal);
    power_manager::InputEvent proto;
    if (!reader.PopArrayOfBytesAsProto(&proto)) {
      LOG(ERROR) << "Unable to decode protocol buffer from "
                 << power_manager::kInputEventSignal << " signal";
      return;
    }

    base::TimeTicks timestamp =
        base::TimeTicks::FromInternalValue(proto.timestamp());
    VLOG(1) << "Got " << power_manager::kInputEventSignal << " signal:"
            << " type=" << proto.type() << " timestamp=" << proto.timestamp();
    switch (proto.type()) {
      case power_manager::InputEvent_Type_POWER_BUTTON_DOWN:
      case power_manager::InputEvent_Type_POWER_BUTTON_UP: {
        bool down =
            (proto.type() == power_manager::InputEvent_Type_POWER_BUTTON_DOWN);
        FOR_EACH_OBSERVER(PowerManagerClient::Observer, observers_,
                          PowerButtonEventReceived(down, timestamp));
        break;
      }
      case power_manager::InputEvent_Type_LID_OPEN:
      case power_manager::InputEvent_Type_LID_CLOSED: {
        bool open =
            (proto.type() == power_manager::InputEvent_Type_LID_OPEN);
        FOR_EACH_OBSERVER(PowerManagerClient::Observer, observers_,
                          LidEventReceived(open, timestamp));
        break;
      }
    }
  }

  void SuspendStateChangedReceived(dbus::Signal* signal) {
    dbus::MessageReader reader(signal);
    power_manager::SuspendState proto;
    if (!reader.PopArrayOfBytesAsProto(&proto)) {
      LOG(ERROR) << "Unable to decode protocol buffer from "
                 << power_manager::kSuspendStateChangedSignal << " signal";
      return;
    }

    VLOG(1) << "Got " << power_manager::kSuspendStateChangedSignal << " signal:"
            << " type=" << proto.type() << " wall_time=" << proto.wall_time();
    base::Time wall_time =
        base::Time::FromInternalValue(proto.wall_time());
    switch (proto.type()) {
      case power_manager::SuspendState_Type_SUSPEND_TO_MEMORY:
        last_suspend_wall_time_ = wall_time;
        break;
      case power_manager::SuspendState_Type_RESUME:
        FOR_EACH_OBSERVER(
            PowerManagerClient::Observer, observers_,
            SystemResumed(wall_time - last_suspend_wall_time_));
        break;
    }
  }

  // Registers a suspend delay with the power manager.  This is usually
  // only called at startup, but if the power manager restarts, we need to
  // create a new delay.
  void RegisterSuspendDelay() {
    // Throw out any old delay that was registered.
    suspend_delay_id_ = -1;
    has_suspend_delay_id_ = false;

    dbus::MethodCall method_call(
        power_manager::kPowerManagerInterface,
        power_manager::kRegisterSuspendDelayMethod);
    dbus::MessageWriter writer(&method_call);

    power_manager::RegisterSuspendDelayRequest protobuf_request;
    base::TimeDelta timeout =
        base::TimeDelta::FromMilliseconds(kSuspendDelayTimeoutMs);
    protobuf_request.set_timeout(timeout.ToInternalValue());
    protobuf_request.set_description(kSuspendDelayDescription);

    if (!writer.AppendProtoAsArrayOfBytes(protobuf_request)) {
      LOG(ERROR) << "Error constructing message for "
                 << power_manager::kRegisterSuspendDelayMethod;
      return;
    }
    power_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(
            &PowerManagerClientImpl::OnRegisterSuspendDelayReply,
            weak_ptr_factory_.GetWeakPtr()));
  }

  // Records the fact that an observer has finished doing asynchronous work
  // that was blocking a pending suspend attempt and possibly reports
  // suspend readiness to powerd.  Called by callbacks returned via
  // GetSuspendReadinessCallback().
  void HandleObserverSuspendReadiness(int32 suspend_id) {
    DCHECK(OnOriginThread());
    if (!suspend_is_pending_ || suspend_id != pending_suspend_id_)
      return;

    num_pending_suspend_readiness_callbacks_--;
    MaybeReportSuspendReadiness();
  }

  // Reports suspend readiness to powerd if no observers are still holding
  // suspend readiness callbacks.
  void MaybeReportSuspendReadiness() {
    if (!suspend_is_pending_ || num_pending_suspend_readiness_callbacks_ > 0)
      return;

    dbus::MethodCall method_call(
        power_manager::kPowerManagerInterface,
        power_manager::kHandleSuspendReadinessMethod);
    dbus::MessageWriter writer(&method_call);

    power_manager::SuspendReadinessInfo protobuf_request;
    protobuf_request.set_delay_id(suspend_delay_id_);
    protobuf_request.set_suspend_id(pending_suspend_id_);

    pending_suspend_id_ = -1;
    suspend_is_pending_ = false;

    if (!writer.AppendProtoAsArrayOfBytes(protobuf_request)) {
      LOG(ERROR) << "Error constructing message for "
                 << power_manager::kHandleSuspendReadinessMethod;
      return;
    }
    power_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        dbus::ObjectProxy::EmptyResponseCallback());
  }

  // Origin thread (i.e. the UI thread in production).
  base::PlatformThreadId origin_thread_id_;

  dbus::ObjectProxy* power_manager_proxy_;
  ObserverList<Observer> observers_;

  // The delay_id_ obtained from the RegisterSuspendDelay request.
  int32 suspend_delay_id_;
  bool has_suspend_delay_id_;

  // powerd-supplied ID corresponding to an imminent suspend attempt that is
  // currently being delayed.
  int32 pending_suspend_id_;
  bool suspend_is_pending_;

  // Number of callbacks that have been returned by
  // GetSuspendReadinessCallback() during the currently-pending suspend
  // attempt but have not yet been called.
  int num_pending_suspend_readiness_callbacks_;

  // Wall time from the latest signal telling us that the system was about to
  // suspend to memory.
  base::Time last_suspend_wall_time_;

  // Last state passed to SetIsProjecting().
  bool last_is_projecting_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<PowerManagerClientImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(PowerManagerClientImpl);
};

// The PowerManagerClient implementation used on Linux desktop,
// which does nothing.
class PowerManagerClientStubImpl : public PowerManagerClient {
 public:
  PowerManagerClientStubImpl()
      : discharging_(true),
        battery_percentage_(40),
        brightness_(50.0),
        pause_count_(2),
        cycle_count_(0),
        weak_ptr_factory_(this) {
    if (CommandLine::ForCurrentProcess()->HasSwitch(
        chromeos::switches::kEnableStubInteractive)) {
      const int kStatusUpdateMs = 1000;
      update_timer_.Start(FROM_HERE,
          base::TimeDelta::FromMilliseconds(kStatusUpdateMs), this,
          &PowerManagerClientStubImpl::UpdateStatus);
    }
  }

  virtual ~PowerManagerClientStubImpl() {}

  // PowerManagerClient overrides:

  virtual void AddObserver(Observer* observer) OVERRIDE {
    observers_.AddObserver(observer);
  }

  virtual void RemoveObserver(Observer* observer) OVERRIDE {
    observers_.RemoveObserver(observer);
  }

  virtual bool HasObserver(Observer* observer) OVERRIDE {
    return observers_.HasObserver(observer);
  }

  virtual void DecreaseScreenBrightness(bool allow_off) OVERRIDE {
    VLOG(1) << "Requested to descrease screen brightness";
    SetBrightness(brightness_ - 5.0, true);
  }

  virtual void IncreaseScreenBrightness() OVERRIDE {
    VLOG(1) << "Requested to increase screen brightness";
    SetBrightness(brightness_ + 5.0, true);
  }

  virtual void SetScreenBrightnessPercent(double percent,
                                          bool gradual) OVERRIDE {
    VLOG(1) << "Requested to set screen brightness to " << percent << "% "
            << (gradual ? "gradually" : "instantaneously");
    SetBrightness(percent, false);
  }

  virtual void GetScreenBrightnessPercent(
      const GetScreenBrightnessPercentCallback& callback) OVERRIDE {
    callback.Run(brightness_);
  }

  virtual void DecreaseKeyboardBrightness() OVERRIDE {
    VLOG(1) << "Requested to descrease keyboard brightness";
  }

  virtual void IncreaseKeyboardBrightness() OVERRIDE {
    VLOG(1) << "Requested to increase keyboard brightness";
  }

  virtual void RequestStatusUpdate() OVERRIDE {
    base::MessageLoop::current()->PostTask(FROM_HERE,
        base::Bind(&PowerManagerClientStubImpl::UpdateStatus,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  virtual void RequestRestart() OVERRIDE {}
  virtual void RequestShutdown() OVERRIDE {}

  virtual void RequestIdleNotification(int64 threshold) OVERRIDE {
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&PowerManagerClientStubImpl::TriggerIdleNotify,
                   weak_ptr_factory_.GetWeakPtr(), threshold),
        base::TimeDelta::FromMilliseconds(threshold));
  }

  virtual void NotifyUserActivity(
      power_manager::UserActivityType type) OVERRIDE {}
  virtual void NotifyVideoActivity(bool is_fullscreen) OVERRIDE {}
  virtual void SetPolicy(
      const power_manager::PowerManagementPolicy& policy) OVERRIDE {}
  virtual void SetIsProjecting(bool is_projecting) OVERRIDE {}
  virtual base::Closure GetSuspendReadinessCallback() OVERRIDE {
    return base::Closure();
  }

 private:
  void UpdateStatus() {
    if (pause_count_ > 0) {
      pause_count_--;
      if (pause_count_ == 2)
        discharging_ = !discharging_;
    } else {
      if (discharging_)
        battery_percentage_ -= (battery_percentage_ <= 10 ? 1 : 10);
      else
        battery_percentage_ += (battery_percentage_ >= 10 ? 10 : 1);
      battery_percentage_ = std::min(std::max(battery_percentage_, 0), 100);
      // We pause at 0 and 100% so that it's easier to check those conditions.
      if (battery_percentage_ == 0 || battery_percentage_ == 100) {
        pause_count_ = 4;
        if (battery_percentage_ == 100)
          cycle_count_ = (cycle_count_ + 1) % 3;
      }
    }

    const int kSecondsToEmptyFullBattery = 3 * 60 * 60;  // 3 hours.
    int64 remaining_battery_time =
        std::max(1, battery_percentage_ * kSecondsToEmptyFullBattery / 100);

    props_.Clear();

    switch (cycle_count_) {
      case 0:
        // Say that the system is charging with AC connected and
        // discharging without any charger connected.
        props_.set_external_power(discharging_ ?
            power_manager::PowerSupplyProperties_ExternalPower_DISCONNECTED :
            power_manager::PowerSupplyProperties_ExternalPower_AC);
        break;
      case 1:
        // Say that the system is both charging and discharging on USB
        // (i.e. a low-power charger).
        props_.set_external_power(
            power_manager::PowerSupplyProperties_ExternalPower_USB);
        break;
      case 2:
        // Say that the system is both charging and discharging on AC.
        props_.set_external_power(
            power_manager::PowerSupplyProperties_ExternalPower_AC);
        break;
      default:
        NOTREACHED() << "Unhandled cycle " << cycle_count_;
    }

    if (battery_percentage_ == 100 && !discharging_) {
      props_.set_battery_state(
          power_manager::PowerSupplyProperties_BatteryState_FULL);
    } else if (!discharging_) {
      props_.set_battery_state(
          power_manager::PowerSupplyProperties_BatteryState_CHARGING);
      props_.set_battery_time_to_full_sec(std::max(static_cast<int64>(1),
          kSecondsToEmptyFullBattery - remaining_battery_time));
    } else {
      props_.set_battery_state(
          power_manager::PowerSupplyProperties_BatteryState_DISCHARGING);
      props_.set_battery_time_to_empty_sec(remaining_battery_time);
    }

    props_.set_battery_percent(battery_percentage_);
    props_.set_is_calculating_battery_time(pause_count_ > 1);

    FOR_EACH_OBSERVER(Observer, observers_, PowerChanged(props_));
  }

  void SetBrightness(double percent, bool user_initiated) {
    brightness_ = std::min(std::max(0.0, percent), 100.0);
    int brightness_level = static_cast<int>(brightness_);
    FOR_EACH_OBSERVER(Observer, observers_,
                      BrightnessChanged(brightness_level, user_initiated));
  }

  void TriggerIdleNotify(int64 threshold) {
    FOR_EACH_OBSERVER(Observer, observers_, IdleNotify(threshold));
  }

  bool discharging_;
  int battery_percentage_;
  double brightness_;
  int pause_count_;
  int cycle_count_;
  ObserverList<Observer> observers_;
  base::RepeatingTimer<PowerManagerClientStubImpl> update_timer_;
  power_manager::PowerSupplyProperties props_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<PowerManagerClientStubImpl> weak_ptr_factory_;
};

PowerManagerClient::PowerManagerClient() {
}

PowerManagerClient::~PowerManagerClient() {
}

// static
PowerManagerClient* PowerManagerClient::Create(
    DBusClientImplementationType type,
    dbus::Bus* bus) {
  if (type == REAL_DBUS_CLIENT_IMPLEMENTATION)
    return new PowerManagerClientImpl(bus);
  DCHECK_EQ(STUB_DBUS_CLIENT_IMPLEMENTATION, type);
  return new PowerManagerClientStubImpl();
}

}  // namespace chromeos
