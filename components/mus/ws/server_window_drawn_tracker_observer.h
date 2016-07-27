// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MUS_WS_SERVER_WINDOW_DRAWN_TRACKER_OBSERVER_H_
#define COMPONENTS_MUS_WS_SERVER_WINDOW_DRAWN_TRACKER_OBSERVER_H_

namespace mus {

namespace ws {

class ServerWindow;

class ServerWindowDrawnTrackerObserver {
 public:
  // Invoked when the drawn state changes. If |is_drawn| is false |ancestor|
  // identifies where the change occurred. In the case of a remove |ancestor| is
  // the parent of the window that was removed. In the case of a visibility
  // change |ancestor| is the parent of the window whose visibility changed.
  virtual void OnDrawnStateChanged(ServerWindow* ancestor,
                                   ServerWindow* window,
                                   bool is_drawn) {}

 protected:
  virtual ~ServerWindowDrawnTrackerObserver() {}
};

}  // namespace ws

}  // namespace mus

#endif  // COMPONENTS_MUS_WS_SERVER_WINDOW_DRAWN_TRACKER_OBSERVER_H_
