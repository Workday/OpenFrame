// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/screen_capture_notification_ui.h"

#include <gtk/gtk.h>
#include <math.h>

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "grit/generated_resources.h"
#include "ui/base/gtk/gtk_signal.h"
#include "ui/base/l10n/l10n_util.h"

class ScreenCaptureNotificationUIGtk : public ScreenCaptureNotificationUI {
 public:
  explicit ScreenCaptureNotificationUIGtk(const string16& text);
  virtual ~ScreenCaptureNotificationUIGtk();

  // ScreenCaptureNotificationUI interface
  virtual void OnStarted(const base::Closure& stop_callback) OVERRIDE;

 private:
  CHROMEGTK_CALLBACK_1(ScreenCaptureNotificationUIGtk, gboolean, OnDelete,
                       GdkEvent*);
  CHROMEGTK_CALLBACK_0(ScreenCaptureNotificationUIGtk, void, OnClicked);
  CHROMEGTK_CALLBACK_1(ScreenCaptureNotificationUIGtk, gboolean, OnConfigure,
                       GdkEventConfigure*);
  CHROMEGTK_CALLBACK_1(ScreenCaptureNotificationUIGtk, gboolean, OnButtonPress,
                       GdkEventButton*);

  void CreateWindow();
  void HideWindow();

  const std::string text_;

  base::Closure stop_callback_;
  GtkWidget* window_;
  GtkWidget* message_;
  GtkWidget* button_;

  // Used to distinguish resize events from other types of "configure-event"
  // notifications.
  int current_width_;
  int current_height_;

  DISALLOW_COPY_AND_ASSIGN(ScreenCaptureNotificationUIGtk);
};

ScreenCaptureNotificationUIGtk::ScreenCaptureNotificationUIGtk(
    const string16& text)
    : text_(UTF16ToUTF8(text)),
      window_(NULL),
      current_width_(0),
      current_height_(0) {
}

ScreenCaptureNotificationUIGtk::~ScreenCaptureNotificationUIGtk() {
  HideWindow();
}

