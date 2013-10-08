// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/autofill/autofill_popup_view_gtk.h"

#include <gdk/gdkkeysyms.h>

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/autofill/autofill_popup_controller.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "grit/ui_resources.h"
#include "third_party/WebKit/public/web/WebAutofillClient.h"
#include "ui/base/gtk/gtk_compat.h"
#include "ui/base/gtk/gtk_hig_constants.h"
#include "ui/base/gtk/gtk_windowing.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/pango_util.h"
#include "ui/gfx/rect.h"

using WebKit::WebAutofillClient;

namespace {

const GdkColor kBorderColor = GDK_COLOR_RGB(0xc7, 0xca, 0xce);
const GdkColor kHoveredBackgroundColor = GDK_COLOR_RGB(0xcd, 0xcd, 0xcd);
const GdkColor kNameColor = GDK_COLOR_RGB(0x00, 0x00, 0x00);
const GdkColor kWarningColor = GDK_COLOR_RGB(0x7f, 0x7f, 0x7f);
const GdkColor kSubtextColor = GDK_COLOR_RGB(0x7f, 0x7f, 0x7f);

}  // namespace

namespace autofill {

AutofillPopupViewGtk::AutofillPopupViewGtk(
    AutofillPopupController* controller)
    : controller_(controller),
      window_(gtk_window_new(GTK_WINDOW_POPUP)) {
  gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);
  gtk_widget_set_app_paintable(window_, TRUE);
  gtk_widget_set_double_buffered(window_, TRUE);

  // Setup the window to ensure it receives the expose event.
  gtk_widget_add_events(window_, GDK_BUTTON_MOTION_MASK |
                                 GDK_BUTTON_RELEASE_MASK |
                                 GDK_EXPOSURE_MASK |
                                 GDK_POINTER_MOTION_MASK);

  GtkWidget* toplevel_window = gtk_widget_get_toplevel(
      controller->container_view());
  signals_.Connect(toplevel_window, "configure-event",
                   G_CALLBACK(HandleConfigureThunk), this);
  g_signal_connect(window_, "expose-event",
                   G_CALLBACK(HandleExposeThunk), this);
  g_signal_connect(window_, "leave-notify-event",
                   G_CALLBACK(HandleLeaveThunk), this);
  g_signal_connect(window_, "motion-notify-event",
                   G_CALLBACK(HandleMotionThunk), this);
  g_signal_connect(window_, "button-release-event",
                   G_CALLBACK(HandleButtonReleaseThunk), this);

  // Cache the layout so we don't have to create it for every expose.
  layout_ = gtk_widget_create_pango_layout(window_, NULL);
}

AutofillPopupViewGtk::~AutofillPopupViewGtk() {
  g_object_unref(layout_);
  gtk_widget_destroy(window_);
}

void AutofillPopupViewGtk::Hide() {
  delete this;
}

void AutofillPopupViewGtk::Show() {
  UpdateBoundsAndRedrawPopup();

  gtk_widget_show(window_);

  GtkWidget* parent_window =
      gtk_widget_get_toplevel(controller_->container_view());
  ui::StackPopupWindow(window_, parent_window);
}

void AutofillPopupViewGtk::InvalidateRow(size_t row) {
  GdkRectangle row_rect = controller_->GetRowBounds(row).ToGdkRectangle();
  GdkWindow* gdk_window = gtk_widget_get_window(window_);
  gdk_window_invalidate_rect(gdk_window, &row_rect, FALSE);
}

void AutofillPopupViewGtk::UpdateBoundsAndRedrawPopup() {
  gtk_widget_set_size_request(window_,
                              controller_->popup_bounds().width(),
                              controller_->popup_bounds().height());
  gtk_window_move(GTK_WINDOW(window_),
                  controller_->popup_bounds().x(),
                  controller_->popup_bounds().y());

  GdkWindow* gdk_window = gtk_widget_get_window(window_);
  GdkRectangle popup_rect = controller_->popup_bounds().ToGdkRectangle();
  if (gdk_window != NULL)
    gdk_window_invalidate_rect(gdk_window, &popup_rect, FALSE);
}

gboolean AutofillPopupViewGtk::HandleConfigure(GtkWidget* widget,
                                               GdkEventConfigure* event) {
  controller_->Hide();
  return FALSE;
}

gboolean AutofillPopupViewGtk::HandleButtonRelease(GtkWidget* widget,
                                                   GdkEventButton* event) {
  // We only care about the left click.
  if (event->button != 1)
    return FALSE;

  controller_->MouseClicked(event->x, event->y);
  return TRUE;
}

gboolean AutofillPopupViewGtk::HandleExpose(GtkWidget* widget,
                                            GdkEventExpose* event) {
  cairo_t* cr = gdk_cairo_create(GDK_DRAWABLE(gtk_widget_get_window(widget)));
  gdk_cairo_rectangle(cr, &event->area);
  cairo_clip(cr);

  // This assert is kinda ugly, but it would be more currently unneeded work
  // to support painting a border that isn't 1 pixel thick.  There is no point
  // in writing that code now, and explode if that day ever comes.
  DCHECK_EQ(1, kBorderThickness);
  // Draw the 1px border around the entire window.
  gdk_cairo_set_source_color(cr, &kBorderColor);
  gdk_cairo_rectangle(cr, &widget->allocation);
  cairo_stroke(cr);
  SetUpLayout();

  gfx::Rect damage_rect(event->area);

  for (size_t i = 0; i < controller_->names().size(); ++i) {
    gfx::Rect line_rect = controller_->GetRowBounds(i);
    // Only repaint and layout damaged lines.
    if (!line_rect.Intersects(damage_rect))
      continue;

    if (controller_->identifiers()[i] == WebAutofillClient::MenuItemIDSeparator)
      DrawSeparator(cr, line_rect);
    else
      DrawAutofillEntry(cr, i, line_rect);
  }

  cairo_destroy(cr);

  return TRUE;
}

