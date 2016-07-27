// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/services/tracing/public/cpp/tracing_impl.h"

#include "base/lazy_instance.h"
#include "base/synchronization/lock.h"
#include "base/threading/platform_thread.h"
#include "base/trace_event/trace_event_impl.h"
#include "mojo/application/public/cpp/application_impl.h"

#ifdef NDEBUG
#include "base/command_line.h"
#include "mojo/services/tracing/public/cpp/switches.h"
#endif

namespace mojo {
namespace {

// Controls access to |g_tracing_singleton_created|, which can be accessed from
// different threads.
base::LazyInstance<base::Lock>::Leaky g_singleton_lock =
    LAZY_INSTANCE_INITIALIZER;

// Whether we are the first TracingImpl to be created in this mojo
// application. The first TracingImpl in a physical mojo application connects
// to the mojo:tracing service.
//
// If this is a ContentHandler, it will outlive all its served Applications. If
// this is a raw mojo application, it is the only Application served.
bool g_tracing_singleton_created = false;

}

TracingImpl::TracingImpl() {
}

TracingImpl::~TracingImpl() {
}

void TracingImpl::Initialize(ApplicationImpl* app) {
  {
    base::AutoLock lock(g_singleton_lock.Get());
    if (g_tracing_singleton_created)
      return;
    g_tracing_singleton_created = true;
  }

  // This will only set the name for the first app in a loaded mojo file. It's
  // up to something like CoreServices to name its own child threads.
  base::PlatformThread::SetName(app->url());

  connection_ = app->ConnectToApplication("mojo:tracing");
  connection_->AddService(this);

#ifdef NDEBUG
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          tracing::kEarlyTracing)) {
    provider_impl_.ForceEnableTracing();
  }
#else
  provider_impl_.ForceEnableTracing();
#endif
}

void TracingImpl::Create(ApplicationConnection* connection,
                         InterfaceRequest<tracing::TraceProvider> request) {
  provider_impl_.Bind(request.Pass());
}

}  // namespace mojo
