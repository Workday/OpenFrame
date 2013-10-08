// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/omnibox/omnibox_popup_view_gtk.h"

#include <gtk/gtk.h>

#include <algorithm>
#include <string>

#include "base/basictypes.h"
#include "base/i18n/rtl.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/autocomplete/autocomplete_result.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_model.h"
#include "chrome/browser/ui/omnibox/omnibox_popup_model.h"
#include "chrome/browser/ui/omnibox/omnibox_view.h"
#include "content/public/browser/notification_source.h"
#include "grit/theme_resources.h"
#include "ui/base/gtk/gtk_compat.h"
#include "ui/base/gtk/gtk_hig_constants.h"
#include "ui/base/gtk/gtk_screen_util.h"
#include "ui/base/gtk/gtk_signal_registrar.h"
#include "ui/base/gtk/gtk_windowing.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font.h"
#include "ui/gfx/gtk_util.h"
#include "ui/gfx/image/cairo_cached_surface.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/skia_utils_gtk.h"

namespace {

const GdkColor kBorderColor = GDK_COLOR_RGB(0xc7, 0xca, 0xce);
const GdkColor kBackgroundColor = GDK_COLOR_RGB(0xff, 0xff, 0xff);
const GdkColor kSelectedBackgroundColor = GDK_COLOR_RGB(0xdf, 0xe6, 0xf6);
const GdkColor kHoveredBackgroundColor = GDK_COLOR_RGB(0xef, 0xf2, 0xfa);

const GdkColor kContentTextColor = GDK_COLOR_RGB(0x00, 0x00, 0x00);
const GdkColor kURLTextColor = GDK_COLOR_RGB(0x00, 0x88, 0x00);

// We have a 1 pixel border around the entire results popup.
const int kBorderThickness = 1;

// The vertical height of each result.
const int kHeightPerResult = 24;

// Width of the icons.
const int kIconWidth = 17;

// We want to vertically center the image in the result space.
const int kIconTopPadding = 2;

// Space between the left edge (including the border) and the text.
const int kIconLeftPadding = 3 + kBorderThickness;

// Space between the image and the text.
const int kIconRightPadding = 5;

// Space between the left edge (including the border) and the text.
const int kIconAreaWidth =
    kIconLeftPadding + kIconWidth + kIconRightPadding;

// Space between the right edge (including the border) and the text.
const int kRightPadding = 3;

// When we have both a content and description string, we don't want the
// content to push the description off.  Limit the content to a percentage of
// the total width.
const float kContentWidthPercentage = 0.7;

// How much to offset the popup from the bottom of the location bar.
const int kVerticalOffset = 3;

// The size delta between the font used for the edit and the result rows. Passed
// to gfx::Font::DeriveFont.
const int kEditFontAdjust = -1;

// UTF-8 Left-to-right embedding.
const char* kLRE = "\xe2\x80\xaa";

// Return a Rect covering the whole area of |window|.
gfx::Rect GetWindowRect(GdkWindow* window) {
  gint width = gdk_window_get_width(window);
  gint height = gdk_window_get_height(window);
  return gfx::Rect(width, height);
}

// Return a Rect for the space for a result line.  This excludes the border,
// but includes the padding.  This is the area that is colored for a selection.
gfx::Rect GetRectForLine(size_t line, int width) {
  return gfx::Rect(kBorderThickness,
                   (line * kHeightPerResult) + kBorderThickness,
                   width - (kBorderThickness * 2),
                   kHeightPerResult);
}

// TODO(deanm): Find some better home for this, and make it more efficient.
size_t GetUTF8Offset(const string16& text, size_t text_offset) {
  return UTF16ToUTF8(text.substr(0, text_offset)).length();
}

// Generates the normal URL color, a green color used in unhighlighted URL
// text. It is a mix of |kURLTextColor| and the current text color.  Unlike the
// selected text color, it is more important to match the qualities of the
// foreground typeface color instead of taking the background into account.
GdkColor NormalURLColor(GdkColor foreground) {
  color_utils::HSL fg_hsl;
  color_utils::SkColorToHSL(gfx::GdkColorToSkColor(foreground), &fg_hsl);

  color_utils::HSL hue_hsl;
  color_utils::SkColorToHSL(gfx::GdkColorToSkColor(kURLTextColor), &hue_hsl);

  // Only allow colors that have a fair amount of saturation in them (color vs
  // white). This means that our output color will always be fairly green.
  double s = std::max(0.5, fg_hsl.s);

  // Make sure the luminance is at least as bright as the |kURLTextColor| green
  // would be if we were to use that.
  double l;
  if (fg_hsl.l < hue_hsl.l)
    l = hue_hsl.l;
  else
    l = (fg_hsl.l + hue_hsl.l) / 2;

  color_utils::HSL output = { hue_hsl.h, s, l };
  return gfx::SkColorToGdkColor(color_utils::HSLToSkColor(output, 255));
}

// Generates the selected URL color, a green color used on URL text in the
// currently highlighted entry in the autocomplete popup. It's a mix of
// |kURLTextColor|, the current text color, and the background color (the
// select highlight). It is more important to contrast with the background
// saturation than to look exactly like the foreground color.
GdkColor SelectedURLColor(GdkColor foreground, GdkColor background) {
  color_utils::HSL fg_hsl;
  color_utils::SkColorToHSL(gfx::GdkColorToSkColor(foreground), &fg_hsl);

  color_utils::HSL bg_hsl;
  color_utils::SkColorToHSL(gfx::GdkColorToSkColor(background), &bg_hsl);

  color_utils::HSL hue_hsl;
  color_utils::SkColorToHSL(gfx::GdkColorToSkColor(kURLTextColor), &hue_hsl);

  // The saturation of the text should be opposite of the background, clamped
  // to 0.2-0.8. We make sure it's greater than 0.2 so there's some color, but
  // less than 0.8 so it's not the oversaturated neon-color.
  double opposite_s = 1 - bg_hsl.s;
  double s = std::max(0.2, std::min(0.8, opposite_s));

  // The luminance should match the luminance of the foreground text.  Again,
  // we clamp so as to have at some amount of color (green) in the text.
  double opposite_l = fg_hsl.l;
  double l = std::max(0.1, std::min(0.9, opposite_l));

  color_utils::HSL output = { hue_hsl.h, s, l };
  return gfx::SkColorToGdkColor(color_utils::HSLToSkColor(output, 255));
}
}  // namespace

