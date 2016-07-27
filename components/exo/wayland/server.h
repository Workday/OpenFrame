// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_EXO_WAYLAND_SERVER_H_
#define COMPONENTS_EXO_WAYLAND_SERVER_H_

#include <string>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/time/time.h"
#include "components/exo/wayland/scoped_wl_types.h"

namespace exo {
class Display;

namespace wayland {

// This class is a thin wrapper around a Wayland display server. All Wayland
// requests are dispatched into the given Exospere display.
class Server {
 public:
  explicit Server(Display* display);
  ~Server();

  // Creates a Wayland display server that clients can connect to using the
  // default socket name.
  static scoped_ptr<Server> Create(Display* display);

  // This adds a Unix socket to the Wayland display server which can be used
  // by clients to connect to the display server.
  bool AddSocket(const std::string name);

  // Returns the file descriptor associated with the server.
  int GetFileDescriptor() const;

  // This function dispatches events. This must be called on a thread for
  // which it's safe to access the Exospere display that this server was
  // created for. The |timeout| argument specifies the amount of time that
  // Dispatch() should block waiting for the file descriptor to become ready.
  void Dispatch(base::TimeDelta timeout);

  // Send all buffered events to the clients.
  void Flush();

 private:
  Display* const display_;
  ScopedWLDisplay wl_display_;

  DISALLOW_COPY_AND_ASSIGN(Server);
};

}  // namespace wayland
}  // namespace exo

#endif  // COMPONENTS_EXO_WAYLAND_SERVER_H_
