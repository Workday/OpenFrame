// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/services/tracing/tracing_app.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "mojo/application/public/cpp/application_connection.h"

namespace tracing {

TracingApp::TracingApp() : collector_binding_(this), tracing_active_(false) {
}

TracingApp::~TracingApp() {
}

bool TracingApp::ConfigureIncomingConnection(
    mojo::ApplicationConnection* connection) {
  connection->AddService<TraceCollector>(this);
  connection->AddService<StartupPerformanceDataCollector>(this);

  // If someone connects to us they may want to use the TraceCollector
  // interface and/or they may want to expose themselves to be traced. Attempt
  // to connect to the TraceProvider interface to see if the application
  // connecting to us wants to be traced. They can refuse the connection or
  // close the pipe if not.
  TraceProviderPtr provider_ptr;
  connection->ConnectToService(&provider_ptr);
  if (tracing_active_) {
    TraceRecorderPtr recorder_ptr;
    recorder_impls_.push_back(
        new TraceRecorderImpl(GetProxy(&recorder_ptr), sink_.get()));
    provider_ptr->StartTracing(tracing_categories_, recorder_ptr.Pass());
  }
  provider_ptrs_.AddInterfacePtr(provider_ptr.Pass());
  return true;
}

void TracingApp::Create(mojo::ApplicationConnection* connection,
                        mojo::InterfaceRequest<TraceCollector> request) {
  collector_binding_.Bind(request.Pass());
}

void TracingApp::Create(
    mojo::ApplicationConnection* connection,
    mojo::InterfaceRequest<StartupPerformanceDataCollector> request) {
  startup_performance_data_collector_bindings_.AddBinding(this, request.Pass());
}

void TracingApp::Start(mojo::ScopedDataPipeProducerHandle stream,
                       const mojo::String& categories) {
  tracing_categories_ = categories;
  sink_.reset(new TraceDataSink(stream.Pass()));
  provider_ptrs_.ForAllPtrs([categories, this](TraceProvider* controller) {
    TraceRecorderPtr ptr;
    recorder_impls_.push_back(
        new TraceRecorderImpl(GetProxy(&ptr), sink_.get()));
    controller->StartTracing(categories, ptr.Pass());
  });
  tracing_active_ = true;
}

void TracingApp::StopAndFlush() {
  // Remove any collectors that closed their message pipes before we called
  // StopTracing().
  for (int i = static_cast<int>(recorder_impls_.size()) - 1; i >= 0; --i) {
    if (!recorder_impls_[i]->TraceRecorderHandle().is_valid()) {
      recorder_impls_.erase(recorder_impls_.begin() + i);
    }
  }

  tracing_active_ = false;
  provider_ptrs_.ForAllPtrs(
      [](TraceProvider* controller) { controller->StopTracing(); });

  // Sending the StopTracing message to registered controllers will request that
  // they send trace data back via the collector interface and, when they are
  // done, close the collector pipe. We don't know how long they will take. We
  // want to read all data that any collector might send until all collectors or
  // closed or an (arbitrary) deadline has passed. Since the bindings don't
  // support this directly we do our own MojoWaitMany over the handles and read
  // individual messages until all are closed or our absolute deadline has
  // elapsed.
  static const MojoDeadline kTimeToWaitMicros = 1000 * 1000;
  MojoTimeTicks end = MojoGetTimeTicksNow() + kTimeToWaitMicros;

  while (!recorder_impls_.empty()) {
    MojoTimeTicks now = MojoGetTimeTicksNow();
    if (now >= end)  // Timed out?
      break;

    MojoDeadline mojo_deadline = end - now;
    std::vector<mojo::Handle> handles;
    std::vector<MojoHandleSignals> signals;
    for (const auto& it : recorder_impls_) {
      handles.push_back(it->TraceRecorderHandle());
      signals.push_back(MOJO_HANDLE_SIGNAL_READABLE |
                        MOJO_HANDLE_SIGNAL_PEER_CLOSED);
    }
    std::vector<MojoHandleSignalsState> signals_states(signals.size());
    const mojo::WaitManyResult wait_many_result =
        mojo::WaitMany(handles, signals, mojo_deadline, &signals_states);
    if (wait_many_result.result == MOJO_RESULT_DEADLINE_EXCEEDED) {
      // Timed out waiting, nothing more to read.
      LOG(WARNING) << "Timed out waiting for trace flush";
      break;
    }
    if (wait_many_result.IsIndexValid()) {
      // Iterate backwards so we can remove closed pipes from |recorder_impls_|
      // without invalidating subsequent offsets.
      for (size_t i = signals_states.size(); i != 0; --i) {
        size_t index = i - 1;
        MojoHandleSignals satisfied = signals_states[index].satisfied_signals;
        // To avoid dropping data, don't close unless there's no
        // readable signal.
        if (satisfied & MOJO_HANDLE_SIGNAL_READABLE)
          recorder_impls_[index]->TryRead();
        else if (satisfied & MOJO_HANDLE_SIGNAL_PEER_CLOSED)
          recorder_impls_.erase(recorder_impls_.begin() + index);
      }
    }
  }
  AllDataCollected();
}

void TracingApp::SetShellProcessCreationTime(int64 time) {
  if (startup_performance_times_.shell_process_creation_time == 0)
    startup_performance_times_.shell_process_creation_time = time;
}

void TracingApp::SetShellMainEntryPointTime(int64 time) {
  if (startup_performance_times_.shell_main_entry_point_time == 0)
    startup_performance_times_.shell_main_entry_point_time = time;
}

void TracingApp::SetBrowserMessageLoopStartTicks(int64 ticks) {
  if (startup_performance_times_.browser_message_loop_start_ticks == 0)
    startup_performance_times_.browser_message_loop_start_ticks = ticks;
}

void TracingApp::SetBrowserWindowDisplayTicks(int64 ticks) {
  if (startup_performance_times_.browser_window_display_ticks == 0)
    startup_performance_times_.browser_window_display_ticks = ticks;
}

void TracingApp::SetBrowserOpenTabsTimeDelta(int64 delta) {
  if (startup_performance_times_.browser_open_tabs_time_delta == 0)
    startup_performance_times_.browser_open_tabs_time_delta = delta;
}

void TracingApp::SetFirstWebContentsMainFrameLoadTicks(int64 ticks) {
  if (startup_performance_times_.first_web_contents_main_frame_load_ticks == 0)
    startup_performance_times_.first_web_contents_main_frame_load_ticks = ticks;
}

void TracingApp::SetFirstVisuallyNonEmptyLayoutTicks(int64 ticks) {
  if (startup_performance_times_.first_visually_non_empty_layout_ticks == 0)
    startup_performance_times_.first_visually_non_empty_layout_ticks = ticks;
}

void TracingApp::GetStartupPerformanceTimes(
    const GetStartupPerformanceTimesCallback& callback) {
  callback.Run(startup_performance_times_.Clone());
}

void TracingApp::AllDataCollected() {
  recorder_impls_.clear();
  sink_.reset();
}

}  // namespace tracing