void OmniboxPopupViewGtk::SetupLayoutForMatch(
    PangoLayout* layout,
    const string16& text,
    const AutocompleteMatch::ACMatchClassifications& classifications,
    const GdkColor* base_color,
    const GdkColor* dim_color,
    const GdkColor* url_color,
    const std::string& prefix_text) {
  // In RTL, mark text with left-to-right embedding mark if there is no strong
  // RTL characters inside it, so the ending punctuation displays correctly
  // and the eliding ellipsis displays correctly. We only mark the text with
  // LRE. Wrapping it with LRE and PDF by calling AdjustStringForLocaleDirection
  // or WrapStringWithLTRFormatting will render the elllipsis at the left of the
  // elided pure LTR text.
  bool marked_with_lre = false;
  string16 localized_text = text;
  // Pango is really easy to overflow and send into a computational death
  // spiral that can corrupt the screen. Assume that we'll never have more than
  // 2000 characters, which should be a safe assumption until we all get robot
  // eyes. http://crbug.com/66576
  if (localized_text.length() > 2000)
    localized_text = localized_text.substr(0, 2000);
  bool is_rtl = base::i18n::IsRTL();
  if (is_rtl && !base::i18n::StringContainsStrongRTLChars(localized_text)) {
    localized_text.insert(0, 1, base::i18n::kLeftToRightEmbeddingMark);
    marked_with_lre = true;
  }

  // We can have a prefix, or insert additional characters while processing the
  // classifications.  We need to take this in to account when we translate the
  // UTF-16 offsets in the classification into text_utf8 byte offsets.
  size_t additional_offset = prefix_text.length();  // Length in utf-8 bytes.
  std::string text_utf8 = prefix_text + UTF16ToUTF8(localized_text);

  PangoAttrList* attrs = pango_attr_list_new();

  // TODO(deanm): This is a hack, just to handle coloring prefix_text.
  // Hopefully I can clean up the match situation a bit and this will
  // come out cleaner.  For now, apply the base color to the whole text
  // so that our prefix will have the base color applied.
  PangoAttribute* base_fg_attr = pango_attr_foreground_new(
      base_color->red, base_color->green, base_color->blue);
  pango_attr_list_insert(attrs, base_fg_attr);  // Ownership taken.

  // Walk through the classifications, they are linear, in order, and should
  // cover the entire text.  We create a bunch of overlapping attributes,
  // extending from the offset to the end of the string.  The ones created
  // later will override the previous ones, meaning we will still setup each
  // portion correctly, we just don't need to compute the end offset.
  for (ACMatchClassifications::const_iterator i = classifications.begin();
       i != classifications.end(); ++i) {
    size_t offset = GetUTF8Offset(localized_text, i->offset) +
                    additional_offset;

    // TODO(deanm): All the colors should probably blend based on whether this
    // result is selected or not.  This would include the green URLs.  Right
    // now the caller is left to blend only the base color.  Do we need to
    // handle things like DIM urls?  Turns out DIM means something different
    // than you'd think, all of the description text is not DIM, it is a
    // special case that is not very common, but we should figure out and
    // support it.
    const GdkColor* color = base_color;
    if (i->style & ACMatchClassification::URL) {
      color = url_color;
      // Insert a left to right embedding to make sure that URLs are shown LTR.
      if (is_rtl && !marked_with_lre) {
        std::string lre(kLRE);
        text_utf8.insert(offset, lre);
        additional_offset += lre.length();
      }
    }

    if (i->style & ACMatchClassification::DIM)
      color = dim_color;

    PangoAttribute* fg_attr = pango_attr_foreground_new(
        color->red, color->green, color->blue);
    fg_attr->start_index = offset;
    pango_attr_list_insert(attrs, fg_attr);  // Ownership taken.

    // Matched portions are bold, otherwise use the normal weight.
    PangoWeight weight = (i->style & ACMatchClassification::MATCH) ?
        PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL;
    PangoAttribute* weight_attr = pango_attr_weight_new(weight);
    weight_attr->start_index = offset;
    pango_attr_list_insert(attrs, weight_attr);  // Ownership taken.
  }

  pango_layout_set_text(layout, text_utf8.data(), text_utf8.length());
  pango_layout_set_attributes(layout, attrs);  // Ref taken.
  pango_attr_list_unref(attrs);
}

