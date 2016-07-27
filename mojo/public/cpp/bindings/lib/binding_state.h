// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_BINDING_STATE_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_BINDING_STATE_H_

#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "mojo/public/c/environment/async_waiter.h"
#include "mojo/public/cpp/bindings/associated_group.h"
#include "mojo/public/cpp/bindings/callback.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_ptr_info.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/lib/filter_chain.h"
#include "mojo/public/cpp/bindings/lib/interface_endpoint_client.h"
#include "mojo/public/cpp/bindings/lib/interface_id.h"
#include "mojo/public/cpp/bindings/lib/message_header_validator.h"
#include "mojo/public/cpp/bindings/lib/multiplex_router.h"
#include "mojo/public/cpp/bindings/lib/router.h"
#include "mojo/public/cpp/bindings/lib/scoped_interface_endpoint_handle.h"
#include "mojo/public/cpp/system/core.h"

namespace mojo {
namespace internal {

template <typename Interface, bool use_multiplex_router>
class BindingState;

// Uses a single-threaded, dedicated router. If |Interface| doesn't have any
// methods to pass associated interface pointers or requests, there won't be
// multiple interfaces running on the underlying message pipe. In that case, we
// can use this specialization to reduce cost.
template <typename Interface>
class BindingState<Interface, false> {
 public:
  explicit BindingState(Interface* impl) : impl_(impl) {
    stub_.set_sink(impl_);
  }

  ~BindingState() {
    if (router_)
      Close();
  }

  void Bind(ScopedMessagePipeHandle handle, const MojoAsyncWaiter* waiter) {
    DCHECK(!router_);
    internal::FilterChain filters;
    filters.Append<internal::MessageHeaderValidator>();
    filters.Append<typename Interface::RequestValidator_>();

    router_ = new internal::Router(handle.Pass(), filters.Pass(), waiter);
    router_->set_incoming_receiver(&stub_);
    router_->set_connection_error_handler(
        [this]() { connection_error_handler_.Run(); });
  }

  void PauseIncomingMethodCallProcessing() {
    DCHECK(router_);
    router_->PauseIncomingMethodCallProcessing();
  }
  void ResumeIncomingMethodCallProcessing() {
    DCHECK(router_);
    router_->ResumeIncomingMethodCallProcessing();
  }

  bool WaitForIncomingMethodCall(
      MojoDeadline deadline = MOJO_DEADLINE_INDEFINITE) {
    DCHECK(router_);
    return router_->WaitForIncomingMessage(deadline);
  }

  void Close() {
    DCHECK(router_);
    router_->CloseMessagePipe();
    DestroyRouter();
  }

  InterfaceRequest<Interface> Unbind() {
    InterfaceRequest<Interface> request =
        MakeRequest<Interface>(router_->PassMessagePipe());
    DestroyRouter();
    return request.Pass();
  }

  void set_connection_error_handler(const Closure& error_handler) {
    connection_error_handler_ = error_handler;
  }

  Interface* impl() { return impl_; }

  bool is_bound() const { return !!router_; }

  MessagePipeHandle handle() const {
    DCHECK(is_bound());
    return router_->handle();
  }

  AssociatedGroup* associated_group() { return nullptr; }

  void EnableTestingMode() {
    DCHECK(is_bound());
    router_->EnableTestingMode();
  }

 private:
  void DestroyRouter() {
    router_->set_connection_error_handler(Closure());
    delete router_;
    router_ = nullptr;
  }

  internal::Router* router_ = nullptr;
  typename Interface::Stub_ stub_;
  Interface* impl_;
  Closure connection_error_handler_;

  DISALLOW_COPY_AND_ASSIGN(BindingState);
};

// Uses a multiplexing router. If |Interface| has methods to pass associated
// interface pointers or requests, this specialization should be used.
template <typename Interface>
class BindingState<Interface, true> {
 public:
  explicit BindingState(Interface* impl) : impl_(impl) {
    stub_.set_sink(impl_);
  }

  ~BindingState() {
    if (router_)
      Close();
  }

  void Bind(ScopedMessagePipeHandle handle, const MojoAsyncWaiter* waiter) {
    DCHECK(!router_);

    router_ = new internal::MultiplexRouter(false, handle.Pass(), waiter);
    stub_.serialization_context()->router = router_;

    endpoint_client_.reset(new internal::InterfaceEndpointClient(
        router_->CreateLocalEndpointHandle(internal::kMasterInterfaceId),
        &stub_, make_scoped_ptr(new typename Interface::RequestValidator_())));

    endpoint_client_->set_connection_error_handler(
        [this]() { connection_error_handler_.Run(); });
  }

  void Close() {
    DCHECK(router_);
    endpoint_client_.reset();
    router_->CloseMessagePipe();
    router_ = nullptr;
  }

  InterfaceRequest<Interface> Unbind() {
    endpoint_client_.reset();
    InterfaceRequest<Interface> request =
        MakeRequest<Interface>(router_->PassMessagePipe());
    router_ = nullptr;
    return request.Pass();
  }

  void set_connection_error_handler(const Closure& error_handler) {
    connection_error_handler_ = error_handler;
  }

  Interface* impl() { return impl_; }

  bool is_bound() const { return !!router_; }

  MessagePipeHandle handle() const {
    DCHECK(is_bound());
    return router_->handle();
  }

  AssociatedGroup* associated_group() {
    return endpoint_client_ ? endpoint_client_->associated_group() : nullptr;
  }

  void EnableTestingMode() {
    DCHECK(is_bound());
    router_->EnableTestingMode();
  }

  // Intentionally not defined:
  // void PauseIncomingMethodCallProcessing();
  // void ResumeIncomingMethodCallProcessing();
  // bool WaitForIncomingMethodCall(MojoDeadline deadline);

 private:
  scoped_refptr<internal::MultiplexRouter> router_;
  scoped_ptr<internal::InterfaceEndpointClient> endpoint_client_;

  typename Interface::Stub_ stub_;
  Interface* impl_;
  Closure connection_error_handler_;

  DISALLOW_COPY_AND_ASSIGN(BindingState);
};

}  // namesapce internal
}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_BINDING_STATE_H_
