// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/in_process_view_renderer.h"

#include <android/bitmap.h>

#include "android_webview/browser/aw_gl_surface.h"
#include "android_webview/browser/scoped_app_gl_state_restore.h"
#include "android_webview/common/aw_switches.h"
#include "android_webview/public/browser/draw_gl.h"
#include "android_webview/public/browser/draw_sw.h"
#include "base/android/jni_android.h"
#include "base/auto_reset.h"
#include "base/command_line.h"
#include "base/debug/trace_event.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "content/public/browser/android/synchronous_compositor.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "gpu/command_buffer/service/in_process_command_buffer.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkDevice.h"
#include "third_party/skia/include/core/SkGraphics.h"
#include "third_party/skia/include/core/SkPicture.h"
#include "ui/gfx/skia_util.h"
#include "ui/gfx/transform.h"
#include "ui/gfx/vector2d_conversions.h"
#include "ui/gfx/vector2d_f.h"

using base::android::AttachCurrentThread;
using base::android::JavaRef;
using base::android::ScopedJavaLocalRef;
using content::BrowserThread;

namespace android_webview {

namespace {


const void* kUserDataKey = &kUserDataKey;

class UserData : public content::WebContents::Data {
 public:
  UserData(InProcessViewRenderer* ptr) : instance_(ptr) {}
  virtual ~UserData() {
    instance_->WebContentsGone();
  }

  static InProcessViewRenderer* GetInstance(content::WebContents* contents) {
    if (!contents)
      return NULL;
    UserData* data = reinterpret_cast<UserData*>(
        contents->GetUserData(kUserDataKey));
    return data ? data->instance_ : NULL;
  }

 private:
  InProcessViewRenderer* instance_;
};

bool RasterizeIntoBitmap(JNIEnv* env,
                         const JavaRef<jobject>& jbitmap,
                         int scroll_x,
                         int scroll_y,
                         const InProcessViewRenderer::RenderMethod& renderer) {
  DCHECK(jbitmap.obj());

  AndroidBitmapInfo bitmap_info;
  if (AndroidBitmap_getInfo(env, jbitmap.obj(), &bitmap_info) < 0) {
    LOG(ERROR) << "Error getting java bitmap info.";
    return false;
  }

  void* pixels = NULL;
  if (AndroidBitmap_lockPixels(env, jbitmap.obj(), &pixels) < 0) {
    LOG(ERROR) << "Error locking java bitmap pixels.";
    return false;
  }

  bool succeeded;
  {
    SkBitmap bitmap;
    bitmap.setConfig(SkBitmap::kARGB_8888_Config,
                     bitmap_info.width,
                     bitmap_info.height,
                     bitmap_info.stride);
    bitmap.setPixels(pixels);

    SkDevice device(bitmap);
    SkCanvas canvas(&device);
    canvas.translate(-scroll_x, -scroll_y);
    succeeded = renderer.Run(&canvas);
  }

  if (AndroidBitmap_unlockPixels(env, jbitmap.obj()) < 0) {
    LOG(ERROR) << "Error unlocking java bitmap pixels.";
    return false;
  }

  return succeeded;
}

bool RenderPictureToCanvas(SkPicture* picture, SkCanvas* canvas) {
  canvas->drawPicture(*picture);
  return true;
}

class ScopedPixelAccess {
 public:
  ScopedPixelAccess(JNIEnv* env, jobject java_canvas) {
    AwDrawSWFunctionTable* sw_functions =
        BrowserViewRenderer::GetAwDrawSWFunctionTable();
    pixels_ = sw_functions ?
      sw_functions->access_pixels(env, java_canvas) : NULL;
  }
  ~ScopedPixelAccess() {
    if (pixels_)
      BrowserViewRenderer::GetAwDrawSWFunctionTable()->release_pixels(pixels_);
  }
  AwPixelInfo* pixels() { return pixels_; }

 private:
  AwPixelInfo* pixels_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(ScopedPixelAccess);
};

bool HardwareEnabled() {
  static bool g_hw_enabled = !CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableWebViewGLMode);
  return g_hw_enabled;
}

// Provides software rendering functions from the Android glue layer.
// Allows preventing extra copies of data when rendering.
AwDrawSWFunctionTable* g_sw_draw_functions = NULL;

const int64 kFallbackTickTimeoutInMilliseconds = 20;

class ScopedAllowGL {
 public:
  ScopedAllowGL();
  ~ScopedAllowGL();

