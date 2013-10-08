// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/apps/native_app_window_gtk.h"

#include <gdk/gdkx.h>
#include <vector>

#include "base/message_loop/message_pump_gtk.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/gtk/extensions/extension_keybinding_registry_gtk.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/browser/ui/gtk/gtk_window_util.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "ui/base/x/active_window_watcher_x.h"
#include "ui/gfx/gtk_util.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/rect.h"

using apps::ShellWindow;

namespace {

// The timeout in milliseconds before we'll get the true window position with
// gtk_window_get_position() after the last GTK configure-event signal.
const int kDebounceTimeoutMilliseconds = 100;

const char* kAtomsToCache[] = {
  "_NET_WM_STATE",
  "_NET_WM_STATE_HIDDEN",
  NULL
};

} // namespace

NativeAppWindowGtk::NativeAppWindowGtk(ShellWindow* shell_window,
                                       const ShellWindow::CreateParams& params)
    : shell_window_(shell_window),
      window_(NULL),
      state_(GDK_WINDOW_STATE_WITHDRAWN),
      is_active_(false),
      content_thinks_its_fullscreen_(false),
      frameless_(params.frame == ShellWindow::FRAME_NONE),
      frame_cursor_(NULL),
      atom_cache_(base::MessagePumpGtk::GetDefaultXDisplay(), kAtomsToCache),
      is_x_event_listened_(false) {
  window_ = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));

  gfx::NativeView native_view =
      web_contents()->GetView()->GetNativeView();
  gtk_container_add(GTK_CONTAINER(window_), native_view);

  if (params.bounds.x() != INT_MIN && params.bounds.y() != INT_MIN)
    gtk_window_move(window_, params.bounds.x(), params.bounds.y());

  // This is done to avoid a WM "feature" where setting the window size to
  // the monitor size causes the WM to set the EWMH for full screen mode.
  int win_height = params.bounds.height();
  if (frameless_ &&
      gtk_window_util::BoundsMatchMonitorSize(window_, params.bounds)) {
    win_height -= 1;
  }
  gtk_window_set_default_size(window_, params.bounds.width(), win_height);

  resizable_ = params.resizable;
  if (!resizable_) {
    // If the window doesn't have a size request when we set resizable to
    // false, GTK will shrink the window to 1x1px.
    gtk_widget_set_size_request(GTK_WIDGET(window_),
        params.bounds.width(), win_height);
    gtk_window_set_resizable(window_, FALSE);
  }

  // make sure bounds_ and restored_bounds_ have correct values until we
  // get our first configure-event
  bounds_ = restored_bounds_ = params.bounds;
  gint x, y;
  gtk_window_get_position(window_, &x, &y);
  bounds_.set_origin(gfx::Point(x, y));

  // Hide titlebar when {frame: 'none'} specified on ShellWindow.
  if (frameless_)
    gtk_window_set_decorated(window_, false);

  int min_width = params.minimum_size.width();
  int min_height = params.minimum_size.height();
  int max_width = params.maximum_size.width();
  int max_height = params.maximum_size.height();
  GdkGeometry hints;
  int hints_mask = 0;
  if (min_width || min_height) {
    hints.min_height = min_height;
    hints.min_width = min_width;
    hints_mask |= GDK_HINT_MIN_SIZE;
  }
  if (max_width || max_height) {
    hints.max_height = max_height ? max_height : G_MAXINT;
    hints.max_width = max_width ? max_width : G_MAXINT;
    hints_mask |= GDK_HINT_MAX_SIZE;
  }
  if (hints_mask) {
    gtk_window_set_geometry_hints(
        window_,
        GTK_WIDGET(window_),
        &hints,
        static_cast<GdkWindowHints>(hints_mask));
  }

  // In some (older) versions of compiz, raising top-level windows when they
  // are partially off-screen causes them to get snapped back on screen, not
  // always even on the current virtual desktop.  If we are running under
  // compiz, suppress such raises, as they are not necessary in compiz anyway.
  if (ui::GuessWindowManager() == ui::WM_COMPIZ)
    suppress_window_raise_ = true;

  gtk_window_set_title(window_, extension()->name().c_str());

  std::string app_name = web_app::GenerateApplicationNameFromExtensionId(
      extension()->id());
  gtk_window_util::SetWindowCustomClass(window_,
      web_app::GetWMClassFromAppName(app_name));

  g_signal_connect(window_, "delete-event",
                   G_CALLBACK(OnMainWindowDeleteEventThunk), this);
  g_signal_connect(window_, "configure-event",
                   G_CALLBACK(OnConfigureThunk), this);
  g_signal_connect(window_, "window-state-event",
                   G_CALLBACK(OnWindowStateThunk), this);
  if (frameless_) {
    g_signal_connect(window_, "button-press-event",
                     G_CALLBACK(OnButtonPressThunk), this);
    g_signal_connect(window_, "motion-notify-event",
                     G_CALLBACK(OnMouseMoveEventThunk), this);
  }

  // If _NET_WM_STATE_HIDDEN is in _NET_SUPPORTED, listen for XEvent to work
  // around GTK+ not reporting minimization state changes. See comment in the
  // |OnXEvent|.
  std::vector< ::Atom> supported_atoms;
  if (ui::GetAtomArrayProperty(ui::GetX11RootWindow(),
                               "_NET_SUPPORTED",
                               &supported_atoms)) {
    if (std::find(supported_atoms.begin(),
                  supported_atoms.end(),
                  atom_cache_.GetAtom("_NET_WM_STATE_HIDDEN")) !=
        supported_atoms.end()) {
      GdkWindow* window = gtk_widget_get_window(GTK_WIDGET(window_));
      gdk_window_add_filter(window,
                            &NativeAppWindowGtk::OnXEventThunk,
                            this);
      is_x_event_listened_ = true;
    }
  }

  // Add the keybinding registry.
  extension_keybinding_registry_.reset(new ExtensionKeybindingRegistryGtk(
      shell_window_->profile(),
      window_,
      extensions::ExtensionKeybindingRegistry::PLATFORM_APPS_ONLY,
      shell_window_));

  ui::ActiveWindowWatcherX::AddObserver(this);
}