OmniboxPopupViewGtk::OmniboxPopupViewGtk(const gfx::Font& font,
                                         OmniboxView* omnibox_view,
                                         OmniboxEditModel* edit_model,
                                         GtkWidget* location_bar)
    : signal_registrar_(new ui::GtkSignalRegistrar),
      model_(new OmniboxPopupModel(this, edit_model)),
      omnibox_view_(omnibox_view),
      location_bar_(location_bar),
      window_(gtk_window_new(GTK_WINDOW_POPUP)),
      layout_(NULL),
      theme_service_(GtkThemeService::GetFrom(edit_model->profile())),
      font_(font.DeriveFont(kEditFontAdjust)),
      ignore_mouse_drag_(false),
      opened_(false) {
  gtk_widget_set_can_focus(window_, FALSE);
  // Don't allow the window to be resized.  This also forces the window to
  // shrink down to the size of its child contents.
  gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);
  gtk_widget_set_app_paintable(window_, TRUE);
  // Have GTK double buffer around the expose signal.
  gtk_widget_set_double_buffered(window_, TRUE);

  // Cache the layout so we don't have to create it for every expose.  If we
  // were a real widget we should handle changing directions, but we're not
  // doing RTL or anything yet, so it shouldn't be important now.
  layout_ = gtk_widget_create_pango_layout(window_, NULL);
  // We don't want the layout of search results depending on their language.
  pango_layout_set_auto_dir(layout_, FALSE);
  // We always ellipsize when drawing our text runs.
  pango_layout_set_ellipsize(layout_, PANGO_ELLIPSIZE_END);

  gtk_widget_add_events(window_, GDK_BUTTON_MOTION_MASK |
                                 GDK_POINTER_MOTION_MASK |
                                 GDK_BUTTON_PRESS_MASK |
                                 GDK_BUTTON_RELEASE_MASK);
  signal_registrar_->Connect(window_, "motion-notify-event",
                             G_CALLBACK(HandleMotionThunk), this);
  signal_registrar_->Connect(window_, "button-press-event",
                             G_CALLBACK(HandleButtonPressThunk), this);
  signal_registrar_->Connect(window_, "button-release-event",
                             G_CALLBACK(HandleButtonReleaseThunk), this);
  signal_registrar_->Connect(window_, "expose-event",
                             G_CALLBACK(HandleExposeThunk), this);

  registrar_.Add(this,
                 chrome::NOTIFICATION_BROWSER_THEME_CHANGED,
                 content::Source<ThemeService>(theme_service_));
  theme_service_->InitThemesFor(this);

  // TODO(erg): There appears to be a bug somewhere in something which shows
  // itself when we're in NX. Previously, we called
  // gtk_util::ActAsRoundedWindow() to make this popup have rounded
  // corners. This worked on the standard xorg server (both locally and
  // remotely), but broke over NX. My current hypothesis is that it can't
  // handle shaping top-level windows during an expose event, but I'm not sure
  // how else to get accurate shaping information.
  //
  // r25080 (the original patch that added rounded corners here) should
  // eventually be cherry picked once I know what's going
  // on. http://crbug.com/22015.
}