gboolean AutofillPopupViewGtk::HandleLeave(GtkWidget* widget,
                                           GdkEventCrossing* event) {
  controller_->MouseExitedPopup();

  return FALSE;
}

gboolean AutofillPopupViewGtk::HandleMotion(GtkWidget* widget,
                                            GdkEventMotion* event) {
  controller_->MouseHovered(event->x, event->y);

  return TRUE;
}

void AutofillPopupViewGtk::SetUpLayout() {
  pango_layout_set_width(layout_, window_->allocation.width * PANGO_SCALE);
  pango_layout_set_height(layout_, window_->allocation.height * PANGO_SCALE);
}

void AutofillPopupViewGtk::SetLayoutText(const string16& text,
                                         const gfx::Font& font,
                                         const GdkColor text_color) {
  PangoAttrList* attrs = pango_attr_list_new();

  PangoAttribute* fg_attr = pango_attr_foreground_new(text_color.red,
                                                      text_color.green,
                                                      text_color.blue);
  pango_attr_list_insert(attrs, fg_attr);  // Ownership taken.

  pango_layout_set_attributes(layout_, attrs);  // Ref taken.
  pango_attr_list_unref(attrs);

  gfx::ScopedPangoFontDescription font_description(font.GetNativeFont());
  pango_layout_set_font_description(layout_, font_description.get());

  gtk_util::SetLayoutText(layout_, text);

  // The popup is already the correct size for the text, so set the width to -1
  // to prevent additional wrapping or ellipsization.
  pango_layout_set_width(layout_, -1);
}

void AutofillPopupViewGtk::DrawSeparator(cairo_t* cairo_context,
                                         const gfx::Rect& separator_rect) {
  cairo_save(cairo_context);
  cairo_move_to(cairo_context, 0, separator_rect.y());
  cairo_line_to(cairo_context,
                separator_rect.width(),
                separator_rect.y() + separator_rect.height());
  cairo_stroke(cairo_context);
  cairo_restore(cairo_context);
}

void AutofillPopupViewGtk::DrawAutofillEntry(cairo_t* cairo_context,
                                             size_t index,
                                             const gfx::Rect& entry_rect) {
  if (controller_->selected_line() == static_cast<int>(index)) {
    gdk_cairo_set_source_color(cairo_context, &kHoveredBackgroundColor);
    cairo_rectangle(cairo_context, entry_rect.x(), entry_rect.y(),
                    entry_rect.width(), entry_rect.height());
    cairo_fill(cairo_context);
  }

  // Draw the value.
  SetLayoutText(controller_->names()[index],
                controller_->GetNameFontForRow(index),
                controller_->IsWarning(index) ? kWarningColor : kNameColor);
  int value_text_width = controller_->GetNameFontForRow(index).GetStringWidth(
      controller_->names()[index]);

  // Center the text within the line.
  int row_height = entry_rect.height();
  int value_content_y = std::max(
      entry_rect.y(),
      entry_rect.y() +
          (row_height - controller_->GetNameFontForRow(index).GetHeight()) / 2);

  bool is_rtl = controller_->IsRTL();
  int value_content_x = is_rtl ?
      entry_rect.width() - value_text_width - kEndPadding : kEndPadding;

  cairo_save(cairo_context);
  cairo_move_to(cairo_context, value_content_x, value_content_y);
  pango_cairo_show_layout(cairo_context, layout_);
  cairo_restore(cairo_context);

  // Use this to figure out where all the other Autofill items should be placed.
  int x_align_left = is_rtl ? kEndPadding : entry_rect.width() - kEndPadding;

  // Draw the Autofill icon, if one exists
  if (!controller_->icons()[index].empty()) {
    int icon = controller_->GetIconResourceID(controller_->icons()[index]);
    DCHECK_NE(-1, icon);
    int icon_y = entry_rect.y() + (row_height - kAutofillIconHeight) / 2;

    x_align_left += is_rtl ? 0 : -kAutofillIconWidth;

    cairo_save(cairo_context);
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    gtk_util::DrawFullImage(cairo_context,
                            window_,
                            rb.GetImageNamed(icon),
                            x_align_left,
                            icon_y);
    cairo_restore(cairo_context);

    x_align_left += is_rtl ? kAutofillIconWidth + kIconPadding : -kIconPadding;
  }

  // Draw the subtext.
  SetLayoutText(controller_->subtexts()[index],
                controller_->subtext_font(),
                kSubtextColor);
  if (!is_rtl) {
    x_align_left -= controller_->subtext_font().GetStringWidth(
        controller_->subtexts()[index]);
  }

  // Center the text within the line.
  int subtext_content_y = std::max(
      entry_rect.y(),
      entry_rect.y() +
          (row_height - controller_->subtext_font().GetHeight()) / 2);

  cairo_save(cairo_context);
  cairo_move_to(cairo_context, x_align_left, subtext_content_y);
  pango_cairo_show_layout(cairo_context, layout_);
  cairo_restore(cairo_context);
}

AutofillPopupView* AutofillPopupView::Create(
    AutofillPopupController* controller) {
  return new AutofillPopupViewGtk(controller);
}

}  // namespace autofill
