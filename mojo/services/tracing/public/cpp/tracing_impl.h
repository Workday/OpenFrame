// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_TRACING_PUBLIC_CPP_TRACING_IMPL_H_
#define MOJO_SERVICES_TRACING_PUBLIC_CPP_TRACING_IMPL_H_

#include "base/macros.h"
#include "mojo/application/public/cpp/interface_factory.h"
#include "mojo/services/tracing/public/cpp/trace_provider_impl.h"
#include "mojo/services/tracing/public/interfaces/tracing.mojom.h"

namespace mojo {

class ApplicationConnection;
class ApplicationImpl;

// Connects to mojo:tracing during your Application's Initialize() call once
// per process.
//
// We need to deal with multiple ways of packaging mojo applications
// together. We'll need to deal with packages that use the mojo.ContentHandler
// interface to bundle several Applciations into a single physical on disk
// mojo binary, and with those same services each in their own mojo binary.
//
// Have your bundle ContentHandler own a TracingImpl, and each Application own
// a TracingImpl. In bundles, the second TracingImpl will be a no-op.
class TracingImpl : public InterfaceFactory<tracing::TraceProvider> {
 public:
  TracingImpl();
  ~TracingImpl() override;

  // This connects to the tracing service and registers ourselves to provide
  // tracing data on demand.
  void Initialize(ApplicationImpl* app);

 private:
  // InterfaceFactory<tracing::TraceProvider> implementation.
  void Create(ApplicationConnection* connection,
              InterfaceRequest<tracing::TraceProvider> request) override;

  scoped_ptr<ApplicationConnection> connection_;
  TraceProviderImpl provider_impl_;

  DISALLOW_COPY_AND_ASSIGN(TracingImpl);
};

}  // namespace mojo

#endif  // MOJO_SERVICES_TRACING_PUBLIC_CPP_TRACING_IMPL_H_