OmniboxPopupViewGtk::~OmniboxPopupViewGtk() {
  // Stop listening to our signals before we destroy the model. I suspect that
  // we can race window destruction, otherwise.
  signal_registrar_.reset();

  // Explicitly destroy our model here, before we destroy our GTK widgets.
  // This is because the model destructor can call back into us, and we need
  // to make sure everything is still valid when it does.
  model_.reset();
  g_object_unref(layout_);
  gtk_widget_destroy(window_);
}

bool OmniboxPopupViewGtk::IsOpen() const {
  return opened_;
}

void OmniboxPopupViewGtk::InvalidateLine(size_t line) {
  // TODO(deanm): Is it possible to use some constant for the width, instead
  // of having to query the width of the window?
  GdkWindow* gdk_window = gtk_widget_get_window(GTK_WIDGET(window_));
  GdkRectangle line_rect = GetRectForLine(
      line, GetWindowRect(gdk_window).width()).ToGdkRectangle();
  gdk_window_invalidate_rect(gdk_window, &line_rect, FALSE);
}

void OmniboxPopupViewGtk::UpdatePopupAppearance() {
  const AutocompleteResult& result = model_->result();
  if (result.empty()) {
    Hide();
    return;
  }

  Show(result.size());
  gtk_widget_queue_draw(window_);
}

gfx::Rect OmniboxPopupViewGtk::GetTargetBounds() {
  if (!gtk_widget_get_realized(window_))
    return gfx::Rect();

  gfx::Rect retval = ui::GetWidgetScreenBounds(window_);

  // The widget bounds don't update synchronously so may be out of sync with
  // our last size request.
  GtkRequisition req;
  gtk_widget_size_request(window_, &req);
  retval.set_width(req.width);
  retval.set_height(req.height);

  return retval;
}

void OmniboxPopupViewGtk::PaintUpdatesNow() {
  // Paint our queued invalidations now, synchronously.
  GdkWindow* gdk_window = gtk_widget_get_window(window_);
  gdk_window_process_updates(gdk_window, FALSE);
}

void OmniboxPopupViewGtk::OnDragCanceled() {
  ignore_mouse_drag_ = true;
}

void OmniboxPopupViewGtk::Observe(int type,
                                  const content::NotificationSource& source,
                                  const content::NotificationDetails& details) {
  DCHECK(type == chrome::NOTIFICATION_BROWSER_THEME_CHANGED);

  if (theme_service_->UsingNativeTheme()) {
    gtk_util::UndoForceFontSize(window_);

    border_color_ = theme_service_->GetBorderColor();

    gtk_util::GetTextColors(
        &background_color_, &selected_background_color_,
        &content_text_color_, &selected_content_text_color_);

    hovered_background_color_ = gtk_util::AverageColors(
        background_color_, selected_background_color_);
    url_text_color_ = NormalURLColor(content_text_color_);
    url_selected_text_color_ = SelectedURLColor(selected_content_text_color_,
                                                selected_background_color_);
  } else {
    gtk_util::ForceFontSizePixels(window_, font_.GetFontSize());

    border_color_ = kBorderColor;
    background_color_ = kBackgroundColor;
    selected_background_color_ = kSelectedBackgroundColor;
    hovered_background_color_ = kHoveredBackgroundColor;

    content_text_color_ = kContentTextColor;
    selected_content_text_color_ = kContentTextColor;
    url_text_color_ = kURLTextColor;
    url_selected_text_color_ = kURLTextColor;
  }

  // Calculate dimmed colors.
  content_dim_text_color_ =
      gtk_util::AverageColors(content_text_color_,
                              background_color_);
  selected_content_dim_text_color_ =
      gtk_util::AverageColors(selected_content_text_color_,
                              selected_background_color_);

  // Set the background color, so we don't need to paint it manually.
  gtk_widget_modify_bg(window_, GTK_STATE_NORMAL, &background_color_);
}

