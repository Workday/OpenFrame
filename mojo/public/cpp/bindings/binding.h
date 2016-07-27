// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_CPP_BINDINGS_BINDING_H_
#define MOJO_PUBLIC_CPP_BINDINGS_BINDING_H_

#include "base/macros.h"
#include "mojo/public/c/environment/async_waiter.h"
#include "mojo/public/cpp/bindings/callback.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_ptr_info.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/lib/binding_state.h"
#include "mojo/public/cpp/system/core.h"

namespace mojo {

class AssociatedGroup;

// Represents the binding of an interface implementation to a message pipe.
// When the |Binding| object is destroyed, the binding between the message pipe
// and the interface is torn down and the message pipe is closed, leaving the
// interface implementation in an unbound state.
//
// Example:
//
//   #include "foo.mojom.h"
//
//   class FooImpl : public Foo {
//    public:
//     explicit FooImpl(InterfaceRequest<Foo> request)
//         : binding_(this, request.Pass()) {}
//
//     // Foo implementation here.
//
//    private:
//     Binding<Foo> binding_;
//   };
//
//   class MyFooFactory : public InterfaceFactory<Foo> {
//    public:
//     void Create(..., InterfaceRequest<Foo> request) override {
//       auto f = new FooImpl(request.Pass());
//       // Do something to manage the lifetime of |f|. Use StrongBinding<> to
//       // delete FooImpl on connection errors.
//     }
//   };
//
// The caller may specify a |MojoAsyncWaiter| to be used by the connection when
// waiting for calls to arrive. Normally it is fine to use the default waiter.
// However, the caller may provide their own implementation if needed. The
// |Binding| will not take ownership of the waiter, and the waiter must outlive
// the |Binding|. The provided waiter must be able to signal the implementation
// which generally means it needs to be able to schedule work on the thread the
// implementation runs on. If writing library code that has to work on different
// types of threads callers may need to provide different waiter
// implementations.
template <typename Interface>
class Binding {
 public:
  // Constructs an incomplete binding that will use the implementation |impl|.
  // The binding may be completed with a subsequent call to the |Bind| method.
  // Does not take ownership of |impl|, which must outlive the binding.
  explicit Binding(Interface* impl) : internal_state_(impl) {}

  // Constructs a completed binding of message pipe |handle| to implementation
  // |impl|. Does not take ownership of |impl|, which must outlive the binding.
  // See class comment for definition of |waiter|.
  Binding(Interface* impl,
          ScopedMessagePipeHandle handle,
          const MojoAsyncWaiter* waiter = Environment::GetDefaultAsyncWaiter())
      : Binding(impl) {
    Bind(handle.Pass(), waiter);
  }

  // Constructs a completed binding of |impl| to a new message pipe, passing the
  // client end to |ptr|, which takes ownership of it. The caller is expected to
  // pass |ptr| on to the client of the service. Does not take ownership of any
  // of the parameters. |impl| must outlive the binding. |ptr| only needs to
  // last until the constructor returns. See class comment for definition of
  // |waiter|.
  Binding(Interface* impl,
          InterfacePtr<Interface>* ptr,
          const MojoAsyncWaiter* waiter = Environment::GetDefaultAsyncWaiter())
      : Binding(impl) {
    Bind(ptr, waiter);
  }

  // Constructs a completed binding of |impl| to the message pipe endpoint in
  // |request|, taking ownership of the endpoint. Does not take ownership of
  // |impl|, which must outlive the binding. See class comment for definition of
  // |waiter|.
  Binding(Interface* impl,
          InterfaceRequest<Interface> request,
          const MojoAsyncWaiter* waiter = Environment::GetDefaultAsyncWaiter())
      : Binding(impl) {
    Bind(request.PassMessagePipe(), waiter);
  }

  // Tears down the binding, closing the message pipe and leaving the interface
  // implementation unbound.
  ~Binding() {}

  // Completes a binding that was constructed with only an interface
  // implementation. Takes ownership of |handle| and binds it to the previously
  // specified implementation. See class comment for definition of |waiter|.
  void Bind(
      ScopedMessagePipeHandle handle,
      const MojoAsyncWaiter* waiter = Environment::GetDefaultAsyncWaiter()) {
    internal_state_.Bind(handle.Pass(), waiter);
  }

