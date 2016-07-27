// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBRARIES_NACL_IO_SOCKET_UNIX_EVENT_EMITTER_H_
#define LIBRARIES_NACL_IO_SOCKET_UNIX_EVENT_EMITTER_H_

#include "nacl_io/fifo_char.h"
#include "nacl_io/stream/stream_event_emitter.h"

#include "sdk_util/macros.h"
#include "sdk_util/scoped_ref.h"
#include "sdk_util/simple_lock.h"

namespace nacl_io {

class UnixEventEmitter;
class Node;

typedef sdk_util::ScopedRef<UnixEventEmitter> ScopedUnixEventEmitter;

class UnixEventEmitter : public StreamEventEmitter {
 public:
  uint32_t ReadIn_Locked(char* buffer, uint32_t len);
  uint32_t WriteOut_Locked(const char* buffer, uint32_t len);

  uint32_t BytesInOutputFIFO();
  uint32_t SpaceInInputFIFO();

  virtual ScopedUnixEventEmitter GetPeerEmitter() = 0;

  static ScopedUnixEventEmitter MakeUnixEventEmitter(size_t size);

 protected:
  UnixEventEmitter() {}

  // Probably only need the master's lock.
  virtual const sdk_util::SimpleLock& GetFifoLock() = 0;
  virtual FIFOInterface* in_fifo() { return in_fifoc(); }
  virtual FIFOInterface* out_fifo() { return out_fifoc(); }
  virtual FIFOChar* in_fifoc() = 0;
  virtual FIFOChar* out_fifoc() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(UnixEventEmitter);
};

}  // namespace nacl_io

#endif  // LIBRARIES_NACL_IO_SOCKET_UNIX_EVENT_EMITTER_H_