void OmniboxPopupViewGtk::Show(size_t num_results) {
  gint origin_x, origin_y;
  GdkWindow* gdk_window = gtk_widget_get_window(location_bar_);
  gdk_window_get_origin(gdk_window, &origin_x, &origin_y);
  GtkAllocation allocation;
  gtk_widget_get_allocation(location_bar_, &allocation);

  int horizontal_offset = 1;
  gtk_window_move(GTK_WINDOW(window_),
      origin_x + allocation.x - kBorderThickness + horizontal_offset,
      origin_y + allocation.y + allocation.height - kBorderThickness - 1 +
          kVerticalOffset);
  gtk_widget_set_size_request(window_,
      allocation.width + (kBorderThickness * 2) - (horizontal_offset * 2),
      (num_results * kHeightPerResult) + (kBorderThickness * 2));
  gtk_widget_show(window_);
  StackWindow();
  opened_ = true;
}

void OmniboxPopupViewGtk::Hide() {
  gtk_widget_hide(window_);
  opened_ = false;
}

void OmniboxPopupViewGtk::StackWindow() {
  gfx::NativeView omnibox_view = omnibox_view_->GetNativeView();
  DCHECK(GTK_IS_WIDGET(omnibox_view));
  GtkWidget* toplevel = gtk_widget_get_toplevel(omnibox_view);
  DCHECK(gtk_widget_is_toplevel(toplevel));
  ui::StackPopupWindow(window_, toplevel);
}

size_t OmniboxPopupViewGtk::LineFromY(int y) {
  size_t line = std::max(y - kBorderThickness, 0) / kHeightPerResult;
  return std::min(line, model_->result().size() - 1);
}

void OmniboxPopupViewGtk::AcceptLine(size_t line,
                                     WindowOpenDisposition disposition) {
  // OpenMatch() may close the popup, which will clear the result set and, by
  // extension, |match| and its contents.  So copy the relevant match out to
  // make sure it stays alive until the call completes.
  AutocompleteMatch match = model_->result().match_at(line);
  omnibox_view_->OpenMatch(match, disposition, GURL(), line);
}

gfx::Image OmniboxPopupViewGtk::IconForMatch(
    const AutocompleteMatch& match,
    bool selected,
    bool is_selected_keyword) {
  const gfx::Image image = model_->GetIconIfExtensionMatch(match);
  if (!image.IsEmpty())
    return image;

  int icon;
  if (is_selected_keyword)
    icon = IDR_OMNIBOX_TTS;
  else if (match.starred)
    icon = IDR_OMNIBOX_STAR;
  else
    icon = AutocompleteMatch::TypeToIcon(match.type);

  if (selected) {
    switch (icon) {
      case IDR_OMNIBOX_EXTENSION_APP:
        icon = IDR_OMNIBOX_EXTENSION_APP_DARK;
        break;
      case IDR_OMNIBOX_HTTP:
        icon = IDR_OMNIBOX_HTTP_DARK;
        break;
      case IDR_OMNIBOX_SEARCH:
        icon = IDR_OMNIBOX_SEARCH_DARK;
        break;
      case IDR_OMNIBOX_STAR:
        icon = IDR_OMNIBOX_STAR_DARK;
        break;
      case IDR_OMNIBOX_TTS:
        icon = IDR_OMNIBOX_TTS_DARK;
        break;
      default:
        NOTREACHED();
        break;
    }
  }

  return theme_service_->GetImageNamed(icon);
}

