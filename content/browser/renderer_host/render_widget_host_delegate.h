// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_DELEGATE_H_
#define CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_DELEGATE_H_

#include <vector>

#include "base/basictypes.h"
#include "build/build_config.h"
#include "content/common/content_export.h"
#include "third_party/WebKit/public/platform/WebDisplayMode.h"
#include "third_party/WebKit/public/web/WebInputEvent.h"
#include "ui/gfx/native_widget_types.h"

namespace blink {
class WebMouseWheelEvent;
class WebGestureEvent;
}

namespace gfx {
class Point;
class Rect;
class Size;
}

namespace content {

class BrowserAccessibilityManager;
class RenderWidgetHostImpl;
class RenderWidgetHostInputEventRouter;
struct NativeWebKeyboardEvent;

//
// RenderWidgetHostDelegate
//
//  An interface implemented by an object interested in knowing about the state
//  of the RenderWidgetHost.
class CONTENT_EXPORT RenderWidgetHostDelegate {
 public:
  // The RenderWidgetHost has just been created.
  virtual void RenderWidgetCreated(RenderWidgetHostImpl* render_widget_host) {}

  // The RenderWidgetHost is going to be deleted.
  virtual void RenderWidgetDeleted(RenderWidgetHostImpl* render_widget_host) {}

  // The RenderWidgetHost got the focus.
  virtual void RenderWidgetGotFocus(RenderWidgetHostImpl* render_widget_host) {}

  // The RenderWidget was resized.
  virtual void RenderWidgetWasResized(RenderWidgetHostImpl* render_widget_host,
                                      bool width_changed) {}

  // The contents auto-resized and the container should match it.
  virtual void ResizeDueToAutoResize(RenderWidgetHostImpl* render_widget_host,
                                     const gfx::Size& new_size) {}

  // The screen info has changed.
  virtual void ScreenInfoChanged() {}

  // Callback to give the browser a chance to handle the specified keyboard
  // event before sending it to the renderer.
  // Returns true if the |event| was handled. Otherwise, if the |event| would
  // be handled in HandleKeyboardEvent() method as a normal keyboard shortcut,
  // |*is_keyboard_shortcut| should be set to true.
  virtual bool PreHandleKeyboardEvent(const NativeWebKeyboardEvent& event,
                                      bool* is_keyboard_shortcut);

  // Callback to inform the browser that the renderer did not process the
  // specified events. This gives an opportunity to the browser to process the
  // event (used for keyboard shortcuts).
  virtual void HandleKeyboardEvent(const NativeWebKeyboardEvent& event) {}

  // Callback to inform the browser that the renderer did not process the
  // specified mouse wheel event.  Returns true if the browser has handled
  // the event itself.
  virtual bool HandleWheelEvent(const blink::WebMouseWheelEvent& event);

  // Notification the user has performed a direct interaction (mouse down, mouse
  // wheel, raw key down, or gesture tap) while focus was on the page. Informs
  // the delegate that a user is interacting with a site. Only the first mouse
  // wheel event during a scroll will trigger this method.
  virtual void OnUserInteraction(const blink::WebInputEvent::Type type) {}

  // Callback to give the browser a chance to handle the specified gesture
  // event before sending it to the renderer.
  // Returns true if the |event| was handled.
  virtual bool PreHandleGestureEvent(const blink::WebGestureEvent& event);

  // Notification the user has made a gesture while focus was on the
  // page. This is used to avoid uninitiated user downloads (aka carpet
  // bombing), see DownloadRequestLimiter for details.
  virtual void OnUserGesture(RenderWidgetHostImpl* render_widget_host) {}

  // Notifies that screen rects were sent to renderer process.
  virtual void DidSendScreenRects(RenderWidgetHostImpl* rwh) {}

  // Get the root BrowserAccessibilityManager for this frame tree.
  virtual BrowserAccessibilityManager* GetRootBrowserAccessibilityManager();

  // Get the root BrowserAccessibilityManager for this frame tree,
  // or create it if it doesn't exist.
  virtual BrowserAccessibilityManager*
      GetOrCreateRootBrowserAccessibilityManager();

  // Send OS Cut/Copy/Paste actions to the focused frame.
  virtual void Cut() = 0;
  virtual void Copy() = 0;
  virtual void Paste() = 0;
  virtual void SelectAll() = 0;

  // Requests the renderer to move the selection extent to a new position.
  virtual void MoveRangeSelectionExtent(const gfx::Point& extent) {}

  // Requests the renderer to select the region between two points in the
  // currently focused frame.
  virtual void SelectRange(const gfx::Point& base, const gfx::Point& extent) {}

  virtual RenderWidgetHostInputEventRouter* GetInputEventRouter();

  // Send page-level focus state to all SiteInstances involved in rendering the
  // current FrameTree, not including the main frame's SiteInstance.
  virtual void ReplicatePageFocus(bool is_focused) {}

  // Get the focused RenderWidgetHost associated with |receiving_widget|. A
  // RenderWidgetHostView, upon receiving a keyboard event, will pass its
  // RenderWidgetHost to this function to determine who should ultimately
  // consume the event.  This facilitates keyboard event routing with
  // out-of-process iframes, where multiple RenderWidgetHosts may be involved
  // in rendering a page, yet keyboard events all arrive at the main frame's
  // RenderWidgetHostView.  When a main frame's RenderWidgetHost is passed in,
  // the function returns the focused frame that should consume keyboard
  // events. In all other cases, the function returns back |receiving_widget|.
  virtual RenderWidgetHostImpl* GetFocusedRenderWidgetHost(
      RenderWidgetHostImpl* receiving_widget);

  // Notification that the renderer has become unresponsive. The
  // delegate can use this notification to show a warning to the user.
  virtual void RendererUnresponsive(RenderWidgetHostImpl* render_widget_host) {}

  // Notification that a previously unresponsive renderer has become
  // responsive again. The delegate can use this notification to end the
  // warning shown to the user.
  virtual void RendererResponsive(RenderWidgetHostImpl* render_widget_host) {}

  // Requests to lock the mouse. Once the request is approved or rejected,
  // GotResponseToLockMouseRequest() will be called on the requesting render
  // widget host.
  virtual void RequestToLockMouse(RenderWidgetHostImpl* render_widget_host,
                                  bool user_gesture,
                                  bool last_unlocked_by_target) {}

  // Return the rect where to display the resize corner, if any, otherwise
  // an empty rect.
  virtual gfx::Rect GetRootWindowResizerRect(
      RenderWidgetHostImpl* render_widget_host) const;

  // Returns whether the associated tab is in fullscreen mode.
  virtual bool IsFullscreenForCurrentTab(
      RenderWidgetHostImpl* render_widget_host) const;

  // Returns the display mode for the view.
  virtual blink::WebDisplayMode GetDisplayMode(
      RenderWidgetHostImpl* render_widget_host) const;

  // Notification that the widget has lost capture.
  virtual void LostCapture(RenderWidgetHostImpl* render_widget_host) {}

  // Notification that the widget has lost the mouse lock.
  virtual void LostMouseLock(RenderWidgetHostImpl* render_widget_host) {}

#if defined(OS_WIN)
  virtual gfx::NativeViewAccessible GetParentNativeViewAccessible();
#endif

  // Called when the widget has sent a compositor proto.  This is used in Blimp
  // mode with the RemoteChannel compositor.
  virtual void ForwardCompositorProto(RenderWidgetHostImpl* render_widget_host,
                                      const std::vector<uint8_t>& proto) {}

 protected:
  virtual ~RenderWidgetHostDelegate() {}
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_DELEGATE_H_