NativeAppWindowGtk::~NativeAppWindowGtk() {
  ui::ActiveWindowWatcherX::RemoveObserver(this);
  if (is_x_event_listened_) {
    gdk_window_remove_filter(NULL,
                             &NativeAppWindowGtk::OnXEventThunk,
                             this);
  }
}

bool NativeAppWindowGtk::IsActive() const {
  if (ui::ActiveWindowWatcherX::WMSupportsActivation())
    return is_active_;

  // This still works even though we don't get the activation notification.
  return gtk_window_is_active(window_);
}

bool NativeAppWindowGtk::IsMaximized() const {
  return (state_ & GDK_WINDOW_STATE_MAXIMIZED);
}

bool NativeAppWindowGtk::IsMinimized() const {
  return (state_ & GDK_WINDOW_STATE_ICONIFIED);
}

bool NativeAppWindowGtk::IsFullscreen() const {
  return (state_ & GDK_WINDOW_STATE_FULLSCREEN);
}

gfx::NativeWindow NativeAppWindowGtk::GetNativeWindow() {
  return window_;
}

gfx::Rect NativeAppWindowGtk::GetRestoredBounds() const {
  gfx::Rect window_bounds = restored_bounds_;
  window_bounds.Inset(-GetFrameInsets());
  return window_bounds;
}

ui::WindowShowState NativeAppWindowGtk::GetRestoredState() const {
  if (IsMaximized())
    return ui::SHOW_STATE_MAXIMIZED;
  return ui::SHOW_STATE_NORMAL;
}

gfx::Rect NativeAppWindowGtk::GetBounds() const {
  gfx::Rect window_bounds = bounds_;
  window_bounds.Inset(-GetFrameInsets());
  return window_bounds;
}

void NativeAppWindowGtk::Show() {
  gtk_window_present(window_);
}

void NativeAppWindowGtk::ShowInactive() {
  gtk_window_set_focus_on_map(window_, false);
  gtk_widget_show(GTK_WIDGET(window_));
}

void NativeAppWindowGtk::Hide() {
  gtk_widget_hide(GTK_WIDGET(window_));
}

void NativeAppWindowGtk::Close() {
  shell_window_->OnNativeWindowChanged();

  // Cancel any pending callback from the window configure debounce timer.
  window_configure_debounce_timer_.Stop();

  GtkWidget* window = GTK_WIDGET(window_);
  // To help catch bugs in any event handlers that might get fired during the
  // destruction, set window_ to NULL before any handlers will run.
  window_ = NULL;

  // OnNativeClose does a delete this so no other members should
  // be accessed after. gtk_widget_destroy is safe (and must
  // be last).
  shell_window_->OnNativeClose();
  gtk_widget_destroy(window);
}

void NativeAppWindowGtk::Activate() {
  gtk_window_present(window_);
}

void NativeAppWindowGtk::Deactivate() {
  gdk_window_lower(gtk_widget_get_window(GTK_WIDGET(window_)));
}