  // Completes a binding that was constructed with only an interface
  // implementation by creating a new message pipe, binding one end of it to the
  // previously specified implementation, and passing the other to |ptr|, which
  // takes ownership of it. The caller is expected to pass |ptr| on to the
  // eventual client of the service. Does not take ownership of |ptr|. See
  // class comment for definition of |waiter|.
  void Bind(
      InterfacePtr<Interface>* ptr,
      const MojoAsyncWaiter* waiter = Environment::GetDefaultAsyncWaiter()) {
    MessagePipe pipe;
    ptr->Bind(
        InterfacePtrInfo<Interface>(pipe.handle0.Pass(), Interface::Version_),
        waiter);
    Bind(pipe.handle1.Pass(), waiter);
  }

  // Completes a binding that was constructed with only an interface
  // implementation by removing the message pipe endpoint from |request| and
  // binding it to the previously specified implementation. See class comment
  // for definition of |waiter|.
  void Bind(
      InterfaceRequest<Interface> request,
      const MojoAsyncWaiter* waiter = Environment::GetDefaultAsyncWaiter()) {
    Bind(request.PassMessagePipe(), waiter);
  }

  // Stops processing incoming messages until
  // ResumeIncomingMethodCallProcessing(), or WaitForIncomingMethodCall().
  // Outgoing messages are still sent.
  //
  // No errors are detected on the message pipe while paused.
  //
  // NOTE: Not supported (yet) if |Interface| has methods to pass associated
  // interface pointers/requests.
  void PauseIncomingMethodCallProcessing() {
    internal_state_.PauseIncomingMethodCallProcessing();
  }
  // NOTE: Not supported (yet) if |Interface| has methods to pass associated
  // interface pointers/requests.
  void ResumeIncomingMethodCallProcessing() {
    internal_state_.ResumeIncomingMethodCallProcessing();
  }

  // Blocks the calling thread until either a call arrives on the previously
  // bound message pipe, the deadline is exceeded, or an error occurs. Returns
  // true if a method was successfully read and dispatched.
  //
  // NOTE: Not supported (yet) if |Interface| has methods to pass associated
  // interface pointers/requests.
  bool WaitForIncomingMethodCall(
      MojoDeadline deadline = MOJO_DEADLINE_INDEFINITE) {
    return internal_state_.WaitForIncomingMethodCall(deadline);
  }

  // Closes the message pipe that was previously bound. Put this object into a
  // state where it can be rebound to a new pipe.
  void Close() { internal_state_.Close(); }

  // Unbinds the underlying pipe from this binding and returns it so it can be
  // used in another context, such as on another thread or with a different
  // implementation. Put this object into a state where it can be rebound to a
  // new pipe.
  InterfaceRequest<Interface> Unbind() { return internal_state_.Unbind(); }

  // Sets an error handler that will be called if a connection error occurs on
  // the bound message pipe.
  void set_connection_error_handler(const Closure& error_handler) {
    internal_state_.set_connection_error_handler(error_handler);
  }

  // Returns the interface implementation that was previously specified. Caller
  // does not take ownership.
  Interface* impl() { return internal_state_.impl(); }

  // Indicates whether the binding has been completed (i.e., whether a message
  // pipe has been bound to the implementation).
  bool is_bound() const { return internal_state_.is_bound(); }

  // Returns the value of the handle currently bound to this Binding which can
  // be used to make explicit Wait/WaitMany calls. Requires that the Binding be
  // bound. Ownership of the handle is retained by the Binding, it is not
  // transferred to the caller.
  MessagePipeHandle handle() const { return internal_state_.handle(); }

  // Returns the associated group that this object belongs to. Returns null if:
  //   - this object is not bound; or
  //   - the interface doesn't have methods to pass associated interface
  //     pointers or requests.
  AssociatedGroup* associated_group() {
    return internal_state_.associated_group();
  }

  // Exposed for testing, should not generally be used.
  void EnableTestingMode() { internal_state_.EnableTestingMode(); }

 private:
  internal::BindingState<Interface, Interface::PassesAssociatedKinds_>
      internal_state_;

  DISALLOW_COPY_AND_ASSIGN(Binding);
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_BINDING_H_