void OmniboxPopupViewGtk::GetVisibleMatchForInput(
    size_t index,
    const AutocompleteMatch** match,
    bool* is_selected_keyword) {
  const AutocompleteResult& result = model_->result();

  if (result.match_at(index).associated_keyword.get() &&
      model_->selected_line() == index &&
      model_->selected_line_state() == OmniboxPopupModel::KEYWORD) {
    *match = result.match_at(index).associated_keyword.get();
    *is_selected_keyword = true;
    return;
  }

  *match = &result.match_at(index);
  *is_selected_keyword = false;
}

gboolean OmniboxPopupViewGtk::HandleMotion(GtkWidget* widget,
                                           GdkEventMotion* event) {
  // TODO(deanm): Windows has a bunch of complicated logic here.
  size_t line = LineFromY(static_cast<int>(event->y));
  // There is both a hovered and selected line, hovered just means your mouse
  // is over it, but selected is what's showing in the location edit.
  model_->SetHoveredLine(line);
  // Select the line if the user has the left mouse button down.
  if (!ignore_mouse_drag_ && (event->state & GDK_BUTTON1_MASK))
    model_->SetSelectedLine(line, false, false);
  return TRUE;
}

gboolean OmniboxPopupViewGtk::HandleButtonPress(GtkWidget* widget,
                                                GdkEventButton* event) {
  ignore_mouse_drag_ = false;
  // Very similar to HandleMotion.
  size_t line = LineFromY(static_cast<int>(event->y));
  model_->SetHoveredLine(line);
  if (event->button == 1)
    model_->SetSelectedLine(line, false, false);
  return TRUE;
}

gboolean OmniboxPopupViewGtk::HandleButtonRelease(GtkWidget* widget,
                                                  GdkEventButton* event) {
  if (ignore_mouse_drag_) {
    // See header comment about this flag.
    ignore_mouse_drag_ = false;
    return TRUE;
  }

  size_t line = LineFromY(static_cast<int>(event->y));
  switch (event->button) {
    case 1:  // Left click.
      AcceptLine(line, CURRENT_TAB);
      break;
    case 2:  // Middle click.
      AcceptLine(line, NEW_BACKGROUND_TAB);
      break;
    default:
      // Don't open the result.
      break;
  }
  return TRUE;
}

