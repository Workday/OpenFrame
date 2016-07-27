// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_MOJO_SRC_MOJO_EDK_SYSTEM_SIMPLE_DISPATCHER_H_
#define THIRD_PARTY_MOJO_SRC_MOJO_EDK_SYSTEM_SIMPLE_DISPATCHER_H_

#include <list>

#include "mojo/public/cpp/system/macros.h"
#include "third_party/mojo/src/mojo/edk/system/awakable_list.h"
#include "third_party/mojo/src/mojo/edk/system/dispatcher.h"
#include "third_party/mojo/src/mojo/edk/system/system_impl_export.h"

namespace mojo {
namespace system {

// A base class for simple dispatchers. "Simple" means that there's a one-to-one
// correspondence between handles and dispatchers (see the explanatory comment
// in core.cc). This class implements the standard waiter-signalling mechanism
// in that case.
class MOJO_SYSTEM_IMPL_EXPORT SimpleDispatcher : public Dispatcher {
 protected:
  SimpleDispatcher();
  ~SimpleDispatcher() override;

  // To be called by subclasses when the state changes (so
  // |GetHandleSignalsStateImplNoLock()| should be checked again).
  void HandleSignalsStateChangedNoLock() MOJO_EXCLUSIVE_LOCKS_REQUIRED(mutex());

  // |Dispatcher| protected methods:
  void CancelAllAwakablesNoLock() override;
  MojoResult AddAwakableImplNoLock(Awakable* awakable,
                                   MojoHandleSignals signals,
                                   uintptr_t context,
                                   HandleSignalsState* signals_state) override;
  void RemoveAwakableImplNoLock(Awakable* awakable,
                                HandleSignalsState* signals_state) override;

 private:
  AwakableList awakable_list_ MOJO_GUARDED_BY(mutex());

  MOJO_DISALLOW_COPY_AND_ASSIGN(SimpleDispatcher);
};

}  // namespace system
}  // namespace mojo

#endif  // THIRD_PARTY_MOJO_SRC_MOJO_EDK_SYSTEM_SIMPLE_DISPATCHER_H_
