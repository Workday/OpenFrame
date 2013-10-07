// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/net/network_portal_detector_impl.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chromeos/dbus/shill_service_client_stub.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "content/public/browser/notification_service.h"
#include "grit/generated_resources.h"
#include "net/http/http_status_code.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "ui/base/l10n/l10n_util.h"

using captive_portal::CaptivePortalDetector;

namespace chromeos {

namespace {

// Maximum number of portal detections for the same default network
// after network change.
const int kMaxRequestAttempts = 3;

// Minimum timeout between consecutive portal checks for the same
// network.
const int kMinTimeBetweenAttemptsSec = 3;

// Delay before portal detection caused by changes in proxy settings.
const int kProxyChangeDelaySec = 1;

// Delay between consecutive portal checks for a network in lazy mode.
const int kLazyCheckIntervalSec = 5;

std::string CaptivePortalStatusString(
    NetworkPortalDetectorImpl::CaptivePortalStatus status) {
  switch (status) {
    case NetworkPortalDetectorImpl::CAPTIVE_PORTAL_STATUS_UNKNOWN:
      return l10n_util::GetStringUTF8(
          IDS_CHROMEOS_CAPTIVE_PORTAL_STATUS_UNKNOWN);
    case NetworkPortalDetectorImpl::CAPTIVE_PORTAL_STATUS_OFFLINE:
      return l10n_util::GetStringUTF8(
          IDS_CHROMEOS_CAPTIVE_PORTAL_STATUS_OFFLINE);
    case NetworkPortalDetectorImpl::CAPTIVE_PORTAL_STATUS_ONLINE:
      return l10n_util::GetStringUTF8(
          IDS_CHROMEOS_CAPTIVE_PORTAL_STATUS_ONLINE);
    case NetworkPortalDetectorImpl::CAPTIVE_PORTAL_STATUS_PORTAL:
      return l10n_util::GetStringUTF8(
          IDS_CHROMEOS_CAPTIVE_PORTAL_STATUS_PORTAL);
    case NetworkPortalDetectorImpl::CAPTIVE_PORTAL_STATUS_PROXY_AUTH_REQUIRED:
      return l10n_util::GetStringUTF8(
          IDS_CHROMEOS_CAPTIVE_PORTAL_STATUS_PROXY_AUTH_REQUIRED);
    case NetworkPortalDetectorImpl::CAPTIVE_PORTAL_STATUS_COUNT:
      NOTREACHED();
  }
  return l10n_util::GetStringUTF8(
      IDS_CHROMEOS_CAPTIVE_PORTAL_STATUS_UNRECOGNIZED);
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// NetworkPortalDetectorImpl, public:

NetworkPortalDetectorImpl::NetworkPortalDetectorImpl(
    const scoped_refptr<net::URLRequestContextGetter>& request_context)
    : test_url_(CaptivePortalDetector::kDefaultURL),
      enabled_(false),
      weak_ptr_factory_(this),
      attempt_count_(0),
      lazy_detection_enabled_(false),
      lazy_check_interval_(base::TimeDelta::FromSeconds(kLazyCheckIntervalSec)),
      min_time_between_attempts_(
          base::TimeDelta::FromSeconds(kMinTimeBetweenAttemptsSec)),
      request_timeout_for_testing_initialized_(false) {
  captive_portal_detector_.reset(new CaptivePortalDetector(request_context));

  registrar_.Add(this,
                 chrome::NOTIFICATION_LOGIN_PROXY_CHANGED,
                 content::NotificationService::AllSources());
  registrar_.Add(this,
                 chrome::NOTIFICATION_AUTH_SUPPLIED,
                 content::NotificationService::AllSources());
  registrar_.Add(this,
                 chrome::NOTIFICATION_AUTH_CANCELLED,
                 content::NotificationService::AllSources());
}

NetworkPortalDetectorImpl::~NetworkPortalDetectorImpl() {
}

void NetworkPortalDetectorImpl::Init() {
  DCHECK(CalledOnValidThread());

  state_ = STATE_IDLE;
  NetworkHandler::Get()->network_state_handler()->AddObserver(
      this, FROM_HERE);
}

void NetworkPortalDetectorImpl::Shutdown() {
  DCHECK(CalledOnValidThread());

  detection_task_.Cancel();
  detection_timeout_.Cancel();

  captive_portal_detector_->Cancel();
  captive_portal_detector_.reset();
  observers_.Clear();
  if (NetworkHandler::IsInitialized()) {
    NetworkHandler::Get()->network_state_handler()->RemoveObserver(
        this, FROM_HERE);
  }
}

void NetworkPortalDetectorImpl::AddObserver(Observer* observer) {
  DCHECK(CalledOnValidThread());
  if (!observer || observers_.HasObserver(observer))
    return;
  observers_.AddObserver(observer);
}

void NetworkPortalDetectorImpl::AddAndFireObserver(Observer* observer) {
  DCHECK(CalledOnValidThread());
  if (!observer)
    return;
  AddObserver(observer);
  const NetworkState* network =
      NetworkHandler::Get()->network_state_handler()->DefaultNetwork();
  observer->OnPortalDetectionCompleted(network, GetCaptivePortalState(network));
}

void NetworkPortalDetectorImpl::RemoveObserver(Observer* observer) {
  DCHECK(CalledOnValidThread());
  if (observer)
    observers_.RemoveObserver(observer);
}

bool NetworkPortalDetectorImpl::IsEnabled() {
  return enabled_;
}

void NetworkPortalDetectorImpl::Enable(bool start_detection) {
  DCHECK(CalledOnValidThread());
  if (enabled_)
    return;
  enabled_ = true;
  DCHECK(!IsPortalCheckPending());
  DCHECK(!IsCheckingForPortal());
  DCHECK(!lazy_detection_enabled());
  if (!start_detection)
    return;
  state_ = STATE_IDLE;
  attempt_count_ = 0;
  const NetworkState* default_network =
      NetworkHandler::Get()->network_state_handler()->DefaultNetwork();
  if (!default_network)
    return;
  portal_state_map_.erase(default_network->path());
  DCHECK(CanPerformDetection());
  DetectCaptivePortal(base::TimeDelta());
}

NetworkPortalDetectorImpl::CaptivePortalState
NetworkPortalDetectorImpl::GetCaptivePortalState(const NetworkState* network) {
  DCHECK(CalledOnValidThread());
  if (!network)
    return CaptivePortalState();
  CaptivePortalStateMap::const_iterator it =
      portal_state_map_.find(network->path());
  if (it == portal_state_map_.end())
    return CaptivePortalState();
  return it->second;
}

bool NetworkPortalDetectorImpl::StartDetectionIfIdle() {
  if (IsPortalCheckPending() || IsCheckingForPortal())
    return false;
  if (!CanPerformDetection())
    attempt_count_ = 0;
  DCHECK(CanPerformDetection());
  DetectCaptivePortal(base::TimeDelta());
  return true;
}

void NetworkPortalDetectorImpl::EnableLazyDetection() {
  if (lazy_detection_enabled())
    return;
  lazy_detection_enabled_ = true;
  VLOG(1) << "Lazy detection mode enabled.";
  StartDetectionIfIdle();
}

void NetworkPortalDetectorImpl::DisableLazyDetection() {
  if (!lazy_detection_enabled())
    return;
  lazy_detection_enabled_ = false;
  if (attempt_count_ == kMaxRequestAttempts && IsPortalCheckPending())
    CancelPortalDetection();
  VLOG(1) << "Lazy detection mode disabled.";
}

void NetworkPortalDetectorImpl::NetworkManagerChanged() {
  DCHECK(CalledOnValidThread());
  const NetworkState* default_network =
      NetworkHandler::Get()->network_state_handler()->DefaultNetwork();
  if (!default_network) {
    default_network_id_.clear();
    return;
  }

  default_network_id_ = default_network->guid();

  bool network_changed = (default_service_path_ != default_network->path());
  default_service_path_ = default_network->path();

  bool connection_state_changed = (default_connection_state_ !=
                                   default_network->connection_state());
  default_connection_state_ = default_network->connection_state();

  if (network_changed || connection_state_changed) {
    attempt_count_ = 0;
    CancelPortalDetection();
  }

  if (CanPerformDetection() &&
      NetworkState::StateIsConnected(default_connection_state_)) {
    // Initiate Captive Portal detection if network's captive
    // portal state is unknown (e.g. for freshly created networks),
    // offline or if network connection state was changed.
    CaptivePortalState state = GetCaptivePortalState(default_network);
    if (state.status == CAPTIVE_PORTAL_STATUS_UNKNOWN ||
        state.status == CAPTIVE_PORTAL_STATUS_OFFLINE ||
        (!network_changed && connection_state_changed)) {
      DetectCaptivePortal(base::TimeDelta());
    }
  }
}

void NetworkPortalDetectorImpl::DefaultNetworkChanged(
    const NetworkState* network) {
  NetworkManagerChanged();
}

////////////////////////////////////////////////////////////////////////////////
// NetworkPortalDetectorImpl, private:

bool NetworkPortalDetectorImpl::CanPerformDetection() const {
  if (IsPortalCheckPending() || IsCheckingForPortal())
    return false;
  return attempt_count_ < kMaxRequestAttempts || lazy_detection_enabled();
}

void NetworkPortalDetectorImpl::DetectCaptivePortal(
    const base::TimeDelta& delay) {
  DCHECK(CanPerformDetection());

  if (!IsEnabled())
    return;

  detection_task_.Cancel();
  detection_timeout_.Cancel();
  state_ = STATE_PORTAL_CHECK_PENDING;

  next_attempt_delay_ = delay;
  if (attempt_count_ > 0) {
    base::TimeTicks now = GetCurrentTimeTicks();
    base::TimeDelta elapsed_time;

    base::TimeDelta delay_between_attempts = min_time_between_attempts_;
    if (attempt_count_ == kMaxRequestAttempts) {
      DCHECK(lazy_detection_enabled());
      delay_between_attempts = lazy_check_interval_;
    }
    if (now > attempt_start_time_)
      elapsed_time = now - attempt_start_time_;
    if (elapsed_time < delay_between_attempts &&
        delay_between_attempts - elapsed_time > next_attempt_delay_) {
      next_attempt_delay_ = delay_between_attempts - elapsed_time;
    }
  } else {
    detection_start_time_ = GetCurrentTimeTicks();
  }
  detection_task_.Reset(
      base::Bind(&NetworkPortalDetectorImpl::DetectCaptivePortalTask,
                 weak_ptr_factory_.GetWeakPtr()));
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE, detection_task_.callback(), next_attempt_delay_);
}

void NetworkPortalDetectorImpl::DetectCaptivePortalTask() {
  DCHECK(IsPortalCheckPending());

  state_ = STATE_CHECKING_FOR_PORTAL;
  attempt_start_time_ = GetCurrentTimeTicks();

  if (attempt_count_ < kMaxRequestAttempts) {
    ++attempt_count_;
    VLOG(1) << "Portal detection started: "
            << "network=" << default_network_id_ << ", "
            << "attempt=" << attempt_count_ << " of " << kMaxRequestAttempts;
  } else {
    DCHECK(lazy_detection_enabled());
    VLOG(1) << "Lazy portal detection attempt started";
  }

  captive_portal_detector_->DetectCaptivePortal(
      test_url_,
      base::Bind(&NetworkPortalDetectorImpl::OnPortalDetectionCompleted,
                 weak_ptr_factory_.GetWeakPtr()));
  detection_timeout_.Reset(
      base::Bind(&NetworkPortalDetectorImpl::PortalDetectionTimeout,
                 weak_ptr_factory_.GetWeakPtr()));
  base::TimeDelta request_timeout;

  // For easier unit testing check for testing state is performed here
  // and not in the GetRequestTimeoutSec().
  if (request_timeout_for_testing_initialized_)
    request_timeout = request_timeout_for_testing_;
  else
    request_timeout = base::TimeDelta::FromSeconds(GetRequestTimeoutSec());
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE, detection_timeout_.callback(), request_timeout);
}

void NetworkPortalDetectorImpl::PortalDetectionTimeout() {
  DCHECK(CalledOnValidThread());
  DCHECK(IsCheckingForPortal());

  VLOG(1) << "Portal detection timeout: network=" << default_network_id_;

  captive_portal_detector_->Cancel();
  CaptivePortalDetector::Results results;
  results.result = captive_portal::RESULT_NO_RESPONSE;
  OnPortalDetectionCompleted(results);
}

void NetworkPortalDetectorImpl::CancelPortalDetection() {
  if (IsPortalCheckPending())
    detection_task_.Cancel();
  else if (IsCheckingForPortal())
    captive_portal_detector_->Cancel();
  detection_timeout_.Cancel();
  state_ = STATE_IDLE;
}

void NetworkPortalDetectorImpl::OnPortalDetectionCompleted(
    const CaptivePortalDetector::Results& results) {
  captive_portal::Result result = results.result;
  int response_code = results.response_code;

  if (ShillServiceClientStub::IsStubPortalledWifiEnabled(
          default_service_path_)) {
    result = captive_portal::RESULT_BEHIND_CAPTIVE_PORTAL;
    response_code = 200;
  }

  DCHECK(CalledOnValidThread());
  DCHECK(IsCheckingForPortal());

  VLOG(1) << "Portal detection completed: "
          << "network=" << default_network_id_ << ", "
          << "result=" << CaptivePortalDetector::CaptivePortalResultToString(
              results.result) << ", "
          << "response_code=" << results.response_code;

  state_ = STATE_IDLE;
  detection_timeout_.Cancel();

  const NetworkState* default_network =
      NetworkHandler::Get()->network_state_handler()->DefaultNetwork();

  CaptivePortalState state;
  state.response_code = response_code;
  switch (result) {
    case captive_portal::RESULT_NO_RESPONSE:
      if (attempt_count_ >= kMaxRequestAttempts) {
        if (state.response_code == net::HTTP_PROXY_AUTHENTICATION_REQUIRED) {
          state.status = CAPTIVE_PORTAL_STATUS_PROXY_AUTH_REQUIRED;
        } else if (default_network && (default_network->connection_state() ==
                                       flimflam::kStatePortal)) {
          // Take into account shill's detection results.
          state.status = CAPTIVE_PORTAL_STATUS_PORTAL;
          LOG(WARNING) << "Network " << default_network->guid() << " "
                       << "is marked as "
                       << CaptivePortalStatusString(state.status) << " "
                       << "despite the fact that CaptivePortalDetector "
                       << "received no response";
        } else {
          state.status = CAPTIVE_PORTAL_STATUS_OFFLINE;
        }
        SetCaptivePortalState(default_network, state);
      } else {
        DCHECK(CanPerformDetection());
        DetectCaptivePortal(results.retry_after_delta);
      }
      break;
    case captive_portal::RESULT_INTERNET_CONNECTED:
      state.status = CAPTIVE_PORTAL_STATUS_ONLINE;
      SetCaptivePortalState(default_network, state);
      break;
    case captive_portal::RESULT_BEHIND_CAPTIVE_PORTAL:
      state.status = CAPTIVE_PORTAL_STATUS_PORTAL;
      SetCaptivePortalState(default_network, state);
      break;
    default:
      break;
  }

  TryLazyDetection();
}

void NetworkPortalDetectorImpl::TryLazyDetection() {
  if (lazy_detection_enabled() && CanPerformDetection())
    DetectCaptivePortal(base::TimeDelta());
}

void NetworkPortalDetectorImpl::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_LOGIN_PROXY_CHANGED ||
      type == chrome::NOTIFICATION_AUTH_SUPPLIED ||
      type == chrome::NOTIFICATION_AUTH_CANCELLED) {
    VLOG(1) << "Restarting portal detection due to proxy change.";
    attempt_count_ = 0;
    if (IsPortalCheckPending())
      return;
    CancelPortalDetection();
    DCHECK(CanPerformDetection());
    DetectCaptivePortal(base::TimeDelta::FromSeconds(kProxyChangeDelaySec));
  }
}