  static bool IsAllowed() {
    return BrowserThread::CurrentlyOn(BrowserThread::UI) && allow_gl;
  }

 private:
  static bool allow_gl;

  DISALLOW_COPY_AND_ASSIGN(ScopedAllowGL);
};

ScopedAllowGL::ScopedAllowGL() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!allow_gl);
  allow_gl = true;
}

ScopedAllowGL::~ScopedAllowGL() {
  allow_gl = false;
}

bool ScopedAllowGL::allow_gl = false;

base::LazyInstance<GLViewRendererManager>::Leaky g_view_renderer_manager;

}  // namespace

// Called from different threads!
static void ScheduleGpuWork() {
  if (ScopedAllowGL::IsAllowed()) {
    gpu::InProcessCommandBuffer::ProcessGpuWorkOnCurrentThread();
  } else {
    InProcessViewRenderer* renderer = static_cast<InProcessViewRenderer*>(
        g_view_renderer_manager.Get().GetMostRecentlyDrawn());
    if (!renderer || !renderer->RequestProcessGL()) {
      LOG(ERROR) << "Failed to request DrawGL. Probably going to deadlock.";
    }
  }
}

// static
void BrowserViewRenderer::SetAwDrawSWFunctionTable(
    AwDrawSWFunctionTable* table) {
  g_sw_draw_functions = table;
  gpu::InProcessCommandBuffer::SetScheduleCallback(
      base::Bind(&ScheduleGpuWork));
}

// static
AwDrawSWFunctionTable* BrowserViewRenderer::GetAwDrawSWFunctionTable() {
  return g_sw_draw_functions;
}

InProcessViewRenderer::InProcessViewRenderer(
    BrowserViewRenderer::Client* client,
    JavaHelper* java_helper,
    content::WebContents* web_contents)
    : client_(client),
      java_helper_(java_helper),
      web_contents_(web_contents),
      compositor_(NULL),
      visible_(false),
      dip_scale_(0.0),
      page_scale_factor_(1.0),
      on_new_picture_enable_(false),
      compositor_needs_continuous_invalidate_(false),
      need_fast_invalidate_(false),
      block_invalidates_(false),
      width_(0),
      height_(0),
      attached_to_window_(false),
      hardware_initialized_(false),
      hardware_failed_(false),
      last_egl_context_(NULL),
      manager_key_(g_view_renderer_manager.Get().NullKey()) {
  CHECK(web_contents_);
  web_contents_->SetUserData(kUserDataKey, new UserData(this));
  content::SynchronousCompositor::SetClientForWebContents(web_contents_, this);

  // Currently the logic in this class relies on |compositor_| remaining NULL
  // until the DidInitializeCompositor() call, hence it is not set here.
}

InProcessViewRenderer::~InProcessViewRenderer() {
  CHECK(web_contents_);
  content::SynchronousCompositor::SetClientForWebContents(web_contents_, NULL);
  web_contents_->SetUserData(kUserDataKey, NULL);
  NoLongerExpectsDrawGL();
  DCHECK(web_contents_ == NULL);  // WebContentsGone should have been called.
}


// TODO(boliu): Should also call this when we know for sure we are no longer,
// for example, when visible rect becomes 0.
void InProcessViewRenderer::NoLongerExpectsDrawGL() {
  GLViewRendererManager& mru = g_view_renderer_manager.Get();
  if (manager_key_ != mru.NullKey()) {
    mru.NoLongerExpectsDrawGL(manager_key_);
    manager_key_ = mru.NullKey();

    // TODO(boliu): If this is the first one and there are GL pending,
    // requestDrawGL on next one.
    // TODO(boliu): If this is the last one, lose all global contexts.
  }
}

// static
InProcessViewRenderer* InProcessViewRenderer::FromWebContents(
    content::WebContents* contents) {
  return UserData::GetInstance(contents);
}

void InProcessViewRenderer::WebContentsGone() {
  web_contents_ = NULL;
  compositor_ = NULL;
}

bool InProcessViewRenderer::RequestProcessGL() {
  return client_->RequestDrawGL(NULL);
}

void InProcessViewRenderer::UpdateCachedGlobalVisibleRect() {
  client_->UpdateGlobalVisibleRect();
}