gboolean OmniboxPopupViewGtk::HandleExpose(GtkWidget* widget,
                                           GdkEventExpose* event) {
  bool ltr = !base::i18n::IsRTL();
  const AutocompleteResult& result = model_->result();

  gfx::Rect window_rect = GetWindowRect(event->window);
  gfx::Rect damage_rect = gfx::Rect(event->area);
  // Handle when our window is super narrow.  A bunch of the calculations
  // below would go negative, and really we're not going to fit anything
  // useful in such a small window anyway.  Just don't paint anything.
  // This means we won't draw the border, but, yeah, whatever.
  // TODO(deanm): Make the code more robust and remove this check.
  if (window_rect.width() < (kIconAreaWidth * 3))
    return TRUE;

  cairo_t* cr = gdk_cairo_create(gtk_widget_get_window(widget));
  gdk_cairo_rectangle(cr, &event->area);
  cairo_clip(cr);

  // This assert is kinda ugly, but it would be more currently unneeded work
  // to support painting a border that isn't 1 pixel thick.  There is no point
  // in writing that code now, and explode if that day ever comes.
  COMPILE_ASSERT(kBorderThickness == 1, border_1px_implied);
  // Draw the 1px border around the entire window.
  gdk_cairo_set_source_color(cr, &border_color_);
  cairo_rectangle(cr, 0, 0, window_rect.width(), window_rect.height());
  cairo_stroke(cr);

  pango_layout_set_height(layout_, kHeightPerResult * PANGO_SCALE);

  for (size_t i = 0; i < result.size(); ++i) {
    gfx::Rect line_rect = GetRectForLine(i, window_rect.width());
    // Only repaint and layout damaged lines.
    if (!line_rect.Intersects(damage_rect))
      continue;

    const AutocompleteMatch* match = NULL;
    bool is_selected_keyword = false;
    GetVisibleMatchForInput(i, &match, &is_selected_keyword);
    bool is_selected = (model_->selected_line() == i);
    bool is_hovered = (model_->hovered_line() == i);
    if (is_selected || is_hovered) {
      gdk_cairo_set_source_color(cr, is_selected ? &selected_background_color_ :
                                 &hovered_background_color_);
      // This entry is selected or hovered, fill a rect with the color.
      cairo_rectangle(cr, line_rect.x(), line_rect.y(),
                      line_rect.width(), line_rect.height());
      cairo_fill(cr);
    }

    int icon_start_x = ltr ? kIconLeftPadding :
        (line_rect.width() - kIconLeftPadding - kIconWidth);
    // Draw the icon for this result.
    gtk_util::DrawFullImage(cr, widget,
                            IconForMatch(*match, is_selected,
                                         is_selected_keyword),
                            icon_start_x, line_rect.y() + kIconTopPadding);

    // Draw the results text vertically centered in the results space.
    // First draw the contents / url, but don't let it take up the whole width
    // if there is also a description to be shown.
    bool has_description = !match->description.empty();
    int text_width = window_rect.width() - (kIconAreaWidth + kRightPadding);
    int allocated_content_width = has_description ?
        static_cast<int>(text_width * kContentWidthPercentage) : text_width;
    pango_layout_set_width(layout_, allocated_content_width * PANGO_SCALE);

    // Note: We force to URL to LTR for all text directions.
    SetupLayoutForMatch(layout_, match->contents, match->contents_class,
                        is_selected ? &selected_content_text_color_ :
                            &content_text_color_,
                        is_selected ? &selected_content_dim_text_color_ :
                            &content_dim_text_color_,
                        is_selected ? &url_selected_text_color_ :
                            &url_text_color_,
                        std::string());

    int actual_content_width, actual_content_height;
    pango_layout_get_size(layout_,
        &actual_content_width, &actual_content_height);
    actual_content_width /= PANGO_SCALE;
    actual_content_height /= PANGO_SCALE;

    // DCHECK_LT(actual_content_height, kHeightPerResult);  // Font is too tall.
    // Center the text within the line.
    int content_y = std::max(line_rect.y(),
        line_rect.y() + ((kHeightPerResult - actual_content_height) / 2));

    cairo_save(cr);
    cairo_move_to(cr,
                  ltr ? kIconAreaWidth :
                        (text_width - actual_content_width),
                  content_y);
    pango_cairo_show_layout(cr, layout_);
    cairo_restore(cr);

    if (has_description) {
      pango_layout_set_width(layout_,
          (text_width - actual_content_width) * PANGO_SCALE);

      // In Windows, a boolean "force_dim" is passed as true for the
      // description.  Here, we pass the dim text color for both normal and dim,
      // to accomplish the same thing.
      SetupLayoutForMatch(layout_, match->description, match->description_class,
                          is_selected ? &selected_content_dim_text_color_ :
                              &content_dim_text_color_,
                          is_selected ? &selected_content_dim_text_color_ :
                              &content_dim_text_color_,
                          is_selected ? &url_selected_text_color_ :
                              &url_text_color_,
                          std::string(" - "));
      gint actual_description_width;
      pango_layout_get_size(layout_, &actual_description_width, NULL);

      cairo_save(cr);
      cairo_move_to(cr, ltr ?
                    (kIconAreaWidth + actual_content_width) :
                    (text_width - actual_content_width -
                     (actual_description_width / PANGO_SCALE)),
                    content_y);
      pango_cairo_show_layout(cr, layout_);
      cairo_restore(cr);
    }

    if (match->associated_keyword.get()) {
      // If this entry has an associated keyword, draw the arrow at the extreme
      // other side of the omnibox.
      icon_start_x = ltr ? (line_rect.width() - kIconLeftPadding - kIconWidth) :
          kIconLeftPadding;
      // Draw the icon for this result.
      gtk_util::DrawFullImage(cr, widget,
                              theme_service_->GetImageNamed(
                                  is_selected ? IDR_OMNIBOX_TTS_DARK :
                                  IDR_OMNIBOX_TTS),
                              icon_start_x, line_rect.y() + kIconTopPadding);
    }
  }

  cairo_destroy(cr);
  return TRUE;
}
