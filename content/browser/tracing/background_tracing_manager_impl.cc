// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/tracing/background_tracing_manager_impl.h"

#include "base/command_line.h"
#include "base/json/json_writer.h"
#include "base/macros.h"
#include "base/metrics/histogram_macros.h"
#include "base/rand_util.h"
#include "base/time/time.h"
#include "content/browser/tracing/background_tracing_rule.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/tracing_delegate.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"

namespace content {

namespace {

base::LazyInstance<BackgroundTracingManagerImpl>::Leaky g_controller =
    LAZY_INSTANCE_INITIALIZER;

// These values are used for a histogram. Do not reorder.
enum BackgroundTracingMetrics {
  SCENARIO_ACTIVATION_REQUESTED = 0,
  SCENARIO_ACTIVATED_SUCCESSFULLY = 1,
  RECORDING_ENABLED = 2,
  PREEMPTIVE_TRIGGERED = 3,
  REACTIVE_TRIGGERED = 4,
  FINALIZATION_ALLOWED = 5,
  FINALIZATION_DISALLOWED = 6,
  FINALIZATION_STARTED = 7,
  FINALIZATION_COMPLETE = 8,
  SCENARIO_ACTION_FAILED_LOWRES_CLOCK = 9,
  NUMBER_OF_BACKGROUND_TRACING_METRICS,
};

void RecordBackgroundTracingMetric(BackgroundTracingMetrics metric) {
  UMA_HISTOGRAM_ENUMERATION("Tracing.Background.ScenarioState", metric,
                            NUMBER_OF_BACKGROUND_TRACING_METRICS);
}

}  // namespace

BackgroundTracingManagerImpl::TracingTimer::TracingTimer(
    StartedFinalizingCallback callback) : callback_(callback) {
}

BackgroundTracingManagerImpl::TracingTimer::~TracingTimer() {
}

void BackgroundTracingManagerImpl::TracingTimer::StartTimer(int seconds) {
  tracing_timer_.Start(
      FROM_HERE, base::TimeDelta::FromSeconds(seconds), this,
      &BackgroundTracingManagerImpl::TracingTimer::TracingTimerFired);
}

void BackgroundTracingManagerImpl::TracingTimer::CancelTimer() {
  tracing_timer_.Stop();
}

void BackgroundTracingManagerImpl::TracingTimer::TracingTimerFired() {
  BackgroundTracingManagerImpl::GetInstance()->BeginFinalizing(callback_);
}

void BackgroundTracingManagerImpl::TracingTimer::FireTimerForTesting() {
  CancelTimer();
  TracingTimerFired();
}

BackgroundTracingManager* BackgroundTracingManager::GetInstance() {
  return BackgroundTracingManagerImpl::GetInstance();
}

BackgroundTracingManagerImpl* BackgroundTracingManagerImpl::GetInstance() {
  return g_controller.Pointer();
}

BackgroundTracingManagerImpl::BackgroundTracingManagerImpl()
    : delegate_(GetContentClient()->browser()->GetTracingDelegate()),
      is_gathering_(false),
      is_tracing_(false),
      requires_anonymized_data_(true),
      trigger_handle_ids_(0),
      triggered_named_event_handle_(-1) {}

BackgroundTracingManagerImpl::~BackgroundTracingManagerImpl() {
  NOTREACHED();
}

void BackgroundTracingManagerImpl::WhenIdle(
    base::Callback<void()> idle_callback) {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  idle_callback_ = idle_callback;
}

bool BackgroundTracingManagerImpl::SetActiveScenario(
    scoped_ptr<BackgroundTracingConfig> config,
    const BackgroundTracingManager::ReceiveCallback& receive_callback,
    DataFiltering data_filtering) {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  RecordBackgroundTracingMetric(SCENARIO_ACTIVATION_REQUESTED);

  if (is_tracing_)
    return false;

  // If we don't have a high resolution timer available, traces will be
  // too inaccurate to be useful.
  if (!base::TimeTicks::IsHighResolution()) {
    RecordBackgroundTracingMetric(SCENARIO_ACTION_FAILED_LOWRES_CLOCK);
    return false;
  }

  bool requires_anonymized_data = (data_filtering == ANONYMIZE_DATA);

  // If the I/O thread isn't running, this is a startup scenario and
  // we have to wait until initialization is finished to validate that the
  // scenario can run.
  if (BrowserThread::IsThreadInitialized(BrowserThread::IO)) {
    // TODO(oysteine): Retry when time_until_allowed has elapsed.
    if (config && delegate_ &&
        !delegate_->IsAllowedToBeginBackgroundScenario(
            *config.get(), requires_anonymized_data)) {
      return false;
    }
  } else {
    base::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&BackgroundTracingManagerImpl::ValidateStartupScenario,
                   base::Unretained(this)));
  }

  scoped_ptr<const content::BackgroundTracingConfigImpl> config_impl(
      static_cast<BackgroundTracingConfigImpl*>(config.release()));

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

  if (config_impl) {
    // No point in tracing if there's nowhere to send it.
    if (receive_callback.is_null())
      return false;

    // If the scenario requires us to toggle Blink features, we want
    // to neither override anything else nor to do we want to activate
    // the scenario without doing the toggle, so if something else has
    // configured these switches we just abort.
    if (!config_impl->enable_blink_features().empty() &&
        command_line->HasSwitch(switches::kEnableBlinkFeatures)) {
      return false;
    }

    if (!config_impl->disable_blink_features().empty() &&
        command_line->HasSwitch(switches::kDisableBlinkFeatures)) {
      return false;
    }
  }

  config_ = config_impl.Pass();
  receive_callback_ = receive_callback;
  requires_anonymized_data_ = requires_anonymized_data;

  if (config_) {
    DCHECK(!config_.get()->rules().empty());
    for (auto& rule : config_.get()->rules())
      static_cast<BackgroundTracingRule*>(rule)->Install();

    if (!config_->enable_blink_features().empty()) {
      command_line->AppendSwitchASCII(switches::kEnableBlinkFeatures,
                                      config_->enable_blink_features());
    }

    if (!config_->disable_blink_features().empty()) {
      command_line->AppendSwitchASCII(switches::kDisableBlinkFeatures,
                                      config_->disable_blink_features());
    }
  }

  StartTracingIfConfigNeedsIt();

  RecordBackgroundTracingMetric(SCENARIO_ACTIVATED_SUCCESSFULLY);
  return true;
}

