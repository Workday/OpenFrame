// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_BACKING_STORE_WIN_H_
#define CONTENT_BROWSER_RENDERER_HOST_BACKING_STORE_WIN_H_

#include <windows.h>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "content/browser/renderer_host/backing_store.h"

namespace content {

class BackingStoreWin : public BackingStore {
 public:
  BackingStoreWin(RenderWidgetHost* widget, const gfx::Size& size);
  virtual ~BackingStoreWin();

  HDC hdc() { return hdc_; }

  // Returns true if we should convert to the monitor profile when painting.
  static bool ColorManagementEnabled();

  // BackingStore implementation.
  virtual size_t MemorySize() OVERRIDE;
  virtual void PaintToBackingStore(
      RenderProcessHost* process,
      TransportDIB::Id bitmap,
      const gfx::Rect& bitmap_rect,
      const std::vector<gfx::Rect>& copy_rects,
      float scale_factor,
      const base::Closure& completion_callback,
      bool* scheduled_completion_callback) OVERRIDE;
  virtual bool CopyFromBackingStore(const gfx::Rect& rect,
                                    skia::PlatformBitmap* output) OVERRIDE;
  virtual void ScrollBackingStore(const gfx::Vector2d& delta,
                                  const gfx::Rect& clip_rect,
                                  const gfx::Size& view_size) OVERRIDE;

 private:
  // The backing store dc.
  HDC hdc_;

  // Handle to the backing store dib.
  HANDLE backing_store_dib_;

  // Handle to the original bitmap in the dc.
  HANDLE original_bitmap_;

  // Number of bits per pixel of the screen.
  int color_depth_;

  DISALLOW_COPY_AND_ASSIGN(BackingStoreWin);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_BACKING_STORE_WIN_H_
