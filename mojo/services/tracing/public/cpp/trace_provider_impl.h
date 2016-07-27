// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_TRACING_PUBLIC_CPP_TRACE_PROVIDER_IMPL_H_
#define MOJO_SERVICES_TRACING_PUBLIC_CPP_TRACE_PROVIDER_IMPL_H_

#include "base/macros.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/services/tracing/public/interfaces/tracing.mojom.h"

namespace mojo {

class TraceProviderImpl : public tracing::TraceProvider {
 public:
  TraceProviderImpl();
  ~TraceProviderImpl() override;

  void Bind(InterfaceRequest<tracing::TraceProvider> request);

  // Enable tracing without waiting for an inbound connection. It will stop if
  // no TraceRecorder is sent within a set time.
  void ForceEnableTracing();

 private:
  // tracing::TraceProvider implementation:
  void StartTracing(const String& categories,
                    tracing::TraceRecorderPtr recorder) override;
  void StopTracing() override;

  void SendChunk(const scoped_refptr<base::RefCountedString>& events_str,
                 bool has_more_events);

  void DelayedStop();
  // Stop the collection of traces if no external connection asked for them yet.
  void StopIfForced();

  Binding<tracing::TraceProvider> binding_;
  bool tracing_forced_;
  tracing::TraceRecorderPtr recorder_;

  base::WeakPtrFactory<TraceProviderImpl> weak_factory_;
  DISALLOW_COPY_AND_ASSIGN(TraceProviderImpl);
};

}  // namespace mojo

#endif  // MOJO_SERVICES_TRACING_PUBLIC_CPP_TRACE_PROVIDER_IMPL_H_
