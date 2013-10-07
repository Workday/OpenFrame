// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/test/render_widget_test.h"

#include "base/basictypes.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/stringprintf.h"
#include "content/common/view_messages.h"
#include "content/renderer/render_view_impl.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/WebKit/public/platform/WebSize.h"
#include "third_party/WebKit/public/web/WebView.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/size.h"
#include "ui/surface/transport_dib.h"

namespace content {

const int RenderWidgetTest::kNumBytesPerPixel = 4;
const int RenderWidgetTest::kLargeWidth = 1024;
const int RenderWidgetTest::kLargeHeight = 768;
const int RenderWidgetTest::kSmallWidth = 600;
const int RenderWidgetTest::kSmallHeight = 450;
const int RenderWidgetTest::kTextPositionX = 800;
const int RenderWidgetTest::kTextPositionY = 600;
const uint32 RenderWidgetTest::kRedARGB = 0xFFFF0000;

RenderWidgetTest::RenderWidgetTest() {}

void RenderWidgetTest::ResizeAndPaint(const gfx::Size& page_size,
                                      const gfx::Size& desired_size,
                                      SkBitmap* snapshot) {
  ASSERT_TRUE(snapshot);
  static int g_sequence_num = 0;
  // Use a new sequence number for each DIB.
  scoped_ptr<TransportDIB> pixels(
      TransportDIB::Create(
          page_size.width() * page_size.height() * kNumBytesPerPixel,
          ++g_sequence_num));

  // Go ahead and map the DIB into memory, so that we can use it below to fill
  // tmp_bitmap.  Note that we need to do this before calling OnPaintAtSize, or
  // the last reference to the shared memory will  be closed and the handle will
  // no longer be valid.
  scoped_ptr<TransportDIB> mapped_pixels(TransportDIB::Map(pixels->handle()));

  RenderViewImpl* impl = static_cast<RenderViewImpl*>(view_);
  impl->OnPaintAtSize(pixels->handle(), g_sequence_num, page_size,
                      desired_size);
  ProcessPendingMessages();
  const IPC::Message* msg = render_thread_->sink().GetUniqueMessageMatching(
      ViewHostMsg_PaintAtSize_ACK::ID);
  ASSERT_NE(static_cast<IPC::Message*>(NULL), msg);
  ViewHostMsg_PaintAtSize_ACK::Param params;
  ViewHostMsg_PaintAtSize_ACK::Read(msg, &params);
  render_thread_->sink().ClearMessages();
  EXPECT_EQ(g_sequence_num, params.a);
  gfx::Size size = params.b;
  EXPECT_EQ(desired_size, size);

  SkBitmap tmp_bitmap;
  tmp_bitmap.setConfig(SkBitmap::kARGB_8888_Config,
                       size.width(), size.height());
  tmp_bitmap.setPixels(mapped_pixels->memory());
  // Copy the pixels from the TransportDIB object to the given snapshot.
  ASSERT_TRUE(tmp_bitmap.copyTo(snapshot, SkBitmap::kARGB_8888_Config));
}

void RenderWidgetTest::TestResizeAndPaint() {
  // Hello World message is only visible if the view size is at least
  // kTextPositionX x kTextPositionY
  LoadHTML(base::StringPrintf(
      "<html><body><div style='position: absolute; top: %d; left: "
      "%d; background-color: red;'>Hello World</div></body></html>",
      kTextPositionY, kTextPositionX).c_str());
  WebKit::WebSize old_size = view_->GetWebView()->size();

  SkBitmap bitmap;
  // If we re-size the view to something smaller than where the 'Hello World'
  // text is displayed we won't see any text in the snapshot.  Hence,
  // the snapshot should not contain any red.
  gfx::Size size(kSmallWidth, kSmallHeight);
  ResizeAndPaint(size, size, &bitmap);
  // Make sure that the view has been re-sized to its old size.
  EXPECT_TRUE(old_size == view_->GetWebView()->size());
  EXPECT_EQ(kSmallWidth, bitmap.width());
  EXPECT_EQ(kSmallHeight, bitmap.height());
  EXPECT_FALSE(ImageContainsColor(bitmap, kRedARGB));

  // Since we ask for the view to be re-sized to something larger than where the
  // 'Hello World' text is written the text should be visible in the snapshot.
  // Hence, the snapshot should contain some red.
  size.SetSize(kLargeWidth, kLargeHeight);
  ResizeAndPaint(size, size, &bitmap);
  EXPECT_TRUE(old_size == view_->GetWebView()->size());
  EXPECT_EQ(kLargeWidth, bitmap.width());
  EXPECT_EQ(kLargeHeight, bitmap.height());
  EXPECT_TRUE(ImageContainsColor(bitmap, kRedARGB));

  // Even if the desired size is smaller than where the text is located we
  // should still see the 'Hello World' message since the view size is
  // still large enough.
  ResizeAndPaint(size, gfx::Size(kSmallWidth, kSmallHeight), &bitmap);
  EXPECT_TRUE(old_size == view_->GetWebView()->size());
  EXPECT_EQ(kSmallWidth, bitmap.width());
  EXPECT_EQ(kSmallHeight, bitmap.height());
  EXPECT_TRUE(ImageContainsColor(bitmap, kRedARGB));
}

bool RenderWidgetTest::ImageContainsColor(const SkBitmap& bitmap,
                                          uint32 argb_color) {
  SkAutoLockPixels lock(bitmap);
  bool ready = bitmap.readyToDraw();
  EXPECT_TRUE(ready);
  if (!ready) {
    return false;
  }
  for (int x = 0; x < bitmap.width(); ++x) {
    for (int y = 0; y < bitmap.height(); ++y) {
      if (argb_color == *bitmap.getAddr32(x, y)) {
        return true;
      }
    }
  }
  return false;
}

void RenderWidgetTest::OutputBitmapToFile(const SkBitmap& bitmap,
                                          const base::FilePath& file_path) {
  scoped_refptr<base::RefCountedBytes> bitmap_data(new base::RefCountedBytes());
  SkAutoLockPixels lock(bitmap);
  ASSERT_TRUE(gfx::JPEGCodec::Encode(
      reinterpret_cast<unsigned char*>(bitmap.getAddr32(0, 0)),
      gfx::JPEGCodec::FORMAT_BGRA,
      bitmap.width(),
      bitmap.height(),
      static_cast<int>(bitmap.rowBytes()),
      90 /* quality */,
      &bitmap_data->data()));
  ASSERT_LT(0, file_util::WriteFile(
      file_path,
      reinterpret_cast<const char*>(bitmap_data->front()),
      bitmap_data->size()));
}

void RenderWidgetTest::TestOnResize() {
  RenderWidget* widget = static_cast<RenderViewImpl*>(view_);

  // The initial bounds is empty, so setting it to the same thing should do
  // nothing.
  ViewMsg_Resize_Params resize_params;
  resize_params.screen_info = WebKit::WebScreenInfo();
  resize_params.new_size = gfx::Size();
  resize_params.physical_backing_size = gfx::Size();
  resize_params.overdraw_bottom_height = 0.f;
  resize_params.resizer_rect = gfx::Rect();
  resize_params.is_fullscreen = false;
  widget->OnResize(resize_params);
  EXPECT_FALSE(widget->next_paint_is_resize_ack());

  // Setting empty physical backing size should not send the ack.
  resize_params.new_size = gfx::Size(10, 10);
  widget->OnResize(resize_params);
  EXPECT_FALSE(widget->next_paint_is_resize_ack());

  // Setting the bounds to a "real" rect should send the ack.
  render_thread_->sink().ClearMessages();
  gfx::Size size(100, 100);
  resize_params.new_size = size;
  resize_params.physical_backing_size = size;
  widget->OnResize(resize_params);
  EXPECT_TRUE(widget->next_paint_is_resize_ack());
  widget->DoDeferredUpdate();
  ProcessPendingMessages();

  const ViewHostMsg_UpdateRect* msg =
      static_cast<const ViewHostMsg_UpdateRect*>(
          render_thread_->sink().GetUniqueMessageMatching(
              ViewHostMsg_UpdateRect::ID));
  ASSERT_TRUE(msg);
  ViewHostMsg_UpdateRect::Schema::Param update_rect_params;
  EXPECT_TRUE(ViewHostMsg_UpdateRect::Read(msg, &update_rect_params));
  EXPECT_TRUE(ViewHostMsg_UpdateRect_Flags::is_resize_ack(
      update_rect_params.a.flags));
  EXPECT_EQ(size,
      update_rect_params.a.view_size);
  render_thread_->sink().ClearMessages();

  // Setting the same size again should not send the ack.
  widget->OnResize(resize_params);
  EXPECT_FALSE(widget->next_paint_is_resize_ack());

  // Resetting the rect to empty should not send the ack.
  resize_params.new_size = gfx::Size();
  resize_params.physical_backing_size = gfx::Size();
  widget->OnResize(resize_params);
  EXPECT_FALSE(widget->next_paint_is_resize_ack());
}

}  // namespace content