void NativeAppWindowGtk::Maximize() {
  // Represent the window first in order to keep the maximization behavior
  // consistency with Windows platform. Otherwise the window will be hidden if
  // it has been minimized.
  gtk_window_present(window_);
  gtk_window_maximize(window_);
}

void NativeAppWindowGtk::Minimize() {
  gtk_window_iconify(window_);
}

void NativeAppWindowGtk::Restore() {
  if (IsMaximized())
    gtk_window_unmaximize(window_);
  else if (IsMinimized())
    gtk_window_deiconify(window_);

  // Represent the window to keep restoration behavior consistency with Windows
  // platform.
  // TODO(zhchbin): verify whether we need this until http://crbug.com/261013 is
  // fixed.
  gtk_window_present(window_);
}

void NativeAppWindowGtk::SetBounds(const gfx::Rect& bounds) {
  gfx::Rect content_bounds = bounds;
  content_bounds.Inset(GetFrameInsets());
  gtk_window_move(window_, content_bounds.x(), content_bounds.y());
  if (!resizable_) {
    if (frameless_ &&
        gtk_window_util::BoundsMatchMonitorSize(window_, content_bounds)) {
      content_bounds.set_height(content_bounds.height() - 1);
    }
    // TODO(jeremya): set_size_request doesn't honor min/max size, so the
    // bounds should be constrained manually.
    gtk_widget_set_size_request(GTK_WIDGET(window_),
        content_bounds.width(), content_bounds.height());
  } else {
    gtk_window_util::SetWindowSize(window_,
        gfx::Size(bounds.width(), bounds.height()));
  }
}

GdkFilterReturn NativeAppWindowGtk::OnXEvent(GdkXEvent* gdk_x_event,
                                             GdkEvent* gdk_event) {
  // Work around GTK+ not reporting minimization state changes. Listen
  // for _NET_WM_STATE property changes and use _NET_WM_STATE_HIDDEN's
  // presence to set or clear the iconified bit if _NET_WM_STATE_HIDDEN
  // is supported. http://crbug.com/162794.
  XEvent* x_event = static_cast<XEvent*>(gdk_x_event);
  std::vector< ::Atom> atom_list;

  if (x_event->type == PropertyNotify &&
      x_event->xproperty.atom == atom_cache_.GetAtom("_NET_WM_STATE") &&
      ui::GetAtomArrayProperty(GDK_WINDOW_XWINDOW(GTK_WIDGET(window_)->window),
                               "_NET_WM_STATE",
                               &atom_list)) {
    std::vector< ::Atom>::iterator it =
        std::find(atom_list.begin(),
                  atom_list.end(),
                  atom_cache_.GetAtom("_NET_WM_STATE_HIDDEN"));
    state_ = (it != atom_list.end()) ? GDK_WINDOW_STATE_ICONIFIED :
        static_cast<GdkWindowState>(state_ & ~GDK_WINDOW_STATE_ICONIFIED);

    shell_window_->OnNativeWindowChanged();
  }

  return GDK_FILTER_CONTINUE;
}

void NativeAppWindowGtk::FlashFrame(bool flash) {
  gtk_window_set_urgency_hint(window_, flash);
}

bool NativeAppWindowGtk::IsAlwaysOnTop() const {
  return false;
}

void NativeAppWindowGtk::RenderViewHostChanged() {
  web_contents()->GetView()->Focus();
}

gfx::Insets NativeAppWindowGtk::GetFrameInsets() const {
  if (frameless_)
    return gfx::Insets();
  GdkWindow* gdk_window = gtk_widget_get_window(GTK_WIDGET(window_));
  if (!gdk_window)
    return gfx::Insets();

  gint current_width = 0;
  gint current_height = 0;
  gtk_window_get_size(window_, &current_width, &current_height);
  gint current_x = 0;
  gint current_y = 0;
  gdk_window_get_position(gdk_window, &current_x, &current_y);
  GdkRectangle rect_with_decorations = {0};
  gdk_window_get_frame_extents(gdk_window,
                               &rect_with_decorations);

  int left_inset = current_x - rect_with_decorations.x;
  int top_inset = current_y - rect_with_decorations.y;
  return gfx::Insets(
      top_inset,
      left_inset,
      rect_with_decorations.height - current_height - top_inset,
      rect_with_decorations.width - current_width - left_inset);
}

gfx::NativeView NativeAppWindowGtk::GetHostView() const {
  NOTIMPLEMENTED();
  return NULL;
}

gfx::Point NativeAppWindowGtk::GetDialogPosition(const gfx::Size& size) {
  gint current_width = 0;
  gint current_height = 0;
  gtk_window_get_size(window_, &current_width, &current_height);
  return gfx::Point(current_width / 2 - size.width() / 2,
                    current_height / 2 - size.height() / 2);
}