bool InProcessViewRenderer::OnDraw(jobject java_canvas,
                                   bool is_hardware_canvas,
                                   const gfx::Vector2d& scroll,
                                   const gfx::Rect& clip) {
  scroll_at_start_of_frame_  = scroll;
  if (is_hardware_canvas && attached_to_window_ && HardwareEnabled()) {
    // We should be performing a hardware draw here. If we don't have the
    // comositor yet or if RequestDrawGL fails, it means we failed this draw and
    // thus return false here to clear to background color for this draw.
    return compositor_ && client_->RequestDrawGL(java_canvas);
  }
  // Perform a software draw
  block_invalidates_ = true;
  bool result = DrawSWInternal(java_canvas, clip);
  block_invalidates_ = false;
  EnsureContinuousInvalidation(NULL, false);
  return result;
}

bool InProcessViewRenderer::InitializeHwDraw() {
  TRACE_EVENT0("android_webview", "InitializeHwDraw");
  DCHECK(!gl_surface_);
  gl_surface_  = new AwGLSurface;
  hardware_failed_ = !compositor_->InitializeHwDraw(gl_surface_);
  hardware_initialized_ = true;

  if (hardware_failed_)
    gl_surface_ = NULL;

  return !hardware_failed_;
}

void InProcessViewRenderer::DrawGL(AwDrawGLInfo* draw_info) {
  TRACE_EVENT0("android_webview", "InProcessViewRenderer::DrawGL");

  manager_key_ = g_view_renderer_manager.Get().DidDrawGL(manager_key_, this);

  // We need to watch if the current Android context has changed and enforce
  // a clean-up in the compositor.
  EGLContext current_context = eglGetCurrentContext();
  if (!current_context) {
    TRACE_EVENT_INSTANT0(
        "android_webview", "EarlyOut_NullEGLContext", TRACE_EVENT_SCOPE_THREAD);
    return;
  }

  ScopedAppGLStateRestore state_restore(ScopedAppGLStateRestore::MODE_DRAW);
  gpu::InProcessCommandBuffer::ProcessGpuWorkOnCurrentThread();
  ScopedAllowGL allow_gl;

  if (attached_to_window_ && compositor_ && !hardware_initialized_) {
    if (InitializeHwDraw()) {
      last_egl_context_ = current_context;
    } else {
      return;
    }
  }

  if (draw_info->mode == AwDrawGLInfo::kModeProcess) {
    TRACE_EVENT_INSTANT0(
        "android_webview", "EarlyOut_ModeProcess", TRACE_EVENT_SCOPE_THREAD);
    return;
  }

  UpdateCachedGlobalVisibleRect();
  if (cached_global_visible_rect_.IsEmpty()) {
    TRACE_EVENT_INSTANT0("android_webview",
                         "EarlyOut_EmptyVisibleRect",
                         TRACE_EVENT_SCOPE_THREAD);
    return;
  }

  // DrawGL may be called without OnDraw, so cancel |fallback_tick_| here as
  // well just to be safe.
  fallback_tick_.Cancel();

  if (last_egl_context_ != current_context) {
    // TODO(boliu): Handle context lost
    TRACE_EVENT_INSTANT0(
        "android_webview", "EGLContextChanged", TRACE_EVENT_SCOPE_THREAD);
  }

  if (!compositor_) {
    TRACE_EVENT_INSTANT0(
        "android_webview", "EarlyOut_NoCompositor", TRACE_EVENT_SCOPE_THREAD);
    return;
  }

  DCHECK(gl_surface_);
  gl_surface_->SetBackingFrameBufferObject(
      state_restore.framebuffer_binding_ext());

  gfx::Transform transform;
  transform.matrix().setColMajorf(draw_info->transform);
  transform.Translate(scroll_at_start_of_frame_.x(),
                      scroll_at_start_of_frame_.y());
  gfx::Rect clip_rect(draw_info->clip_left,
                      draw_info->clip_top,
                      draw_info->clip_right - draw_info->clip_left,
                      draw_info->clip_bottom - draw_info->clip_top);

  // Assume we always draw the full visible rect if we are drawing into a layer.
  bool drew_full_visible_rect = true;

  if (!draw_info->is_layer) {
    clip_rect.Intersect(cached_global_visible_rect_);
    drew_full_visible_rect = clip_rect.Contains(cached_global_visible_rect_);
  }

  block_invalidates_ = true;
  // TODO(joth): Check return value.
  compositor_->DemandDrawHw(gfx::Size(draw_info->width, draw_info->height),
                            transform,
                            clip_rect,
                            state_restore.stencil_enabled());
  block_invalidates_ = false;
  gl_surface_->ResetBackingFrameBufferObject();

  EnsureContinuousInvalidation(draw_info, !drew_full_visible_rect);
}