bool NetworkPortalDetectorImpl::IsPortalCheckPending() const {
  return state_ == STATE_PORTAL_CHECK_PENDING;
}

bool NetworkPortalDetectorImpl::IsCheckingForPortal() const {
  return state_ == STATE_CHECKING_FOR_PORTAL;
}

void NetworkPortalDetectorImpl::SetCaptivePortalState(
    const NetworkState* network,
    const CaptivePortalState& state) {
  if (!detection_start_time_.is_null()) {
    UMA_HISTOGRAM_TIMES("CaptivePortal.OOBE.DetectionDuration",
                        GetCurrentTimeTicks() - detection_start_time_);
  }

  if (!network) {
    NotifyPortalDetectionCompleted(network, state);
    return;
  }

  CaptivePortalStateMap::const_iterator it =
      portal_state_map_.find(network->path());
  if (it == portal_state_map_.end() ||
      it->second.status != state.status ||
      it->second.response_code != state.response_code) {
    VLOG(1) << "Updating Chrome Captive Portal state: "
            << "network=" << network->guid() << ", "
            << "status=" << CaptivePortalStatusString(state.status) << ", "
            << "response_code=" << state.response_code;
    portal_state_map_[network->path()] = state;
  }
  NotifyPortalDetectionCompleted(network, state);
}

void NetworkPortalDetectorImpl::NotifyPortalDetectionCompleted(
    const NetworkState* network,
    const CaptivePortalState& state) {
  FOR_EACH_OBSERVER(Observer, observers_,
                    OnPortalDetectionCompleted(network, state));
}

base::TimeTicks NetworkPortalDetectorImpl::GetCurrentTimeTicks() const {
  if (time_ticks_for_testing_.is_null())
    return base::TimeTicks::Now();
  return time_ticks_for_testing_;
}

bool NetworkPortalDetectorImpl::DetectionTimeoutIsCancelledForTesting() const {
  return detection_timeout_.IsCancelled();
}

int NetworkPortalDetectorImpl::GetRequestTimeoutSec() const {
  DCHECK_LE(0, attempt_count_);
  const NetworkState* network =
      NetworkHandler::Get()->network_state_handler()->DefaultNetwork();
  if (!network)
    return kBaseRequestTimeoutSec;
  if (lazy_detection_enabled_)
    return kLazyRequestTimeoutSec;
  return attempt_count_ * kBaseRequestTimeoutSec;
}

}  // namespace chromeos