void NativeAppWindowGtk::AddObserver(
    web_modal::WebContentsModalDialogHostObserver* observer) {
  observer_list_.AddObserver(observer);
}

void NativeAppWindowGtk::RemoveObserver(
    web_modal::WebContentsModalDialogHostObserver* observer) {
  observer_list_.RemoveObserver(observer);
}

void NativeAppWindowGtk::ActiveWindowChanged(GdkWindow* active_window) {
  // Do nothing if we're in the process of closing the browser window.
  if (!window_)
    return;

  is_active_ = gtk_widget_get_window(GTK_WIDGET(window_)) == active_window;
  if (is_active_)
    shell_window_->OnNativeWindowActivated();
}

// Callback for the delete event.  This event is fired when the user tries to
// close the window (e.g., clicking on the X in the window manager title bar).
gboolean NativeAppWindowGtk::OnMainWindowDeleteEvent(GtkWidget* widget,
                                                     GdkEvent* event) {
  Close();

  // Return true to prevent the GTK window from being destroyed.  Close will
  // destroy it for us.
  return TRUE;
}

gboolean NativeAppWindowGtk::OnConfigure(GtkWidget* widget,
                                         GdkEventConfigure* event) {
  // We update |bounds_| but not |restored_bounds_| here.  The latter needs
  // to be updated conditionally when the window is non-maximized and non-
  // fullscreen, but whether those state updates have been processed yet is
  // window-manager specific.  We update |restored_bounds_| in the debounced
  // handler below, after the window state has been updated.
  bounds_.SetRect(event->x, event->y, event->width, event->height);

  // The GdkEventConfigure* we get here doesn't have quite the right
  // coordinates though (they're relative to the drawable window area, rather
  // than any window manager decorations, if enabled), so we need to call
  // gtk_window_get_position() to get the right values. (Otherwise session
  // restore, if enabled, will restore windows to incorrect positions.) That's
  // a round trip to the X server though, so we set a debounce timer and only
  // call it (in OnConfigureDebounced() below) after we haven't seen a
  // reconfigure event in a short while.
  // We don't use Reset() because the timer may not yet be running.
  // (In that case Stop() is a no-op.)
  window_configure_debounce_timer_.Stop();
  window_configure_debounce_timer_.Start(FROM_HERE,
      base::TimeDelta::FromMilliseconds(kDebounceTimeoutMilliseconds), this,
      &NativeAppWindowGtk::OnConfigureDebounced);

  return FALSE;
}

void NativeAppWindowGtk::OnConfigureDebounced() {
  gtk_window_util::UpdateWindowPosition(this, &bounds_, &restored_bounds_);
  shell_window_->OnNativeWindowChanged();

  FOR_EACH_OBSERVER(web_modal::WebContentsModalDialogHostObserver,
                    observer_list_,
                    OnPositionRequiresUpdate());

  // Fullscreen of non-resizable windows requires them to be made resizable
  // first. After that takes effect and OnConfigure is called we transition
  // to fullscreen.
  if (!IsFullscreen() && IsFullscreenOrPending()) {
    gtk_window_fullscreen(window_);
  }
}

gboolean NativeAppWindowGtk::OnWindowState(GtkWidget* sender,
                                           GdkEventWindowState* event) {
  state_ = event->new_window_state;

  if (content_thinks_its_fullscreen_ &&
      !(state_ & GDK_WINDOW_STATE_FULLSCREEN)) {
    content_thinks_its_fullscreen_ = false;
    content::RenderViewHost* rvh = web_contents()->GetRenderViewHost();
    if (rvh)
      rvh->ExitFullscreen();
  }

  return FALSE;
}

bool NativeAppWindowGtk::GetWindowEdge(int x, int y, GdkWindowEdge* edge) {
  if (!frameless_)
    return false;

  if (IsMaximized() || IsFullscreen())
    return false;

  return gtk_window_util::GetWindowEdge(bounds_.size(), 0, x, y, edge);
}

