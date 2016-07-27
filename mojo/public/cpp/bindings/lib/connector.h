// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_CONNECTOR_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_CONNECTOR_H_

#include "base/memory/scoped_ptr.h"
#include "base/threading/thread_checker.h"
#include "mojo/public/c/environment/async_waiter.h"
#include "mojo/public/cpp/bindings/callback.h"
#include "mojo/public/cpp/bindings/message.h"
#include "mojo/public/cpp/environment/environment.h"
#include "mojo/public/cpp/system/core.h"

namespace base {
class Lock;
}

namespace mojo {
namespace internal {

// The Connector class is responsible for performing read/write operations on a
// MessagePipe. It writes messages it receives through the MessageReceiver
// interface that it subclasses, and it forwards messages it reads through the
// MessageReceiver interface assigned as its incoming receiver.
//
// NOTE:
//   - MessagePipe I/O is non-blocking.
//   - Sending messages can be configured to be thread safe (please see comments
//     of the constructor). Other than that, the object should only be accessed
//     on the creating thread.
class Connector : public MessageReceiver {
 public:
  enum ConnectorConfig {
    // Connector::Accept() is only called from a single thread.
    SINGLE_THREADED_SEND,
    // Connector::Accept() is allowed to be called from multiple threads.
    MULTI_THREADED_SEND
  };

  // The Connector takes ownership of |message_pipe|.
  Connector(
      ScopedMessagePipeHandle message_pipe,
      ConnectorConfig config,
      const MojoAsyncWaiter* waiter = Environment::GetDefaultAsyncWaiter());
  ~Connector() override;

  // Sets the receiver to handle messages read from the message pipe.  The
  // Connector will read messages from the pipe regardless of whether or not an
  // incoming receiver has been set.
  void set_incoming_receiver(MessageReceiver* receiver) {
    DCHECK(thread_checker_.CalledOnValidThread());
    incoming_receiver_ = receiver;
  }

  // Errors from incoming receivers will force the connector into an error
  // state, where no more messages will be processed. This method is used
  // during testing to prevent that from happening.
  void set_enforce_errors_from_incoming_receiver(bool enforce) {
    DCHECK(thread_checker_.CalledOnValidThread());
    enforce_errors_from_incoming_receiver_ = enforce;
  }

  // Sets the error handler to receive notifications when an error is
  // encountered while reading from the pipe or waiting to read from the pipe.
  void set_connection_error_handler(const Closure& error_handler) {
    DCHECK(thread_checker_.CalledOnValidThread());
    connection_error_handler_ = error_handler;
  }

  // Returns true if an error was encountered while reading from the pipe or
  // waiting to read from the pipe.
  bool encountered_error() const {
    DCHECK(thread_checker_.CalledOnValidThread());
    return error_;
  }

  // Closes the pipe. The connector is put into a quiescent state.
  //
  // Please note that this method shouldn't be called unless it results from an
  // explicit request of the user of bindings (e.g., the user sets an
  // InterfacePtr to null or closes a Binding).
  void CloseMessagePipe();

  // Releases the pipe. Connector is put into a quiescent state.
  ScopedMessagePipeHandle PassMessagePipe();

  // Enters the error state. The upper layer may do this for unrecoverable
  // issues such as invalid messages are received. If a connection error handler
  // has been set, it will be called asynchronously.
  //
  // It is a no-op if the connector is already in the error state or there isn't
  // a bound message pipe. Otherwise, it closes the message pipe, which notifies
  // the other end and also prevents potential danger (say, the caller raises
  // an error because it believes the other end is malicious). In order to
  // appear to the user that the connector still binds to a message pipe, it
  // creates a new message pipe, closes one end and binds to the other.
  void RaiseError();

  // Is the connector bound to a MessagePipe handle?
  bool is_valid() const {
    DCHECK(thread_checker_.CalledOnValidThread());
    return message_pipe_.is_valid();
  }

  // Waits for the next message on the pipe, blocking until one arrives,
  // |deadline| elapses, or an error happens. Returns |true| if a message has
  // been delivered, |false| otherwise.
  bool WaitForIncomingMessage(MojoDeadline deadline);

  // See Binding for details of pause/resume.
  void PauseIncomingMethodCallProcessing();
  void ResumeIncomingMethodCallProcessing();

  // MessageReceiver implementation:
  bool Accept(Message* message) override;

  MessagePipeHandle handle() const {
    DCHECK(thread_checker_.CalledOnValidThread());
    return message_pipe_.get();
  }

 private:
  static void CallOnHandleReady(void* closure, MojoResult result);
  void OnHandleReady(MojoResult result);

  void WaitToReadMore();

  // Returns false if |this| was destroyed during message dispatch.
  MOJO_WARN_UNUSED_RESULT bool ReadSingleMessage(MojoResult* read_result);

  // |this| can be destroyed during message dispatch.
  void ReadAllAvailableMessages();

  // If |force_pipe_reset| is true, this method replaces the existing
  // |message_pipe_| with a dummy message pipe handle (whose peer is closed).
  // If |force_async_handler| is true, |connection_error_handler_| is called
  // asynchronously.
  void HandleError(bool force_pipe_reset, bool force_async_handler);

  // Cancels any calls made to |waiter_|.
  void CancelWait();

  Closure connection_error_handler_;
  const MojoAsyncWaiter* waiter_;

  ScopedMessagePipeHandle message_pipe_;
  MessageReceiver* incoming_receiver_;

  MojoAsyncWaitID async_wait_id_;
  bool error_;
  bool drop_writes_;
  bool enforce_errors_from_incoming_receiver_;

  bool paused_;

  // If non-null, this will be set to true when the Connector is destroyed.  We
  // use this flag to allow for the Connector to be destroyed as a side-effect
  // of dispatching an incoming message.
  bool* destroyed_flag_;

  // If sending messages is allowed from multiple threads, |lock_| is used to
  // protect modifications to |message_pipe_| and |drop_writes_|.
  scoped_ptr<base::Lock> lock_;

  base::ThreadChecker thread_checker_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(Connector);
};

}  // namespace internal
}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_CONNECTOR_H_