void InProcessViewRenderer::SetGlobalVisibleRect(
    const gfx::Rect& visible_rect) {
  cached_global_visible_rect_ = visible_rect;
}

bool InProcessViewRenderer::DrawSWInternal(jobject java_canvas,
                                           const gfx::Rect& clip) {
  fallback_tick_.Cancel();

  if (clip.IsEmpty()) {
    TRACE_EVENT_INSTANT0(
        "android_webview", "EarlyOut_EmptyClip", TRACE_EVENT_SCOPE_THREAD);
    return true;
  }

  if (!compositor_) {
    TRACE_EVENT_INSTANT0(
        "android_webview", "EarlyOut_NoCompositor", TRACE_EVENT_SCOPE_THREAD);
    return false;
  }

  return RenderViaAuxilaryBitmapIfNeeded(
      java_canvas,
      java_helper_,
      scroll_at_start_of_frame_,
      clip,
      base::Bind(&InProcessViewRenderer::CompositeSW,
                 base::Unretained(this)),
      web_contents_);
}

// static
bool InProcessViewRenderer::RenderViaAuxilaryBitmapIfNeeded(
      jobject java_canvas,
      BrowserViewRenderer::JavaHelper* java_helper,
      const gfx::Vector2d& scroll_correction,
      const gfx::Rect& clip,
      InProcessViewRenderer::RenderMethod render_source,
      void* owner_key) {
  TRACE_EVENT0("android_webview",
               "InProcessViewRenderer::RenderViaAuxilaryBitmapIfNeeded");

  JNIEnv* env = AttachCurrentThread();
  ScopedPixelAccess auto_release_pixels(env, java_canvas);
  AwPixelInfo* pixels = auto_release_pixels.pixels();
  SkMatrix matrix;
  SkBitmap::Config config(SkBitmap::kNo_Config);
  if (pixels) {
    switch (pixels->config) {
      case AwConfig_ARGB_8888:
        config = SkBitmap::kARGB_8888_Config;
        break;
      case AwConfig_RGB_565:
        config = SkBitmap::kRGB_565_Config;
        break;
    }

    for (int i = 0; i < 9; i++) {
      matrix.set(i, pixels->matrix[i]);
    }
    // Workaround for http://crbug.com/271096: SW draw only supports
    // translate & scale transforms.
    if (matrix.getType() & ~(SkMatrix::kTranslate_Mask | SkMatrix::kScale_Mask))
      config = SkBitmap::kNo_Config;
  }

  if (config == SkBitmap::kNo_Config) {
    // Render into an auxiliary bitmap if pixel info is not available.
    ScopedJavaLocalRef<jobject> jcanvas(env, java_canvas);
    TRACE_EVENT0("android_webview", "RenderToAuxBitmap");
    ScopedJavaLocalRef<jobject> jbitmap(java_helper->CreateBitmap(
        env, clip.width(), clip.height(), jcanvas, owner_key));
    if (!jbitmap.obj()) {
      TRACE_EVENT_INSTANT0("android_webview",
                           "EarlyOut_BitmapAllocFail",
                           TRACE_EVENT_SCOPE_THREAD);
      return false;
    }

    if (!RasterizeIntoBitmap(env, jbitmap,
                             clip.x() - scroll_correction.x(),
                             clip.y() - scroll_correction.y(),
                             render_source)) {
      TRACE_EVENT_INSTANT0("android_webview",
                           "EarlyOut_RasterizeFail",
                           TRACE_EVENT_SCOPE_THREAD);
      return false;
    }

    java_helper->DrawBitmapIntoCanvas(env, jbitmap, jcanvas,
                                      clip.x(), clip.y());
    return true;
  }

  // Draw in a SkCanvas built over the pixel information.
  SkBitmap bitmap;
  bitmap.setConfig(config,
                   pixels->width,
                   pixels->height,
                   pixels->row_bytes);
  bitmap.setPixels(pixels->pixels);
  SkDevice device(bitmap);
  SkCanvas canvas(&device);
  canvas.setMatrix(matrix);

  if (pixels->clip_rect_count) {
    SkRegion clip;
    for (int i = 0; i < pixels->clip_rect_count; ++i) {
      clip.op(SkIRect::MakeXYWH(pixels->clip_rects[i + 0],
                                pixels->clip_rects[i + 1],
                                pixels->clip_rects[i + 2],
                                pixels->clip_rects[i + 3]),
              SkRegion::kUnion_Op);
    }
    canvas.setClipRegion(clip);
  }

  canvas.translate(scroll_correction.x(),
                   scroll_correction.y());

  return render_source.Run(&canvas);
}

