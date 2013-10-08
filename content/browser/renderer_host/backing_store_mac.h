// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_BACKING_STORE_MAC_H_
#define CONTENT_BROWSER_RENDERER_HOST_BACKING_STORE_MAC_H_

#include "base/basictypes.h"
#include "base/mac/scoped_cftyperef.h"
#include "content/browser/renderer_host/backing_store.h"

namespace content {

class BackingStoreMac : public BackingStore {
 public:
  // |size| is in view units, |device_scale_factor| is the backingScaleFactor.
  // The pixel size of the backing store is size.Scale(device_scale_factor).
  BackingStoreMac(RenderWidgetHost* widget,
                  const gfx::Size& size,
                  float device_scale_factor);
  virtual ~BackingStoreMac();

  // A CGLayer that stores the contents of the backing store, cached in GPU
  // memory if possible.
  CGLayerRef cg_layer() { return cg_layer_; }

  // A CGBitmapContext that stores the contents of the backing store if the
  // corresponding Cocoa view has not been inserted into an NSWindow yet.
  CGContextRef cg_bitmap() { return cg_bitmap_; }

  // Called when the view's backing scale factor changes.
  void ScaleFactorChanged(float device_scale_factor);

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

  void CopyFromBackingStoreToCGContext(const CGRect& dest_rect,
                                       CGContextRef context);

 private:
  // Creates a CGLayer associated with its owner view's window's graphics
  // context, sized properly for the backing store.  Returns NULL if the owner
  // is not in a window with a CGContext.  cg_layer_ is assigned this method's
  // result.
  CGLayerRef CreateCGLayer();

  // Creates a CGBitmapContext sized properly for the backing store.  The
  // owner view need not be in a window.  cg_bitmap_ is assigned this method's
  // result.
  CGContextRef CreateCGBitmapContext();

  base::ScopedCFTypeRef<CGContextRef> cg_bitmap_;
  base::ScopedCFTypeRef<CGLayerRef> cg_layer_;

  // Number of physical pixels per view unit. This is 1 or 2 in practice.
  float device_scale_factor_;

  DISALLOW_COPY_AND_ASSIGN(BackingStoreMac);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_BACKING_STORE_MAC_H_