bool BackgroundTracingManagerImpl::HasActiveScenario() {
  return config_;
}

bool BackgroundTracingManagerImpl::IsTracingForTesting() {
  return is_tracing_;
}

void BackgroundTracingManagerImpl::ValidateStartupScenario() {
  if (!config_ || !delegate_)
    return;

  if (!delegate_->IsAllowedToBeginBackgroundScenario(
          *config_.get(), requires_anonymized_data_)) {
    AbortScenario();
  }
}

void BackgroundTracingManagerImpl::StartTracingIfConfigNeedsIt() {
  if (!config_)
    return;

  if (config_->tracing_mode() == BackgroundTracingConfigImpl::PREEMPTIVE) {
    StartTracing(
        GetCategoryFilterStringForCategoryPreset(config_->category_preset()),
        base::trace_event::RECORD_CONTINUOUSLY);
  }
  // There is nothing to do in case of reactive tracing.
}

BackgroundTracingRule*
BackgroundTracingManagerImpl::GetRuleAbleToTriggerTracing(
    TriggerHandle handle) const {
  if (!config_)
    return nullptr;

  // If the last trace is still uploading, we don't allow a new one to trigger.
  if (is_gathering_)
    return nullptr;

  if (!IsTriggerHandleValid(handle)) {
    return nullptr;
  }

  std::string trigger_name = GetTriggerNameFromHandle(handle);
  for (const auto& rule : config_.get()->rules()) {
    if (static_cast<BackgroundTracingRule*>(rule)
            ->ShouldTriggerNamedEvent(trigger_name))
      return static_cast<BackgroundTracingRule*>(rule);
  }

  return nullptr;
}

void BackgroundTracingManagerImpl::OnHistogramTrigger(
    const std::string& histogram_name) {
  if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    content::BrowserThread::PostTask(
        content::BrowserThread::UI, FROM_HERE,
        base::Bind(&BackgroundTracingManagerImpl::OnHistogramTrigger,
                   base::Unretained(this), histogram_name));
    return;
  }

  for (const auto& rule : config_->rules()) {
    if (rule->ShouldTriggerNamedEvent(histogram_name))
      OnRuleTriggered(rule, StartedFinalizingCallback());
  }
}