skia::RefPtr<SkPicture> InProcessViewRenderer::CapturePicture(int width,
                                                              int height) {
  TRACE_EVENT0("android_webview", "InProcessViewRenderer::CapturePicture");

  // Return empty Picture objects for empty SkPictures.
  skia::RefPtr<SkPicture> picture = skia::AdoptRef(new SkPicture);
  if (width <= 0 || height <= 0) {
    return picture;
  }

  // Reset scroll back to the origin, will go back to the old
  // value when scroll_reset is out of scope.
  base::AutoReset<gfx::Vector2dF> scroll_reset(&scroll_offset_css_,
                                               gfx::Vector2d());

  SkCanvas* rec_canvas = picture->beginRecording(width, height, 0);
  if (compositor_)
    CompositeSW(rec_canvas);
  picture->endRecording();
  return picture;
}

void InProcessViewRenderer::EnableOnNewPicture(bool enabled) {
  on_new_picture_enable_ = enabled;
}

void InProcessViewRenderer::OnVisibilityChanged(bool visible) {
  TRACE_EVENT_INSTANT1("android_webview",
                       "InProcessViewRenderer::OnVisibilityChanged",
                       TRACE_EVENT_SCOPE_THREAD,
                       "visible",
                       visible);
  visible_ = visible;
}

void InProcessViewRenderer::OnSizeChanged(int width, int height) {
  TRACE_EVENT_INSTANT2("android_webview",
                       "InProcessViewRenderer::OnSizeChanged",
                       TRACE_EVENT_SCOPE_THREAD,
                       "width",
                       width,
                       "height",
                       height);
  width_ = width;
  height_ = height;
}

void InProcessViewRenderer::OnAttachedToWindow(int width, int height) {
  TRACE_EVENT2("android_webview",
               "InProcessViewRenderer::OnAttachedToWindow",
               "width",
               width,
               "height",
               height);
  attached_to_window_ = true;
  width_ = width;
  height_ = height;
}

void InProcessViewRenderer::OnDetachedFromWindow() {
  TRACE_EVENT0("android_webview",
               "InProcessViewRenderer::OnDetachedFromWindow");

  NoLongerExpectsDrawGL();
  if (hardware_initialized_) {
    DCHECK(compositor_);

    ScopedAppGLStateRestore state_restore(
        ScopedAppGLStateRestore::MODE_DETACH_FROM_WINDOW);
    gpu::InProcessCommandBuffer::ProcessGpuWorkOnCurrentThread();
    ScopedAllowGL allow_gl;
    compositor_->ReleaseHwDraw();
    hardware_initialized_ = false;
  }

  gl_surface_ = NULL;
  attached_to_window_ = false;
}

bool InProcessViewRenderer::IsAttachedToWindow() {
  return attached_to_window_;
}

bool InProcessViewRenderer::IsViewVisible() {
  return visible_;
}

gfx::Rect InProcessViewRenderer::GetScreenRect() {
  return gfx::Rect(client_->GetLocationOnScreen(), gfx::Size(width_, height_));
}

void InProcessViewRenderer::DidInitializeCompositor(
    content::SynchronousCompositor* compositor) {
  TRACE_EVENT0("android_webview",
               "InProcessViewRenderer::DidInitializeCompositor");
  DCHECK(compositor && compositor_ == NULL);
  compositor_ = compositor;
  hardware_initialized_ = false;
  hardware_failed_ = false;
}

void InProcessViewRenderer::DidDestroyCompositor(
    content::SynchronousCompositor* compositor) {
  TRACE_EVENT0("android_webview",
               "InProcessViewRenderer::DidDestroyCompositor");
  DCHECK(compositor_ == compositor);

  // This can fail if Apps call destroy while the webview is still attached
  // to the view tree. This is an illegal operation that will lead to leaks.
  // Log for now. Consider a proper fix if this becomes a problem.
  LOG_IF(ERROR, hardware_initialized_)
      << "Destroy called before OnDetachedFromWindow. May Leak GL resources";
  compositor_ = NULL;
}

