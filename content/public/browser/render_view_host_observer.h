// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_RENDER_VIEW_HOST_OBSERVER_H_
#define CONTENT_PUBLIC_BROWSER_RENDER_VIEW_HOST_OBSERVER_H_

#include "base/compiler_specific.h"
#include "ipc/ipc_listener.h"
#include "ipc/ipc_sender.h"
#include "content/common/content_export.h"

class GURL;

namespace content {

class RenderViewHost;
class RenderViewHostImpl;

// An observer API implemented by classes which want to filter IPC messages from
// RenderViewHost.
class CONTENT_EXPORT RenderViewHostObserver : public IPC::Listener,
                                              public IPC::Sender {
 public:

 protected:
  explicit RenderViewHostObserver(RenderViewHost* render_view_host);

  virtual ~RenderViewHostObserver();

  // Invoked after the RenderViewHost is created in the renderer process.  After
  // this point, messages can be sent to it (or to observers in the renderer).
  virtual void RenderViewHostInitialized();

  // Invoked when the RenderViewHost is being destroyed. Gives subclasses a
  // chance to cleanup.  The base implementation will delete the object.
  // |render_view_host| is passed as an argument since render_view_host() will
  // return NULL once this method enters.
  virtual void RenderViewHostDestroyed(RenderViewHost* render_view_host);

  // Notifies that a navigation is starting.
  virtual void Navigate(const GURL& url);

  // IPC::Listener implementation.
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

  // IPC::Sender implementation.
  virtual bool Send(IPC::Message* message) OVERRIDE;

  RenderViewHost* render_view_host() const;
  int routing_id() { return routing_id_; }

 private:
  friend class RenderViewHostImpl;

  // Invoked from RenderViewHost. Invokes RenderViewHostDestroyed and NULL out
  // |render_view_host_|.
  void RenderViewHostDestruction();

  RenderViewHostImpl* render_view_host_;

  // The routing ID of the associated RenderViewHost.
  int routing_id_;

  DISALLOW_COPY_AND_ASSIGN(RenderViewHostObserver);
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_RENDER_VIEW_HOST_OBSERVER_H_