void BackgroundTracingManagerImpl::TriggerNamedEvent(
    BackgroundTracingManagerImpl::TriggerHandle handle,
    StartedFinalizingCallback callback) {
  if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    content::BrowserThread::PostTask(
        content::BrowserThread::UI, FROM_HERE,
        base::Bind(&BackgroundTracingManagerImpl::TriggerNamedEvent,
                   base::Unretained(this), handle, callback));
    return;
  }

  bool is_valid_trigger = true;

  const BackgroundTracingRule* triggered_rule =
      GetRuleAbleToTriggerTracing(handle);
  if (!triggered_rule)
    is_valid_trigger = false;

  // A different reactive config than the running one tried to trigger.
  if (!config_ ||
      (config_->tracing_mode() == BackgroundTracingConfigImpl::REACTIVE &&
       is_tracing_ && triggered_named_event_handle_ != handle)) {
    is_valid_trigger = false;
  }

  if (!is_valid_trigger) {
    if (!callback.is_null())
      callback.Run(false);
    return;
  }

  triggered_named_event_handle_ = handle;
  OnRuleTriggered(triggered_rule, callback);
}

void BackgroundTracingManagerImpl::OnRuleTriggered(
    const BackgroundTracingRule* triggered_rule,
    StartedFinalizingCallback callback) {
  CHECK(config_);

  double trigger_chance = triggered_rule->trigger_chance();
  if (trigger_chance < 1.0 && base::RandDouble() > trigger_chance) {
    if (!callback.is_null())
      callback.Run(false);
    return;
  }

  int trace_timeout = triggered_rule->GetTraceTimeout();

  if (config_->tracing_mode() == BackgroundTracingConfigImpl::REACTIVE) {
    // In reactive mode, a trigger starts tracing, or finalizes tracing
    // immediately if it's already running.
    RecordBackgroundTracingMetric(REACTIVE_TRIGGERED);

    if (!is_tracing_) {
      // It was not already tracing, start a new trace.
      StartTracing(GetCategoryFilterStringForCategoryPreset(
                          triggered_rule->GetCategoryPreset()),
                      base::trace_event::RECORD_UNTIL_FULL);
    } else {
      // Reactive configs that trigger again while tracing should just
      // end right away (to not capture multiple navigations, for example).
      trace_timeout = -1;
    }
  } else {
    // In preemptive mode, a trigger starts finalizing a trace if one is
    // running and we're not got a finalization timer running,
    // otherwise we do nothing.
    if (!is_tracing_ || is_gathering_ || tracing_timer_) {
      if (!callback.is_null())
        callback.Run(false);
      return;
    }

    RecordBackgroundTracingMetric(PREEMPTIVE_TRIGGERED);
  }

  if (trace_timeout < 0) {
    BeginFinalizing(callback);
  } else {
    tracing_timer_.reset(new TracingTimer(callback));
    tracing_timer_->StartTimer(trace_timeout);
  }

  if (!rule_triggered_callback_for_testing_.is_null())
    rule_triggered_callback_for_testing_.Run();
}

BackgroundTracingManagerImpl::TriggerHandle
BackgroundTracingManagerImpl::RegisterTriggerType(const char* trigger_name) {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  trigger_handle_ids_ += 1;

  trigger_handles_.insert(
      std::pair<TriggerHandle, std::string>(trigger_handle_ids_, trigger_name));

  return static_cast<TriggerHandle>(trigger_handle_ids_);
}

bool BackgroundTracingManagerImpl::IsTriggerHandleValid(
    BackgroundTracingManager::TriggerHandle handle) const {
  return trigger_handles_.find(handle) != trigger_handles_.end();
}

std::string BackgroundTracingManagerImpl::GetTriggerNameFromHandle(
    BackgroundTracingManager::TriggerHandle handle) const {
  CHECK(IsTriggerHandleValid(handle));
  return trigger_handles_.find(handle)->second;
}

void BackgroundTracingManagerImpl::InvalidateTriggerHandlesForTesting() {
  trigger_handles_.clear();
}

void BackgroundTracingManagerImpl::SetTracingEnabledCallbackForTesting(
    const base::Closure& callback) {
  tracing_enabled_callback_for_testing_ = callback;
};

void BackgroundTracingManagerImpl::SetRuleTriggeredCallbackForTesting(
    const base::Closure& callback) {
  rule_triggered_callback_for_testing_ = callback;
};

void BackgroundTracingManagerImpl::FireTimerForTesting() {
  DCHECK(tracing_timer_);
  tracing_timer_->FireTimerForTesting();
}

void BackgroundTracingManagerImpl::StartTracing(
    std::string category_filter_str,
    base::trace_event::TraceRecordMode record_mode) {
  base::trace_event::TraceConfig trace_config(category_filter_str, record_mode);
  if (requires_anonymized_data_)
    trace_config.EnableArgumentFilter();

  is_tracing_ = TracingController::GetInstance()->StartTracing(
      trace_config, tracing_enabled_callback_for_testing_);
  RecordBackgroundTracingMetric(RECORDING_ENABLED);
}