gboolean NativeAppWindowGtk::OnMouseMoveEvent(GtkWidget* widget,
                                              GdkEventMotion* event) {
  if (!frameless_) {
    // Reset the cursor.
    if (frame_cursor_) {
      frame_cursor_ = NULL;
      gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(window_)), NULL);
    }
    return FALSE;
  }

  if (!resizable_)
    return FALSE;

  // Update the cursor if we're on the custom frame border.
  GdkWindowEdge edge;
  bool has_hit_edge = GetWindowEdge(static_cast<int>(event->x),
                                    static_cast<int>(event->y), &edge);
  GdkCursorType new_cursor = GDK_LAST_CURSOR;
  if (has_hit_edge)
    new_cursor = gtk_window_util::GdkWindowEdgeToGdkCursorType(edge);

  GdkCursorType last_cursor = GDK_LAST_CURSOR;
  if (frame_cursor_)
    last_cursor = frame_cursor_->type;

  if (last_cursor != new_cursor) {
    frame_cursor_ = has_hit_edge ? gfx::GetCursor(new_cursor) : NULL;
    gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(window_)),
                          frame_cursor_);
  }
  return FALSE;
}

gboolean NativeAppWindowGtk::OnButtonPress(GtkWidget* widget,
                                           GdkEventButton* event) {
  DCHECK(frameless_);
  // Make the button press coordinate relative to the browser window.
  int win_x, win_y;
  GdkWindow* gdk_window = gtk_widget_get_window(GTK_WIDGET(window_));
  gdk_window_get_origin(gdk_window, &win_x, &win_y);

  GdkWindowEdge edge;
  gfx::Point point(static_cast<int>(event->x_root - win_x),
                   static_cast<int>(event->y_root - win_y));
  bool has_hit_edge = resizable_ && GetWindowEdge(point.x(), point.y(), &edge);
  bool has_hit_titlebar =
      draggable_region_ && draggable_region_->contains(event->x, event->y);

  if (event->button == 1) {
    if (GDK_BUTTON_PRESS == event->type) {
      // Raise the window after a click on either the titlebar or the border to
      // match the behavior of most window managers, unless that behavior has
      // been suppressed.
      if ((has_hit_titlebar || has_hit_edge) && !suppress_window_raise_)
        gdk_window_raise(GTK_WIDGET(widget)->window);

      if (has_hit_edge) {
        gtk_window_begin_resize_drag(window_, edge, event->button,
                                     static_cast<gint>(event->x_root),
                                     static_cast<gint>(event->y_root),
                                     event->time);
        return TRUE;
      } else if (has_hit_titlebar) {
        return gtk_window_util::HandleTitleBarLeftMousePress(
            window_, bounds_, event);
      }
    } else if (GDK_2BUTTON_PRESS == event->type) {
      if (has_hit_titlebar && resizable_) {
        // Maximize/restore on double click.
        if (IsMaximized()) {
          gtk_window_util::UnMaximize(GTK_WINDOW(widget),
              bounds_, restored_bounds_);
        } else {
          gtk_window_maximize(window_);
        }
        return TRUE;
      }
    }
  } else if (event->button == 2) {
    if (has_hit_titlebar || has_hit_edge)
      gdk_window_lower(gdk_window);
    return TRUE;
  }

  return FALSE;
}

void NativeAppWindowGtk::SetFullscreen(bool fullscreen) {
  content_thinks_its_fullscreen_ = fullscreen;
  if (fullscreen){
    if (resizable_) {
      gtk_window_fullscreen(window_);
    } else {
      // We must first make the window resizable. That won't take effect
      // immediately, so OnConfigureDebounced completes the fullscreen call.
      gtk_window_set_resizable(window_, TRUE);
    }
  } else {
    gtk_window_unfullscreen(window_);
    if (!resizable_)
      gtk_window_set_resizable(window_, FALSE);
  }
}

bool NativeAppWindowGtk::IsFullscreenOrPending() const {
  return content_thinks_its_fullscreen_;
}

bool NativeAppWindowGtk::IsDetached() const {
  return false;
}

void NativeAppWindowGtk::UpdateWindowIcon() {
  Profile* profile = shell_window_->profile();
  gfx::Image app_icon = shell_window_->app_icon();
  if (!app_icon.IsEmpty())
    gtk_util::SetWindowIcon(window_, profile, app_icon.ToGdkPixbuf());
  else
    gtk_util::SetWindowIcon(window_, profile);
}

void NativeAppWindowGtk::UpdateWindowTitle() {
  string16 title = shell_window_->GetTitle();
  gtk_window_set_title(window_, UTF16ToUTF8(title).c_str());
}

void NativeAppWindowGtk::HandleKeyboardEvent(
    const content::NativeWebKeyboardEvent& event) {
  // No-op.
}

void NativeAppWindowGtk::UpdateDraggableRegions(
    const std::vector<extensions::DraggableRegion>& regions) {
  // Draggable region is not supported for non-frameless window.
  if (!frameless_)
    return;

  draggable_region_.reset(ShellWindow::RawDraggableRegionsToSkRegion(regions));
}