void InProcessViewRenderer::SetContinuousInvalidate(bool invalidate) {
  if (compositor_needs_continuous_invalidate_ == invalidate)
    return;

  TRACE_EVENT_INSTANT1("android_webview",
                       "InProcessViewRenderer::SetContinuousInvalidate",
                       TRACE_EVENT_SCOPE_THREAD,
                       "invalidate",
                       invalidate);
  compositor_needs_continuous_invalidate_ = invalidate;
  need_fast_invalidate_ = compositor_needs_continuous_invalidate_;
  EnsureContinuousInvalidation(NULL, false);
}

void InProcessViewRenderer::SetDipScale(float dip_scale) {
  dip_scale_ = dip_scale;
  CHECK(dip_scale_ > 0);
}

void InProcessViewRenderer::SetPageScaleFactor(float page_scale_factor) {
  page_scale_factor_ = page_scale_factor;
  CHECK(page_scale_factor_ > 0);
}

void InProcessViewRenderer::ScrollTo(gfx::Vector2d new_value) {
  DCHECK(dip_scale_ > 0);
  // In general we don't guarantee that the scroll offset transforms are
  // symmetrical. That is if scrolling from JS to offset1 results in a native
  // offset2 then scrolling from UI to offset2 results in JS being scrolled to
  // offset1 again.
  // The reason we explicitly do rounding here is that it seems to yeld the
  // most stabile transformation.
  gfx::Vector2dF new_value_css = gfx::ToRoundedVector2d(
      gfx::ScaleVector2d(new_value, 1.0f / (dip_scale_ * page_scale_factor_)));

  // It's possible that more than one set of unique physical coordinates maps
  // to the same set of CSS coordinates which means we can't reliably early-out
  // earlier in the call stack.
  if (scroll_offset_css_ == new_value_css)
    return;

  scroll_offset_css_ = new_value_css;

  if (compositor_)
    compositor_->DidChangeRootLayerScrollOffset();
}

void InProcessViewRenderer::DidUpdateContent() {
  if (on_new_picture_enable_)
    client_->OnNewPicture();
}

void InProcessViewRenderer::SetTotalRootLayerScrollOffset(
    gfx::Vector2dF new_value_css) {
  // TOOD(mkosiba): Add a DCHECK to say that this does _not_ get called during
  // DrawGl when http://crbug.com/249972 is fixed.
  if (scroll_offset_css_ == new_value_css)
    return;

  scroll_offset_css_ = new_value_css;

  DCHECK(dip_scale_ > 0);
  DCHECK(page_scale_factor_ > 0);

  gfx::Vector2d scroll_offset = gfx::ToRoundedVector2d(
      gfx::ScaleVector2d(new_value_css, dip_scale_ * page_scale_factor_));

  client_->ScrollContainerViewTo(scroll_offset);
}

gfx::Vector2dF InProcessViewRenderer::GetTotalRootLayerScrollOffset() {
  return scroll_offset_css_;
}

void InProcessViewRenderer::DidOverscroll(
    gfx::Vector2dF latest_overscroll_delta,
    gfx::Vector2dF current_fling_velocity) {
  // TODO(mkosiba): Enable this once flinging is handled entirely Java-side.
  // DCHECK(current_fling_velocity.IsZero());
  const float physical_pixel_scale = dip_scale_ * page_scale_factor_;
  gfx::Vector2dF scaled_overscroll_delta = gfx::ScaleVector2d(
      latest_overscroll_delta + overscroll_rounding_error_,
      physical_pixel_scale);
  gfx::Vector2d rounded_overscroll_delta =
      gfx::ToRoundedVector2d(scaled_overscroll_delta);
  overscroll_rounding_error_ =
      scaled_overscroll_delta - rounded_overscroll_delta;
  client_->DidOverscroll(rounded_overscroll_delta);
}

