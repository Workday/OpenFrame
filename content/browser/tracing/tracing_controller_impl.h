// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_TRACING_TRACING_CONTROLLER_IMPL_H_
#define CONTENT_BROWSER_TRACING_TRACING_CONTROLLER_IMPL_H_

#include <set>
#include <string>
#include <vector>

#include "base/lazy_instance.h"
#include "base/trace_event/memory_dump_manager.h"
#include "content/public/browser/tracing_controller.h"

namespace base {
class RefCountedString;
class RefCountedMemory;
}

namespace content {

class TraceMessageFilter;
class TracingUI;

class TracingControllerImpl
    : public TracingController,
      public base::trace_event::MemoryDumpManagerDelegate {
 public:
  static TracingControllerImpl* GetInstance();

  // TracingController implementation.
  bool GetCategories(const GetCategoriesDoneCallback& callback) override;
  bool StartTracing(const base::trace_event::TraceConfig& trace_config,
                    const StartTracingDoneCallback& callback) override;
  bool StopTracing(const scoped_refptr<TraceDataSink>& sink) override;
  bool StartMonitoring(
      const base::trace_event::TraceConfig& trace_config,
      const StartMonitoringDoneCallback& callback) override;
  bool StopMonitoring(
      const StopMonitoringDoneCallback& callback) override;
  void GetMonitoringStatus(
      bool* out_enabled,
      base::trace_event::TraceConfig* out_trace_config) override;
  bool CaptureMonitoringSnapshot(
      const scoped_refptr<TraceDataSink>& sink) override;
  bool GetTraceBufferUsage(
      const GetTraceBufferUsageCallback& callback) override;
  bool SetWatchEvent(const std::string& category_name,
                     const std::string& event_name,
                     const WatchEventCallback& callback) override;
  bool CancelWatchEvent() override;
  bool IsTracing() const override;

  void RegisterTracingUI(TracingUI* tracing_ui);
  void UnregisterTracingUI(TracingUI* tracing_ui);

  // base::trace_event::MemoryDumpManagerDelegate implementation.
  void RequestGlobalMemoryDump(
      const base::trace_event::MemoryDumpRequestArgs& args,
      const base::trace_event::MemoryDumpCallback& callback) override;
  uint64 GetTracingProcessId() const override;

  class TraceMessageFilterObserver {
   public:
    virtual void OnTraceMessageFilterAdded(TraceMessageFilter* filter) = 0;
    virtual void OnTraceMessageFilterRemoved(TraceMessageFilter* filter) = 0;
  };
  void AddTraceMessageFilterObserver(TraceMessageFilterObserver* observer);
  void RemoveTraceMessageFilterObserver(TraceMessageFilterObserver* observer);

 private:
  friend struct base::DefaultLazyInstanceTraits<TracingControllerImpl>;
  friend class TraceMessageFilter;

  TracingControllerImpl();
  ~TracingControllerImpl() override;

  bool can_start_tracing() const {
    return !is_tracing_;
  }

  bool can_stop_tracing() const {
    return is_tracing_ && !trace_data_sink_.get();
  }

  bool can_start_monitoring() const {
    return !is_monitoring_;
  }

  bool can_stop_monitoring() const {
    return is_monitoring_ && !monitoring_data_sink_.get();
  }

  bool can_get_trace_buffer_usage() const {
    return pending_trace_buffer_usage_callback_.is_null();
  }

  bool can_cancel_watch_event() const {
    return !watch_event_callback_.is_null();
  }

  // Methods for use by TraceMessageFilter.
  void AddTraceMessageFilter(TraceMessageFilter* trace_message_filter);
  void RemoveTraceMessageFilter(TraceMessageFilter* trace_message_filter);

  void OnTraceDataCollected(
      const scoped_refptr<base::RefCountedString>& events_str_ptr);
  void OnMonitoringTraceDataCollected(
      const scoped_refptr<base::RefCountedString>& events_str_ptr);

  // Callback of TraceLog::Flush() for the local trace.
  void OnLocalTraceDataCollected(
      const scoped_refptr<base::RefCountedString>& events_str_ptr,
      bool has_more_events);
  // Callback of TraceLog::FlushMonitoring() for the local trace.
  void OnLocalMonitoringTraceDataCollected(
      const scoped_refptr<base::RefCountedString>& events_str_ptr,
      bool has_more_events);

  void OnStopTracingAcked(
      TraceMessageFilter* trace_message_filter,
      const std::vector<std::string>& known_category_groups);

#if defined(OS_CHROMEOS) || defined(OS_WIN)
  void OnEndSystemTracingAcked(
      const scoped_refptr<base::RefCountedString>& events_str_ptr);
#endif

  void OnEndPowerTracingAcked(
      const scoped_refptr<base::RefCountedString>& events_str_ptr);

  void OnCaptureMonitoringSnapshotAcked(
      TraceMessageFilter* trace_message_filter);

  void OnTraceLogStatusReply(TraceMessageFilter* trace_message_filter,
                             const base::trace_event::TraceLogStatus& status);
  void OnProcessMemoryDumpResponse(TraceMessageFilter* trace_message_filter,
                                   uint64 dump_guid,
                                   bool success);

  // Callback of MemoryDumpManager::CreateProcessDump().
  void OnBrowserProcessMemoryDumpDone(uint64 dump_guid, bool success);

  void FinalizeGlobalMemoryDumpIfAllProcessesReplied();

  void OnWatchEventMatched();

  void SetEnabledOnFileThread(
      const base::trace_event::TraceConfig& trace_config,
      int mode,
      const base::Closure& callback);
  void SetDisabledOnFileThread(const base::Closure& callback);
  void OnStartTracingDone(
      const base::trace_event::TraceConfig& trace_config,
      const StartTracingDoneCallback& callback);
  void OnStopTracingDone();
  void OnStartMonitoringDone(
      const base::trace_event::TraceConfig& trace_config,
      const StartMonitoringDoneCallback& callback);
  void OnStopMonitoringDone(const StopMonitoringDoneCallback& callback);

  void OnMonitoringStateChanged(bool is_monitoring);

  typedef std::set<scoped_refptr<TraceMessageFilter>> TraceMessageFilterSet;
  TraceMessageFilterSet trace_message_filters_;

  // Pending acks for StopTracing.
  int pending_stop_tracing_ack_count_;
  TraceMessageFilterSet pending_stop_tracing_filters_;

  // Pending acks for CaptureMonitoringSnapshot.
  int pending_capture_monitoring_snapshot_ack_count_;
  TraceMessageFilterSet pending_capture_monitoring_filters_;

  // Pending acks for GetTraceLogStatus.
  int pending_trace_log_status_ack_count_;
  TraceMessageFilterSet pending_trace_log_status_filters_;
  float maximum_trace_buffer_usage_;
  size_t approximate_event_count_;

  // Pending acks for memory RequestGlobalDumpPoint.
  int pending_memory_dump_ack_count_;
  int failed_memory_dump_count_;
  TraceMessageFilterSet pending_memory_dump_filters_;
  uint64 pending_memory_dump_guid_;
  base::trace_event::MemoryDumpCallback pending_memory_dump_callback_;

#if defined(OS_CHROMEOS) || defined(OS_WIN)
  bool is_system_tracing_;
#endif
  bool is_tracing_;
  bool is_monitoring_;
  bool is_power_tracing_;

  GetCategoriesDoneCallback pending_get_categories_done_callback_;
  GetTraceBufferUsageCallback pending_trace_buffer_usage_callback_;

  std::string watch_category_name_;
  std::string watch_event_name_;
  WatchEventCallback watch_event_callback_;

  base::ObserverList<TraceMessageFilterObserver>
      trace_message_filter_observers_;

  std::set<std::string> known_category_groups_;
  std::set<TracingUI*> tracing_uis_;
  scoped_refptr<TraceDataSink> trace_data_sink_;
  scoped_refptr<TraceDataSink> monitoring_data_sink_;

  DISALLOW_COPY_AND_ASSIGN(TracingControllerImpl);
};

}  // namespace content

#endif  // CONTENT_BROWSER_TRACING_TRACING_CONTROLLER_IMPL_H_