void ScreenCaptureNotificationUIGtk::CreateWindow() {
  if (window_)
    return;

  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  GtkWindow* window = GTK_WINDOW(window_);

  g_signal_connect(window_, "delete-event", G_CALLBACK(OnDeleteThunk), this);
  gtk_window_set_title(window, text_.c_str());
  gtk_window_set_resizable(window, FALSE);

  // Try to keep the window always visible.
  gtk_window_stick(window);
  gtk_window_set_keep_above(window, TRUE);

  // Remove window titlebar.
  gtk_window_set_decorated(window, FALSE);

  // In case the titlebar is still there, try to remove some of the buttons.
  // Utility windows have no minimize button or taskbar presence.
  gtk_window_set_type_hint(window, GDK_WINDOW_TYPE_HINT_UTILITY);
  gtk_window_set_deletable(window, FALSE);

  // Allow custom rendering of the background pixmap.
  gtk_widget_set_app_paintable(window_, TRUE);

  // Handle window resizing, to regenerate the background pixmap and window
  // shape bitmap.  The stored width & height need to be initialized here
  // in case the window is created a second time (the size of the previous
  // window would be remembered, preventing the generation of bitmaps for the
  // new window).
  current_height_ = current_width_ = 0;
  g_signal_connect(window_, "configure-event",
                   G_CALLBACK(OnConfigureThunk), this);

  // Handle mouse events to allow the user to drag the window around.
  gtk_widget_set_events(window_, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(window_, "button-press-event",
                   G_CALLBACK(OnButtonPressThunk), this);

  // All magic numbers taken from screen shots provided by UX.
  // The alignment sets narrow margins at the top and bottom, compared with
  // left and right.  The left margin is made larger to accommodate the
  // window movement gripper.
  GtkWidget* align = gtk_alignment_new(0, 0, 1, 1);
  gtk_alignment_set_padding(GTK_ALIGNMENT(align), 8, 8, 24, 12);
  gtk_container_add(GTK_CONTAINER(window), align);

  GtkWidget* button_row = gtk_hbox_new(FALSE, 12);
  gtk_container_add(GTK_CONTAINER(align), button_row);

  std::string button_label =
      l10n_util::GetStringUTF8(IDS_MEDIA_SCREEN_CAPTURE_NOTIFICATION_STOP);
  button_ = gtk_button_new_with_label(button_label.c_str());
  gtk_box_pack_end(GTK_BOX(button_row), button_, FALSE, FALSE, 0);

  g_signal_connect(button_, "clicked", G_CALLBACK(OnClickedThunk), this);

  message_ = gtk_label_new(NULL);
  gtk_box_pack_end(GTK_BOX(button_row), message_, FALSE, FALSE, 0);

  // Override any theme setting for the text color, so that the text is
  // readable against the window's background pixmap.
  PangoAttrList* attributes = pango_attr_list_new();
  PangoAttribute* text_color = pango_attr_foreground_new(0, 0, 0);
  pango_attr_list_insert(attributes, text_color);
  gtk_label_set_attributes(GTK_LABEL(message_), attributes);
  pango_attr_list_unref(attributes);

  gtk_label_set_text(GTK_LABEL(message_), text_.c_str());

  gtk_widget_show_all(window_);
  gtk_window_present(GTK_WINDOW(window_));
}

void ScreenCaptureNotificationUIGtk::OnStarted(
    const base::Closure& stop_callback) {
  DCHECK(stop_callback_.is_null());
  DCHECK(!stop_callback.is_null());
  DCHECK(!window_);

  stop_callback_ = stop_callback;
  CreateWindow();
}

void ScreenCaptureNotificationUIGtk::HideWindow() {
  if (window_) {
    gtk_widget_destroy(window_);
    window_ = NULL;
  }

  stop_callback_.Reset();
}

void ScreenCaptureNotificationUIGtk::OnClicked(GtkWidget* button) {
  stop_callback_.Run();
  HideWindow();
}

gboolean ScreenCaptureNotificationUIGtk::OnDelete(
    GtkWidget* window, GdkEvent* event) {
  stop_callback_.Run();
  HideWindow();

  return TRUE;
}

namespace {
// Helper function for creating a rectangular path with rounded corners, as
// Cairo doesn't have this facility.  |radius| is the arc-radius of each
// corner.  The bounding rectangle extends from (0, 0) to (width, height).
void AddRoundRectPath(cairo_t* cairo_context, int width, int height,
                      int radius) {
  cairo_new_sub_path(cairo_context);
  cairo_arc(cairo_context, width - radius, radius, radius, -M_PI_2, 0);
  cairo_arc(cairo_context, width - radius, height - radius, radius, 0, M_PI_2);
  cairo_arc(cairo_context, radius, height - radius, radius, M_PI_2, 2 * M_PI_2);
  cairo_arc(cairo_context, radius, radius, radius, 2 * M_PI_2, 3 * M_PI_2);
  cairo_close_path(cairo_context);
}

}  // namespace

gboolean ScreenCaptureNotificationUIGtk::OnConfigure(GtkWidget* widget,
                                                     GdkEventConfigure* event) {
  // Only generate bitmaps if the size has actually changed.
  if (event->width == current_width_ && event->height == current_height_)
    return FALSE;

  current_width_ = event->width;
  current_height_ = event->height;

  // Create the depth 1 pixmap for the window shape.
  GdkPixmap* shape_mask =
      gdk_pixmap_new(NULL, current_width_, current_height_, 1);
  cairo_t* cairo_context = gdk_cairo_create(shape_mask);

  // Set the arc radius for the corners.
  const int kCornerRadius = 6;

  // Initialize the whole bitmap to be transparent.
  cairo_set_source_rgba(cairo_context, 0, 0, 0, 0);
  cairo_set_operator(cairo_context, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cairo_context);

  // Paint an opaque round rect covering the whole area (leaving the extreme
  // corners transparent).
  cairo_set_source_rgba(cairo_context, 1, 1, 1, 1);
  cairo_set_operator(cairo_context, CAIRO_OPERATOR_SOURCE);
  AddRoundRectPath(cairo_context, current_width_, current_height_,
                   kCornerRadius);
  cairo_fill(cairo_context);

  cairo_destroy(cairo_context);
  gdk_window_shape_combine_mask(widget->window, shape_mask, 0, 0);
  g_object_unref(shape_mask);

  // Create a full-color pixmap for the window background image.
  GdkPixmap* background =
      gdk_pixmap_new(NULL, current_width_, current_height_, 24);
  cairo_context = gdk_cairo_create(background);

  // Paint the whole bitmap one color.
  cairo_set_source_rgb(cairo_context, 0.91, 0.91, 0.91);
  cairo_paint(cairo_context);

  // Paint the round-rectangle edge.
  cairo_set_source_rgb(cairo_context, 0.13, 0.69, 0.11);
  cairo_set_line_width(cairo_context, 6);
  AddRoundRectPath(cairo_context, current_width_, current_height_,
                   kCornerRadius);
  cairo_stroke(cairo_context);

  // Render the window-gripper.  In order for a straight line to light up
  // single pixels, Cairo requires the coordinates to have fractional
  // components of 0.5 (so the "/ 2" is a deliberate integer division).
  double gripper_top = current_height_ / 2 - 10.5;
  double gripper_bottom = current_height_ / 2 + 10.5;
  cairo_set_line_width(cairo_context, 1);

  double x = 12.5;
  cairo_set_source_rgb(cairo_context, 0.70, 0.70, 0.70);
  cairo_move_to(cairo_context, x, gripper_top);
  cairo_line_to(cairo_context, x, gripper_bottom);
  cairo_stroke(cairo_context);
  x += 3;
  cairo_move_to(cairo_context, x, gripper_top);
  cairo_line_to(cairo_context, x, gripper_bottom);
  cairo_stroke(cairo_context);

  x -= 2;
  cairo_set_source_rgb(cairo_context, 0.97, 0.97, 0.97);
  cairo_move_to(cairo_context, x, gripper_top);
  cairo_line_to(cairo_context, x, gripper_bottom);
  cairo_stroke(cairo_context);
  x += 3;
  cairo_move_to(cairo_context, x, gripper_top);
  cairo_line_to(cairo_context, x, gripper_bottom);
  cairo_stroke(cairo_context);

  cairo_destroy(cairo_context);

  gdk_window_set_back_pixmap(widget->window, background, FALSE);
  g_object_unref(background);
  gdk_window_invalidate_rect(widget->window, NULL, TRUE);

  return FALSE;
}

gboolean ScreenCaptureNotificationUIGtk::OnButtonPress(GtkWidget* widget,
                                              GdkEventButton* event) {
  gtk_window_begin_move_drag(GTK_WINDOW(window_),
                             event->button,
                             event->x_root,
                             event->y_root,
                             event->time);
  return FALSE;
}

scoped_ptr<ScreenCaptureNotificationUI> ScreenCaptureNotificationUI::Create(
    const string16& text) {
  return scoped_ptr<ScreenCaptureNotificationUI>(
      new ScreenCaptureNotificationUIGtk(text));
}