void InProcessViewRenderer::EnsureContinuousInvalidation(
    AwDrawGLInfo* draw_info,
    bool invalidate_ignore_compositor) {
  if ((compositor_needs_continuous_invalidate_ ||
       invalidate_ignore_compositor) &&
      !block_invalidates_) {
    if (draw_info) {
      draw_info->dirty_left = cached_global_visible_rect_.x();
      draw_info->dirty_top = cached_global_visible_rect_.y();
      draw_info->dirty_right = cached_global_visible_rect_.right();
      draw_info->dirty_bottom = cached_global_visible_rect_.bottom();
      draw_info->status_mask |= AwDrawGLInfo::kStatusMaskDraw;
    } else {
      client_->PostInvalidate();
    }

    block_invalidates_ = true;

    // Unretained here is safe because the callback is cancelled when
    // |fallback_tick_| is destroyed.
    fallback_tick_.Reset(base::Bind(&InProcessViewRenderer::FallbackTickFired,
                                    base::Unretained(this)));

    // No need to reschedule fallback tick if compositor does not need to be
    // ticked. This can happen if this is reached because
    // invalidate_ignore_compositor is true.
    if (compositor_needs_continuous_invalidate_) {
      int64 delay_in_ms = kFallbackTickTimeoutInMilliseconds;
      if (need_fast_invalidate_) {
        TRACE_EVENT_INSTANT0(
            "android_webview", "FastFallbackTick", TRACE_EVENT_SCOPE_THREAD);
        delay_in_ms = 0;
        need_fast_invalidate_ = false;
      }
      base::MessageLoop::current()->PostDelayedTask(
          FROM_HERE,
          fallback_tick_.callback(),
          base::TimeDelta::FromMilliseconds(delay_in_ms));
    }
  }
}

void InProcessViewRenderer::FallbackTickFired() {
  TRACE_EVENT1("android_webview",
               "InProcessViewRenderer::FallbackTickFired",
               "compositor_needs_continuous_invalidate_",
               compositor_needs_continuous_invalidate_);

  // This should only be called if OnDraw or DrawGL did not come in time, which
  // means block_invalidates_ must still be true.
  DCHECK(block_invalidates_);
  if (compositor_needs_continuous_invalidate_ && compositor_) {
    SkDevice device(SkBitmap::kARGB_8888_Config, 1, 1);
    SkCanvas canvas(&device);
    block_invalidates_ = true;
    CompositeSW(&canvas);
  }
  block_invalidates_ = false;
  EnsureContinuousInvalidation(NULL, false);
}

bool InProcessViewRenderer::CompositeSW(SkCanvas* canvas) {
  DCHECK(compositor_);
  return compositor_->DemandDrawSw(canvas);
}

std::string InProcessViewRenderer::ToString(AwDrawGLInfo* draw_info) const {
  std::string str;
  base::StringAppendF(&str, "visible: %d ", visible_);
  base::StringAppendF(&str, "dip_scale: %f ", dip_scale_);
  base::StringAppendF(&str, "page_scale_factor: %f ", page_scale_factor_);
  base::StringAppendF(&str,
                      "compositor_needs_continuous_invalidate: %d ",
                      compositor_needs_continuous_invalidate_);
  base::StringAppendF(&str, "block_invalidates: %d ", block_invalidates_);
  base::StringAppendF(&str, "view width height: [%d %d] ", width_, height_);
  base::StringAppendF(&str, "attached_to_window: %d ", attached_to_window_);
  base::StringAppendF(&str, "hardware_initialized: %d ", hardware_initialized_);
  base::StringAppendF(&str, "hardware_failed: %d ", hardware_failed_);
  base::StringAppendF(&str,
                      "global visible rect: %s ",
                      cached_global_visible_rect_.ToString().c_str());
  base::StringAppendF(&str,
                      "scroll_at_start_of_frame: %s ",
                      scroll_at_start_of_frame_.ToString().c_str());
  base::StringAppendF(
      &str, "scroll_offset_css: %s ", scroll_offset_css_.ToString().c_str());
  base::StringAppendF(&str,
                      "overscroll_rounding_error_: %s ",
                      overscroll_rounding_error_.ToString().c_str());
  if (draw_info) {
    base::StringAppendF(&str,
                        "clip left top right bottom: [%d %d %d %d] ",
                        draw_info->clip_left,
                        draw_info->clip_top,
                        draw_info->clip_right,
                        draw_info->clip_bottom);
    base::StringAppendF(&str,
                        "surface width height: [%d %d] ",
                        draw_info->width,
                        draw_info->height);
    base::StringAppendF(&str, "is_layer: %d ", draw_info->is_layer);
  }
  return str;
}

}  // namespace android_webview