void BackgroundTracingManagerImpl::OnFinalizeStarted(
    scoped_ptr<const base::DictionaryValue> metadata,
    base::RefCountedString* file_contents) {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  RecordBackgroundTracingMetric(FINALIZATION_STARTED);
  UMA_HISTOGRAM_MEMORY_KB("Tracing.Background.FinalizingTraceSizeInKB",
                          file_contents->size() / 1024);

  if (!receive_callback_.is_null()) {
    receive_callback_.Run(
        file_contents, metadata.Pass(),
        base::Bind(&BackgroundTracingManagerImpl::OnFinalizeComplete,
                   base::Unretained(this)));
  }
}

void BackgroundTracingManagerImpl::OnFinalizeComplete() {
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&BackgroundTracingManagerImpl::OnFinalizeComplete,
                   base::Unretained(this)));
    return;
  }

  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  is_gathering_ = false;

  if (!idle_callback_.is_null())
    idle_callback_.Run();

  // Now that a trace has completed, we may need to enable recording again.
  // TODO(oysteine): Retry later if IsAllowedToBeginBackgroundScenario fails.
  if (!delegate_ ||
      delegate_->IsAllowedToBeginBackgroundScenario(
          *config_.get(), requires_anonymized_data_)) {
    StartTracingIfConfigNeedsIt();
  } else {
    AbortScenario();
  }

  RecordBackgroundTracingMetric(FINALIZATION_COMPLETE);
}

void BackgroundTracingManagerImpl::AddCustomMetadata(
    TracingControllerImpl::TraceDataSink* trace_data_sink) const {
  base::DictionaryValue metadata_dict;

  scoped_ptr<base::DictionaryValue> config_dict(new base::DictionaryValue());
  config_->IntoDict(config_dict.get());
  metadata_dict.Set("config", config_dict.Pass());

  trace_data_sink->AddMetadata(metadata_dict);
}

void BackgroundTracingManagerImpl::BeginFinalizing(
    StartedFinalizingCallback callback) {
  is_gathering_ = true;
  is_tracing_ = false;
  triggered_named_event_handle_ = -1;
  tracing_timer_.reset();

  bool is_allowed_finalization =
      !delegate_ || (config_ &&
                     delegate_->IsAllowedToEndBackgroundScenario(
                         *config_.get(), requires_anonymized_data_));

  scoped_refptr<TracingControllerImpl::TraceDataSink> trace_data_sink;
  if (is_allowed_finalization) {
    trace_data_sink = content::TracingController::CreateCompressedStringSink(
        content::TracingController::CreateCallbackEndpoint(
            base::Bind(&BackgroundTracingManagerImpl::OnFinalizeStarted,
                       base::Unretained(this))));
    RecordBackgroundTracingMetric(FINALIZATION_ALLOWED);
    AddCustomMetadata(trace_data_sink.get());
  } else {
    RecordBackgroundTracingMetric(FINALIZATION_DISALLOWED);
  }

  content::TracingController::GetInstance()->StopTracing(trace_data_sink);

  if (!callback.is_null())
    callback.Run(is_allowed_finalization);
}

void BackgroundTracingManagerImpl::AbortScenario() {
  is_tracing_ = false;
  triggered_named_event_handle_ = -1;
  config_.reset();
  tracing_timer_.reset();

  content::TracingController::GetInstance()->StopTracing(nullptr);
}

std::string
BackgroundTracingManagerImpl::GetCategoryFilterStringForCategoryPreset(
    BackgroundTracingConfigImpl::CategoryPreset preset) const {
  switch (preset) {
    case BackgroundTracingConfigImpl::CategoryPreset::BENCHMARK:
      return "benchmark,toplevel";
    case BackgroundTracingConfigImpl::CategoryPreset::BENCHMARK_DEEP:
      return "*,disabled-by-default-benchmark.detailed,"
             "disabled-by-default-v8.cpu_profile";
    case BackgroundTracingConfigImpl::CategoryPreset::BENCHMARK_GPU:
      return "benchmark,toplevel,gpu";
    case BackgroundTracingConfigImpl::CategoryPreset::BENCHMARK_IPC:
      return "benchmark,toplevel,ipc";
    case BackgroundTracingConfigImpl::CategoryPreset::BENCHMARK_STARTUP:
      return "benchmark,toplevel,startup,disabled-by-default-file,"
             "disabled-by-default-toplevel.flow,"
             "disabled-by-default-ipc.flow";
    case BackgroundTracingConfigImpl::CategoryPreset::BLINK_STYLE:
      return "blink_style";
  }
  NOTREACHED();
  return "";
}

}  // namspace content
