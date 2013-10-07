// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_FRAME_SNIFFER_H_
#define CHROME_RENDERER_FRAME_SNIFFER_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "content/public/renderer/render_view_observer.h"

// Class which observes events from the frame with specific name and sends IPC
// messages to be handled by RenderViewHostObserver.
class FrameSniffer : public content::RenderViewObserver {
 public:
  FrameSniffer(content::RenderView* render_view,
               const string16 &unique_frame_name);
  virtual ~FrameSniffer();

  // Implements RenderViewObserver.
  virtual void DidFailProvisionalLoad(
      WebKit::WebFrame* frame, const WebKit::WebURLError& error) OVERRIDE;
  virtual void DidCommitProvisionalLoad(WebKit::WebFrame* frame,
                                        bool is_new_navigation) OVERRIDE;

 private:
  bool ShouldSniffFrame(WebKit::WebFrame* frame);

  // Name of the frame to be monitored.
  string16 unique_frame_name_;

  DISALLOW_COPY_AND_ASSIGN(FrameSniffer);
};

#endif  // CHROME_RENDERER_FRAME_SNIFFER_H_
