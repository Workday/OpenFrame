// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/opaque_browser_frame_view.h"

#include <algorithm>
#include <string>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/ui/views/avatar_label.h"
#include "chrome/browser/ui/views/avatar_menu_button.h"
#include "chrome/browser/ui/views/frame/browser_frame.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tab_icon_view.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/browser/ui/views/toolbar_view.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/ui_resources.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "ui/base/hit_test.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/theme_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/path.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/layout_constants.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/window/frame_background.h"
#include "ui/views/window/window_shape.h"

#if defined(OS_WIN)
#include "win8/util/win8_util.h"
#endif  // OS_WIN

using content::WebContents;

namespace {

// The frame border is only visible in restored mode and is hardcoded to 4 px on
// each side regardless of the system window border size.
const int kFrameBorderThickness = 4;
// Besides the frame border, there's another 9 px of empty space atop the
// window in restored mode, to use to drag the window around.
const int kNonClientRestoredExtraThickness = 9;
// While resize areas on Windows are normally the same size as the window
// borders, our top area is shrunk by 1 px to make it easier to move the window
// around with our thinner top grabbable strip.  (Incidentally, our side and
// bottom resize areas don't match the frame border thickness either -- they
// span the whole nonclient area, so there's no "dead zone" for the mouse.)
const int kTopResizeAdjust = 1;
// In the window corners, the resize areas don't actually expand bigger, but the
// 16 px at the end of each edge triggers diagonal resizing.
const int kResizeAreaCornerSize = 16;
// The titlebar never shrinks too short to show the caption button plus some
// padding below it.
const int kCaptionButtonHeightWithPadding = 19;
// The content left/right images have a shadow built into them.
const int kContentEdgeShadowThickness = 2;
// The titlebar has a 2 px 3D edge along the top and bottom.
const int kTitlebarTopAndBottomEdgeThickness = 2;
// The icon is inset 2 px from the left frame border.
const int kIconLeftSpacing = 2;
// The icon never shrinks below 16 px on a side.
const int kIconMinimumSize = 16;
// There is a 4 px gap between the icon and the title text.
const int kIconTitleSpacing = 4;
// There is a 5 px gap between the title text and the caption buttons.
const int kTitleLogoSpacing = 5;
// The avatar ends 2 px above the bottom of the tabstrip (which, given the
// way the tabstrip draws its bottom edge, will appear like a 1 px gap to the
// user).
const int kAvatarBottomSpacing = 2;
// Space between the frame border and the left edge of the avatar.
const int kAvatarLeftSpacing = 2;
// Space between the right edge of the avatar and the tabstrip.
const int kAvatarRightSpacing = -2;
// The top 3 px of the tabstrip is shadow; in maximized mode we push this off
// the top of the screen so the tabs appear flush against the screen edge.
const int kTabstripTopShadowThickness = 3;
// In restored mode, the New Tab button isn't at the same height as the caption
// buttons, but the space will look cluttered if it actually slides under them,
// so we stop it when the gap between the two is down to 5 px.
const int kNewTabCaptionRestoredSpacing = 5;
// In maximized mode, where the New Tab button and the caption buttons are at
// similar vertical coordinates, we need to reserve a larger, 16 px gap to avoid
// looking too cluttered.
const int kNewTabCaptionMaximizedSpacing = 16;
// How far to indent the tabstrip from the left side of the screen when there
// is no avatar icon.
const int kTabStripIndent = -6;

// Converts |bounds| from |src|'s coordinate system to |dst|, and checks if
// |pt| is contained within.
bool ConvertedContainsCheck(gfx::Rect bounds, const views::View* src,
                            const views::View* dst, const gfx::Point& pt) {
  DCHECK(src);
  DCHECK(dst);
  gfx::Point origin(bounds.origin());
  views::View::ConvertPointToTarget(src, dst, &origin);
  bounds.set_origin(origin);
  return bounds.Contains(pt);
}

bool ShouldAddDefaultCaptionButtons() {
#if defined(OS_WIN)
  return !win8::IsSingleWindowMetroMode();
#endif  // OS_WIN
  return true;
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// OpaqueBrowserFrameView, public:

OpaqueBrowserFrameView::OpaqueBrowserFrameView(BrowserFrame* frame,
                                               BrowserView* browser_view)
    : BrowserNonClientFrameView(frame, browser_view),
      minimize_button_(NULL),
      maximize_button_(NULL),
      restore_button_(NULL),
      close_button_(NULL),
      window_icon_(NULL),
      window_title_(NULL),
      frame_background_(new views::FrameBackground()) {
  if (ShouldAddDefaultCaptionButtons()) {
    minimize_button_ = InitWindowCaptionButton(IDR_MINIMIZE,
                                               IDR_MINIMIZE_H,
                                               IDR_MINIMIZE_P,
                                               IDR_MINIMIZE_BUTTON_MASK,
                                               IDS_ACCNAME_MINIMIZE);
    maximize_button_ = InitWindowCaptionButton(IDR_MAXIMIZE,
                                               IDR_MAXIMIZE_H,
                                               IDR_MAXIMIZE_P,
                                               IDR_MAXIMIZE_BUTTON_MASK,
                                               IDS_ACCNAME_MAXIMIZE);
    restore_button_ = InitWindowCaptionButton(IDR_RESTORE,
                                              IDR_RESTORE_H,
                                              IDR_RESTORE_P,
                                              IDR_RESTORE_BUTTON_MASK,
                                              IDS_ACCNAME_RESTORE);
    close_button_ = InitWindowCaptionButton(IDR_CLOSE,
                                            IDR_CLOSE_H,
                                            IDR_CLOSE_P,
                                            IDR_CLOSE_BUTTON_MASK,
                                            IDS_ACCNAME_CLOSE);
  }

  // Initializing the TabIconView is expensive, so only do it if we need to.
  if (browser_view->ShouldShowWindowIcon()) {
    window_icon_ = new TabIconView(this);
    window_icon_->set_is_light(true);
    AddChildView(window_icon_);
    window_icon_->Update();
  }

  window_title_ = new views::Label(browser_view->GetWindowTitle(),
                                   BrowserFrame::GetTitleFont());
  window_title_->SetVisible(browser_view->ShouldShowWindowTitle());
  window_title_->SetEnabledColor(SK_ColorWHITE);
  // TODO(msw): Use a transparent background color as a workaround to use the
  // gfx::Canvas::NO_SUBPIXEL_RENDERING flag and avoid some visual artifacts.
  window_title_->SetBackgroundColor(0x00000000);
  window_title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  AddChildView(window_title_);

  UpdateAvatarInfo();
  if (!browser_view->IsOffTheRecord()) {
    registrar_.Add(this, chrome::NOTIFICATION_PROFILE_CACHED_INFO_CHANGED,
                   content::NotificationService::AllSources());
  }
}

OpaqueBrowserFrameView::~OpaqueBrowserFrameView() {
}

///////////////////////////////////////////////////////////////////////////////
// OpaqueBrowserFrameView, protected:

int OpaqueBrowserFrameView::GetReservedHeight() const {
  return 0;
}

gfx::Rect OpaqueBrowserFrameView::GetBoundsForReservedArea() const {
  gfx::Rect client_view_bounds = CalculateClientAreaBounds(width(), height());
  return gfx::Rect(
      client_view_bounds.x(),
      client_view_bounds.y() + client_view_bounds.height(),
      client_view_bounds.width(),
      GetReservedHeight());
}

int OpaqueBrowserFrameView::NonClientTopBorderHeight(
    bool restored) const {
  views::WidgetDelegate* delegate = frame()->widget_delegate();
  // |delegate| may be NULL if called from callback of InputMethodChanged while
  // a window is being destroyed.
  // See more discussion at http://crosbug.com/8958
  if (delegate && delegate->ShouldShowWindowTitle()) {
    return std::max(FrameBorderThickness(restored) + IconSize(),
        CaptionButtonY(restored) + kCaptionButtonHeightWithPadding) +
        TitlebarBottomThickness(restored);
  }

  return FrameBorderThickness(restored) -
      ((browser_view()->IsTabStripVisible() &&
          !restored && !frame()->ShouldLeaveOffsetNearTopBorder())
              ? kTabstripTopShadowThickness : 0);
}

///////////////////////////////////////////////////////////////////////////////
// OpaqueBrowserFrameView, BrowserNonClientFrameView implementation:

gfx::Rect OpaqueBrowserFrameView::GetBoundsForTabStrip(
    views::View* tabstrip) const {
  if (!tabstrip)
    return gfx::Rect();

  gfx::Rect bounds = GetBoundsForTabStripAndAvatarArea(tabstrip);
  int space_left_of_tabstrip = kTabStripIndent;
  if (browser_view()->ShouldShowAvatar()) {
    if (avatar_label() && avatar_label()->bounds().width()) {
      // Space between the right edge of the avatar label and the tabstrip.
      const int kAvatarLabelRightSpacing = -10;
      space_left_of_tabstrip =
          avatar_label()->bounds().right() + kAvatarLabelRightSpacing;
    } else {
      space_left_of_tabstrip =
          kAvatarLeftSpacing + avatar_bounds_.width() + kAvatarRightSpacing;
    }
  }
  bounds.Inset(space_left_of_tabstrip, 0, 0, 0);
  return bounds;
}

BrowserNonClientFrameView::TabStripInsets
OpaqueBrowserFrameView::GetTabStripInsets(bool restored) const {
  int top = NonClientTopBorderHeight(restored) + ((!restored &&
      (!frame()->ShouldLeaveOffsetNearTopBorder() ||
      frame()->IsFullscreen())) ?
      0 : kNonClientRestoredExtraThickness);
  // TODO: include OTR and caption.
  return TabStripInsets(top, 0, 0);
}

int OpaqueBrowserFrameView::GetThemeBackgroundXInset() const {
  return 0;
}

void OpaqueBrowserFrameView::UpdateThrobber(bool running) {
  if (window_icon_)
    window_icon_->Update();
}

gfx::Size OpaqueBrowserFrameView::GetMinimumSize() {
  gfx::Size min_size(browser_view()->GetMinimumSize());
  int border_thickness = NonClientBorderThickness();
  min_size.Enlarge(2 * border_thickness,
                   NonClientTopBorderHeight(false) + border_thickness);

  views::WidgetDelegate* delegate = frame()->widget_delegate();
  int min_titlebar_width = (2 * FrameBorderThickness(false)) +
      kIconLeftSpacing +
      (delegate && delegate->ShouldShowWindowIcon() ?
       (IconSize() + kTitleLogoSpacing) : 0);
#if !defined(OS_CHROMEOS)
  if (ShouldAddDefaultCaptionButtons()) {
    min_titlebar_width +=
        minimize_button_->GetMinimumSize().width() +
        restore_button_->GetMinimumSize().width() +
        close_button_->GetMinimumSize().width();
  }
#endif
  min_size.set_width(std::max(min_size.width(), min_titlebar_width));

  // Ensure that the minimum width is enough to hold a minimum width tab strip
  // and avatar icon at their usual insets.
  if (browser_view()->IsTabStripVisible()) {
    TabStrip* tabstrip = browser_view()->tabstrip();
    const int min_tabstrip_width = tabstrip->GetMinimumSize().width();
    const int min_tabstrip_area_width =
        width() - GetBoundsForTabStripAndAvatarArea(tabstrip).width() +
        min_tabstrip_width + browser_view()->GetOTRAvatarIcon().width() +
        kAvatarLeftSpacing + kAvatarRightSpacing;
    min_size.set_width(std::max(min_size.width(), min_tabstrip_area_width));
  }

  return min_size;
}

///////////////////////////////////////////////////////////////////////////////
// OpaqueBrowserFrameView, views::NonClientFrameView implementation:

gfx::Rect OpaqueBrowserFrameView::GetBoundsForClientView() const {
  return client_view_bounds_;
}

gfx::Rect OpaqueBrowserFrameView::GetWindowBoundsForClientBounds(
    const gfx::Rect& client_bounds) const {
  int top_height = NonClientTopBorderHeight(false);
  int border_thickness = NonClientBorderThickness();
  return gfx::Rect(std::max(0, client_bounds.x() - border_thickness),
                   std::max(0, client_bounds.y() - top_height),
                   client_bounds.width() + (2 * border_thickness),
                   client_bounds.height() + top_height + border_thickness);
}

int OpaqueBrowserFrameView::NonClientHitTest(const gfx::Point& point) {
  if (!bounds().Contains(point))
    return HTNOWHERE;

  // See if the point is within the avatar menu button or within the avatar
  // label.
  if ((avatar_button() &&
       avatar_button()->GetMirroredBounds().Contains(point)) ||
      (avatar_label() && avatar_label()->GetMirroredBounds().Contains(point)))
    return HTCLIENT;

  int frame_component = frame()->client_view()->NonClientHitTest(point);

  // See if we're in the sysmenu region.  We still have to check the tabstrip
  // first so that clicks in a tab don't get treated as sysmenu clicks.
  gfx::Rect sysmenu_rect(IconBounds());
  // In maximized mode we extend the rect to the screen corner to take advantage
  // of Fitts' Law.
  if (frame()->IsMaximized())
    sysmenu_rect.SetRect(0, 0, sysmenu_rect.right(), sysmenu_rect.bottom());
  sysmenu_rect.set_x(GetMirroredXForRect(sysmenu_rect));
  if (sysmenu_rect.Contains(point))
    return (frame_component == HTCLIENT) ? HTCLIENT : HTSYSMENU;

  if (frame_component != HTNOWHERE)
    return frame_component;

  // Then see if the point is within any of the window controls.
  if (close_button_ && close_button_->visible() &&
      close_button_->GetMirroredBounds().Contains(point))
    return HTCLOSE;
  if (restore_button_ && restore_button_->visible() &&
      restore_button_->GetMirroredBounds().Contains(point))
    return HTMAXBUTTON;
  if (maximize_button_ && maximize_button_->visible() &&
      maximize_button_->GetMirroredBounds().Contains(point))
    return HTMAXBUTTON;
  if (minimize_button_ && minimize_button_->visible() &&
      minimize_button_->GetMirroredBounds().Contains(point))
    return HTMINBUTTON;

  views::WidgetDelegate* delegate = frame()->widget_delegate();
  if (!delegate) {
    LOG(WARNING) << "delegate is NULL, returning safe default.";
    return HTCAPTION;
  }
  int window_component = GetHTComponentForFrame(point, TopResizeHeight(),
      NonClientBorderThickness(), kResizeAreaCornerSize, kResizeAreaCornerSize,
      delegate->CanResize());
  // Fall back to the caption if no other component matches.
  return (window_component == HTNOWHERE) ? HTCAPTION : window_component;
}

void OpaqueBrowserFrameView::GetWindowMask(const gfx::Size& size,
                                           gfx::Path* window_mask) {
  DCHECK(window_mask);

  if (frame()->IsMaximized() || frame()->IsFullscreen())
    return;

  views::GetDefaultWindowMask(size, window_mask);
}

void OpaqueBrowserFrameView::ResetWindowControls() {
  if (!ShouldAddDefaultCaptionButtons())
    return;
  restore_button_->SetState(views::CustomButton::STATE_NORMAL);
  minimize_button_->SetState(views::CustomButton::STATE_NORMAL);
  maximize_button_->SetState(views::CustomButton::STATE_NORMAL);
  // The close button isn't affected by this constraint.
}

void OpaqueBrowserFrameView::UpdateWindowIcon() {
  window_icon_->SchedulePaint();
}

void OpaqueBrowserFrameView::UpdateWindowTitle() {
  if (!frame()->IsFullscreen())
    window_title_->SchedulePaint();
}

///////////////////////////////////////////////////////////////////////////////
// OpaqueBrowserFrameView, views::View overrides:

void OpaqueBrowserFrameView::OnPaint(gfx::Canvas* canvas) {
  if (frame()->IsFullscreen())
    return;  // Nothing is visible, so don't bother to paint.

  if (frame()->IsMaximized())
    PaintMaximizedFrameBorder(canvas);
  else
    PaintRestoredFrameBorder(canvas);

  // The window icon and title are painted by their respective views.
  /* TODO(pkasting):  If this window is active, we should also draw a drop
   * shadow on the title.  This is tricky, because we don't want to hardcode a
   * shadow color (since we want to work with various themes), but we can't
   * alpha-blend either (since the Windows text APIs don't really do this).
   * So we'd need to sample the background color at the right location and
   * synthesize a good shadow color. */

  if (browser_view()->IsToolbarVisible())
    PaintToolbarBackground(canvas);
  if (!frame()->IsMaximized())
    PaintRestoredClientEdge(canvas);
}

void OpaqueBrowserFrameView::Layout() {
  LayoutWindowControls();
  LayoutTitleBar();
  LayoutAvatar();
  client_view_bounds_ = CalculateClientAreaBounds(width(), height());
}

bool OpaqueBrowserFrameView::HitTestRect(const gfx::Rect& rect) const {
  if (!views::View::HitTestRect(rect)) {
    // |rect| is outside OpaqueBrowserFrameView's bounds.
    return false;
  }

  // If the rect is outside the bounds of the client area, claim it.
  // TODO(tdanderson): Implement View::ConvertRectToTarget().
  gfx::Point rect_in_client_view_coords_origin(rect.origin());
  View::ConvertPointToTarget(this, frame()->client_view(),
      &rect_in_client_view_coords_origin);
  gfx::Rect rect_in_client_view_coords(
      rect_in_client_view_coords_origin, rect.size());
  if (!frame()->client_view()->HitTestRect(rect_in_client_view_coords))
    return true;

  // Otherwise, claim |rect| only if it is above the bottom of the tabstrip in
  // a non-tab portion.
  TabStrip* tabstrip = browser_view()->tabstrip();
  if (!tabstrip || !browser_view()->IsTabStripVisible())
    return false;

  gfx::Point rect_in_tabstrip_coords_origin(rect.origin());
  View::ConvertPointToTarget(this, tabstrip,
      &rect_in_tabstrip_coords_origin);
  gfx::Rect rect_in_tabstrip_coords(
      rect_in_tabstrip_coords_origin, rect.size());

  if (rect_in_tabstrip_coords.bottom() > tabstrip->GetLocalBounds().bottom()) {
    // |rect| is below the tabstrip.
    return false;
  }

  if (tabstrip->HitTestRect(rect_in_tabstrip_coords)) {
    // Claim |rect| if it is in a non-tab portion of the tabstrip.
    // TODO(tdanderson): Pass |rect_in_tabstrip_coords| instead of its center
    // point to TabStrip::IsPositionInWindowCaption() once
    // GetEventHandlerForRect() is implemented.
    return tabstrip->IsPositionInWindowCaption(
        rect_in_tabstrip_coords.CenterPoint());
  }

  // The window switcher button is to the right of the tabstrip but is
  // part of the client view.
  views::View* window_switcher_button =
      browser_view()->window_switcher_button();
  if (window_switcher_button && window_switcher_button->visible()) {
    gfx::Point rect_in_window_switcher_coords_origin(rect.origin());
    View::ConvertPointToTarget(this, window_switcher_button,
        &rect_in_window_switcher_coords_origin);
    gfx::Rect rect_in_window_switcher_coords(
        rect_in_window_switcher_coords_origin, rect.size());

    if (window_switcher_button->HitTestRect(rect_in_window_switcher_coords))
      return false;
  }

  // We claim |rect| because it is above the bottom of the tabstrip, but
  // neither in the tabstrip nor in the window switcher button. In particular,
  // the avatar label/button is left of the tabstrip and the window controls
  // are right of the tabstrip.
  return true;
}

void OpaqueBrowserFrameView::GetAccessibleState(
    ui::AccessibleViewState* state) {
  state->role = ui::AccessibilityTypes::ROLE_TITLEBAR;
}

///////////////////////////////////////////////////////////////////////////////
// OpaqueBrowserFrameView, views::ButtonListener implementation:

void OpaqueBrowserFrameView::ButtonPressed(views::Button* sender,
                                           const ui::Event& event) {
  if (sender == minimize_button_)
    frame()->Minimize();
  else if (sender == maximize_button_)
    frame()->Maximize();
  else if (sender == restore_button_)
    frame()->Restore();
  else if (sender == close_button_)
    frame()->Close();
}

///////////////////////////////////////////////////////////////////////////////
// OpaqueBrowserFrameView, TabIconView::TabContentsProvider implementation:

bool OpaqueBrowserFrameView::ShouldTabIconViewAnimate() const {
  // This function is queried during the creation of the window as the
  // TabIconView we host is initialized, so we need to NULL check the selected
  // WebContents because in this condition there is not yet a selected tab.
  WebContents* current_tab = browser_view()->GetActiveWebContents();
  return current_tab ? current_tab->IsLoading() : false;
}

gfx::ImageSkia OpaqueBrowserFrameView::GetFaviconForTabIconView() {
  views::WidgetDelegate* delegate = frame()->widget_delegate();
  if (!delegate) {
    LOG(WARNING) << "delegate is NULL, returning safe default.";
    return gfx::ImageSkia();
  }
  return delegate->GetWindowIcon();
}

///////////////////////////////////////////////////////////////////////////////
// OpaqueBrowserFrameView, protected:

void OpaqueBrowserFrameView::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_PROFILE_CACHED_INFO_CHANGED:
      UpdateAvatarInfo();
      break;
    default:
      NOTREACHED() << "Got a notification we didn't register for!";
      break;
  }
}

///////////////////////////////////////////////////////////////////////////////
// OpaqueBrowserFrameView, private:

views::ImageButton* OpaqueBrowserFrameView::InitWindowCaptionButton(
    int normal_image_id,
    int hot_image_id,
    int pushed_image_id,
    int mask_image_id,
    int accessibility_string_id) {
  views::ImageButton* button = new views::ImageButton(this);
  ui::ThemeProvider* tp = frame()->GetThemeProvider();
  button->SetImage(views::CustomButton::STATE_NORMAL,
                   tp->GetImageSkiaNamed(normal_image_id));
  button->SetImage(views::CustomButton::STATE_HOVERED,
                   tp->GetImageSkiaNamed(hot_image_id));
  button->SetImage(views::CustomButton::STATE_PRESSED,
                   tp->GetImageSkiaNamed(pushed_image_id));
  if (browser_view()->IsBrowserTypeNormal()) {
    button->SetBackground(
        tp->GetColor(ThemeProperties::COLOR_BUTTON_BACKGROUND),
        tp->GetImageSkiaNamed(IDR_THEME_WINDOW_CONTROL_BACKGROUND),
        tp->GetImageSkiaNamed(mask_image_id));
  }
  button->SetAccessibleName(
      l10n_util::GetStringUTF16(accessibility_string_id));
  AddChildView(button);
  return button;
}

int OpaqueBrowserFrameView::FrameBorderThickness(bool restored) const {
  return (!restored && (frame()->IsMaximized() || frame()->IsFullscreen())) ?
      0 : kFrameBorderThickness;
}

int OpaqueBrowserFrameView::TopResizeHeight() const {
  return FrameBorderThickness(false) - kTopResizeAdjust;
}

int OpaqueBrowserFrameView::NonClientBorderThickness() const {
  // When we fill the screen, we don't show a client edge.
  return FrameBorderThickness(false) +
      ((frame()->IsMaximized() || frame()->IsFullscreen()) ?
       0 : kClientEdgeThickness);
}

int OpaqueBrowserFrameView::CaptionButtonY(bool restored) const {
  // Maximized buttons start at window top so that even if their images aren't
  // drawn flush with the screen edge, they still obey Fitts' Law.
  return (!restored && frame()->IsMaximized()) ?
      FrameBorderThickness(false) : kFrameShadowThickness;
}

int OpaqueBrowserFrameView::TitlebarBottomThickness(bool restored) const {
  return kTitlebarTopAndBottomEdgeThickness +
      ((!restored && frame()->IsMaximized()) ? 0 : kClientEdgeThickness);
}

int OpaqueBrowserFrameView::IconSize() const {
#if defined(OS_WIN)
  // This metric scales up if either the titlebar height or the titlebar font
  // size are increased.
  return GetSystemMetrics(SM_CYSMICON);
#else
  return std::max(BrowserFrame::GetTitleFont().GetHeight(), kIconMinimumSize);
#endif
}

gfx::Rect OpaqueBrowserFrameView::IconBounds() const {
  int size = IconSize();
  int frame_thickness = FrameBorderThickness(false);
  int y;
  views::WidgetDelegate* delegate = frame()->widget_delegate();
  if (delegate && (delegate->ShouldShowWindowIcon() ||
                   delegate->ShouldShowWindowTitle())) {
    // Our frame border has a different "3D look" than Windows'.  Theirs has a
    // more complex gradient on the top that they push their icon/title below;
    // then the maximized window cuts this off and the icon/title are centered
    // in the remaining space.  Because the apparent shape of our border is
    // simpler, using the same positioning makes things look slightly uncentered
    // with restored windows, so when the window is restored, instead of
    // calculating the remaining space from below the frame border, we calculate
    // from below the 3D edge.
    int unavailable_px_at_top = frame()->IsMaximized() ?
        frame_thickness : kTitlebarTopAndBottomEdgeThickness;
    // When the icon is shorter than the minimum space we reserve for the
    // caption button, we vertically center it.  We want to bias rounding to put
    // extra space above the icon, since the 3D edge (+ client edge, for
    // restored windows) below looks (to the eye) more like additional space
    // than does the 3D edge (or nothing at all, for maximized windows) above;
    // hence the +1.
    y = unavailable_px_at_top + (NonClientTopBorderHeight(false) -
        unavailable_px_at_top - size - TitlebarBottomThickness(false) + 1) / 2;
  } else {
    // For "browser mode" windows, we use the native positioning, which is just
    // below the top frame border.
    y = frame_thickness;
  }
  return gfx::Rect(frame_thickness + kIconLeftSpacing, y, size, size);
}

gfx::Rect OpaqueBrowserFrameView::GetBoundsForTabStripAndAvatarArea(
    views::View* tabstrip) const {
  int available_width = width();
  if (minimize_button_) {
    available_width = minimize_button_->x();
  } else if (browser_view()->window_switcher_button()) {
    // We don't have the sysmenu buttons in Windows 8 metro mode. However there
    // are buttons like the window switcher which are drawn in the non client
    // are in the BrowserView. We need to ensure that the tab strip does not
    // draw on the window switcher button.
    available_width -= browser_view()->window_switcher_button()->width();
  }
  const int caption_spacing = frame()->IsMaximized() ?
      kNewTabCaptionMaximizedSpacing : kNewTabCaptionRestoredSpacing;
  const int tabstrip_x = NonClientBorderThickness();
  const int tabstrip_width = available_width - tabstrip_x - caption_spacing;
  return gfx::Rect(tabstrip_x, GetTabStripInsets(false).top,
                   std::max(0, tabstrip_width),
                   tabstrip->GetPreferredSize().height());
}

void OpaqueBrowserFrameView::PaintRestoredFrameBorder(gfx::Canvas* canvas) {
  frame_background_->set_frame_color(GetFrameColor());
  frame_background_->set_theme_image(GetFrameImage());
  frame_background_->set_theme_overlay_image(GetFrameOverlayImage());
  frame_background_->set_top_area_height(GetTopAreaHeight());

  ui::ThemeProvider* tp = GetThemeProvider();
  frame_background_->SetSideImages(
      tp->GetImageSkiaNamed(IDR_WINDOW_LEFT_SIDE),
      tp->GetImageSkiaNamed(IDR_WINDOW_TOP_CENTER),
      tp->GetImageSkiaNamed(IDR_WINDOW_RIGHT_SIDE),
      tp->GetImageSkiaNamed(IDR_WINDOW_BOTTOM_CENTER));
  frame_background_->SetCornerImages(
      tp->GetImageSkiaNamed(IDR_WINDOW_TOP_LEFT_CORNER),
      tp->GetImageSkiaNamed(IDR_WINDOW_TOP_RIGHT_CORNER),
      tp->GetImageSkiaNamed(IDR_WINDOW_BOTTOM_LEFT_CORNER),
      tp->GetImageSkiaNamed(IDR_WINDOW_BOTTOM_RIGHT_CORNER));
  frame_background_->PaintRestored(canvas, this);

  // Note: When we don't have a toolbar, we need to draw some kind of bottom
  // edge here.  Because the App Window graphics we use for this have an
  // attached client edge and their sizing algorithm is a little involved, we do
  // all this in PaintRestoredClientEdge().
}

void OpaqueBrowserFrameView::PaintMaximizedFrameBorder(gfx::Canvas* canvas) {
  frame_background_->set_frame_color(GetFrameColor());
  frame_background_->set_theme_image(GetFrameImage());
  frame_background_->set_theme_overlay_image(GetFrameOverlayImage());
  frame_background_->set_top_area_height(GetTopAreaHeight());

  // Theme frame must be aligned with the tabstrip as if we were
  // in restored mode.  Note that the top of the tabstrip is
  // kTabstripTopShadowThickness px off the top of the screen.
  int theme_background_y = -(GetTabStripInsets(true).top +
      kTabstripTopShadowThickness);
  frame_background_->set_theme_background_y(theme_background_y);

  frame_background_->PaintMaximized(canvas, this);

  // TODO(jamescook): Migrate this into FrameBackground.
  if (!browser_view()->IsToolbarVisible()) {
    // There's no toolbar to edge the frame border, so we need to draw a bottom
    // edge.  The graphic we use for this has a built in client edge, so we clip
    // it off the bottom.
    gfx::ImageSkia* top_center =
        GetThemeProvider()->GetImageSkiaNamed(IDR_APP_TOP_CENTER);
    int edge_height = top_center->height() - kClientEdgeThickness;
    canvas->TileImageInt(*top_center, 0,
        frame()->client_view()->y() - edge_height, width(), edge_height);
  }
}

void OpaqueBrowserFrameView::PaintToolbarBackground(gfx::Canvas* canvas) {
  gfx::Rect toolbar_bounds(browser_view()->GetToolbarBounds());
  if (toolbar_bounds.IsEmpty())
    return;
  gfx::Point toolbar_origin(toolbar_bounds.origin());
  ConvertPointToTarget(browser_view(), this, &toolbar_origin);
  toolbar_bounds.set_origin(toolbar_origin);

  int x = toolbar_bounds.x();
  int w = toolbar_bounds.width();
  int y = toolbar_bounds.y();
  int h = toolbar_bounds.height();

  // Gross hack: We split the toolbar images into two pieces, since sometimes
  // (popup mode) the toolbar isn't tall enough to show the whole image.  The
  // split happens between the top shadow section and the bottom gradient
  // section so that we never break the gradient.
  int split_point = kFrameShadowThickness * 2;
  int bottom_y = y + split_point;
  ui::ThemeProvider* tp = GetThemeProvider();
  gfx::ImageSkia* toolbar_left = tp->GetImageSkiaNamed(
      IDR_CONTENT_TOP_LEFT_CORNER);
  int bottom_edge_height = std::min(toolbar_left->height(), h) - split_point;

  // Split our canvas out so we can mask out the corners of the toolbar
  // without masking out the frame.
  canvas->SaveLayerAlpha(
      255, gfx::Rect(x - kClientEdgeThickness, y, w + kClientEdgeThickness * 3,
                     h));

  // Paint the bottom rect.
  canvas->FillRect(gfx::Rect(x, bottom_y, w, bottom_edge_height),
                   tp->GetColor(ThemeProperties::COLOR_TOOLBAR));

  // Tile the toolbar image starting at the frame edge on the left and where the
  // horizontal tabstrip is (or would be) on the top.
  gfx::ImageSkia* theme_toolbar = tp->GetImageSkiaNamed(IDR_THEME_TOOLBAR);
  canvas->TileImageInt(*theme_toolbar,
                       x + GetThemeBackgroundXInset(),
                       bottom_y - GetTabStripInsets(false).top,
                       x, bottom_y, w, theme_toolbar->height());

  // Draw rounded corners for the tab.
  gfx::ImageSkia* toolbar_left_mask =
      tp->GetImageSkiaNamed(IDR_CONTENT_TOP_LEFT_CORNER_MASK);
  gfx::ImageSkia* toolbar_right_mask =
      tp->GetImageSkiaNamed(IDR_CONTENT_TOP_RIGHT_CORNER_MASK);

  // We mask out the corners by using the DestinationIn transfer mode,
  // which keeps the RGB pixels from the destination and the alpha from
  // the source.
  SkPaint paint;
  paint.setXfermodeMode(SkXfermode::kDstIn_Mode);

  // Mask the left edge.
  int left_x = x - kContentEdgeShadowThickness;
  canvas->DrawImageInt(*toolbar_left_mask, 0, 0, toolbar_left_mask->width(),
                       split_point, left_x, y, toolbar_left_mask->width(),
                       split_point, false, paint);
  canvas->DrawImageInt(*toolbar_left_mask, 0,
      toolbar_left_mask->height() - bottom_edge_height,
      toolbar_left_mask->width(), bottom_edge_height, left_x, bottom_y,
      toolbar_left_mask->width(), bottom_edge_height, false, paint);

  // Mask the right edge.
  int right_x =
      x + w - toolbar_right_mask->width() + kContentEdgeShadowThickness;
  canvas->DrawImageInt(*toolbar_right_mask, 0, 0, toolbar_right_mask->width(),
                       split_point, right_x, y, toolbar_right_mask->width(),
                       split_point, false, paint);
  canvas->DrawImageInt(*toolbar_right_mask, 0,
      toolbar_right_mask->height() - bottom_edge_height,
      toolbar_right_mask->width(), bottom_edge_height, right_x, bottom_y,
      toolbar_right_mask->width(), bottom_edge_height, false, paint);
  canvas->Restore();

  canvas->DrawImageInt(*toolbar_left, 0, 0, toolbar_left->width(), split_point,
                       left_x, y, toolbar_left->width(), split_point, false);
  canvas->DrawImageInt(*toolbar_left, 0,
      toolbar_left->height() - bottom_edge_height, toolbar_left->width(),
      bottom_edge_height, left_x, bottom_y, toolbar_left->width(),
      bottom_edge_height, false);

  gfx::ImageSkia* toolbar_center =
      tp->GetImageSkiaNamed(IDR_CONTENT_TOP_CENTER);
  canvas->TileImageInt(*toolbar_center, 0, 0, left_x + toolbar_left->width(),
      y, right_x - (left_x + toolbar_left->width()),
      split_point);

  gfx::ImageSkia* toolbar_right = tp->GetImageSkiaNamed(
      IDR_CONTENT_TOP_RIGHT_CORNER);
  canvas->DrawImageInt(*toolbar_right, 0, 0, toolbar_right->width(),
      split_point, right_x, y, toolbar_right->width(), split_point, false);
  canvas->DrawImageInt(*toolbar_right, 0,
      toolbar_right->height() - bottom_edge_height, toolbar_right->width(),
      bottom_edge_height, right_x, bottom_y, toolbar_right->width(),
      bottom_edge_height, false);

  // Draw the content/toolbar separator.
  canvas->FillRect(
      gfx::Rect(x + kClientEdgeThickness,
                toolbar_bounds.bottom() - kClientEdgeThickness,
                w - (2 * kClientEdgeThickness),
                kClientEdgeThickness),
      ThemeProperties::GetDefaultColor(
          ThemeProperties::COLOR_TOOLBAR_SEPARATOR));
}

void OpaqueBrowserFrameView::PaintRestoredClientEdge(gfx::Canvas* canvas) {
  ui::ThemeProvider* tp = GetThemeProvider();
  int client_area_top = frame()->client_view()->y();
  int image_top = client_area_top;

  gfx::Rect client_area_bounds = CalculateClientAreaBounds(width(), height());
  SkColor toolbar_color = tp->GetColor(ThemeProperties::COLOR_TOOLBAR);

  if (browser_view()->IsToolbarVisible()) {
    // The client edge images always start below the toolbar corner images.  The
    // client edge filled rects start there or at the bottom of the toolbar,
    // whichever is shorter.
    gfx::Rect toolbar_bounds(browser_view()->GetToolbarBounds());
    image_top += toolbar_bounds.y() +
        tp->GetImageSkiaNamed(IDR_CONTENT_TOP_LEFT_CORNER)->height();
    client_area_top = std::min(image_top,
        client_area_top + toolbar_bounds.bottom() - kClientEdgeThickness);
  } else if (!browser_view()->IsTabStripVisible()) {
    // The toolbar isn't going to draw a client edge for us, so draw one
    // ourselves.
    gfx::ImageSkia* top_left = tp->GetImageSkiaNamed(IDR_APP_TOP_LEFT);
    gfx::ImageSkia* top_center = tp->GetImageSkiaNamed(IDR_APP_TOP_CENTER);
    gfx::ImageSkia* top_right = tp->GetImageSkiaNamed(IDR_APP_TOP_RIGHT);
    int top_edge_y = client_area_top - top_center->height();
    int height = client_area_top - top_edge_y;

    canvas->DrawImageInt(*top_left, 0, 0, top_left->width(), height,
        client_area_bounds.x() - top_left->width(), top_edge_y,
        top_left->width(), height, false);
    canvas->TileImageInt(*top_center, 0, 0, client_area_bounds.x(), top_edge_y,
      client_area_bounds.width(), std::min(height, top_center->height()));
    canvas->DrawImageInt(*top_right, 0, 0, top_right->width(), height,
        client_area_bounds.right(), top_edge_y,
        top_right->width(), height, false);

    // Draw the toolbar color across the top edge.
    canvas->FillRect(gfx::Rect(client_area_bounds.x() - kClientEdgeThickness,
        client_area_top - kClientEdgeThickness,
        client_area_bounds.width() + (2 * kClientEdgeThickness),
        kClientEdgeThickness), toolbar_color);
  }

  int client_area_bottom =
      std::max(client_area_top, height() - NonClientBorderThickness());
  int image_height = client_area_bottom - image_top;

  // Draw the client edge images.
  gfx::ImageSkia* right = tp->GetImageSkiaNamed(IDR_CONTENT_RIGHT_SIDE);
  canvas->TileImageInt(*right, client_area_bounds.right(), image_top,
                       right->width(), image_height);
  canvas->DrawImageInt(
      *tp->GetImageSkiaNamed(IDR_CONTENT_BOTTOM_RIGHT_CORNER),
      client_area_bounds.right(), client_area_bottom);
  gfx::ImageSkia* bottom = tp->GetImageSkiaNamed(IDR_CONTENT_BOTTOM_CENTER);
  canvas->TileImageInt(*bottom, client_area_bounds.x(),
      client_area_bottom, client_area_bounds.width(),
      bottom->height());
  gfx::ImageSkia* bottom_left =
      tp->GetImageSkiaNamed(IDR_CONTENT_BOTTOM_LEFT_CORNER);
  canvas->DrawImageInt(*bottom_left,
      client_area_bounds.x() - bottom_left->width(), client_area_bottom);
  gfx::ImageSkia* left = tp->GetImageSkiaNamed(IDR_CONTENT_LEFT_SIDE);
  canvas->TileImageInt(*left, client_area_bounds.x() - left->width(),
                       image_top, left->width(), image_height);

  // Draw the toolbar color so that the client edges show the right color even
  // where not covered by the toolbar image.  NOTE: We do this after drawing the
  // images because the images are meant to alpha-blend atop the frame whereas
  // these rects are meant to be fully opaque, without anything overlaid.
  canvas->FillRect(gfx::Rect(client_area_bounds.x() - kClientEdgeThickness,
      client_area_top, kClientEdgeThickness,
      client_area_bottom + kClientEdgeThickness - client_area_top),
       toolbar_color);
  canvas->FillRect(gfx::Rect(client_area_bounds.x(), client_area_bottom,
                             client_area_bounds.width(), kClientEdgeThickness),
                   toolbar_color);
  canvas->FillRect(gfx::Rect(client_area_bounds.right(), client_area_top,
      kClientEdgeThickness,
      client_area_bottom + kClientEdgeThickness - client_area_top),
      toolbar_color);
}

SkColor OpaqueBrowserFrameView::GetFrameColor() const {
  bool is_incognito = browser_view()->IsOffTheRecord();
  if (browser_view()->IsBrowserTypeNormal()) {
    if (ShouldPaintAsActive()) {
      return GetThemeProvider()->GetColor(is_incognito ?
          ThemeProperties::COLOR_FRAME_INCOGNITO :
          ThemeProperties::COLOR_FRAME);
    }
    return GetThemeProvider()->GetColor(is_incognito ?
        ThemeProperties::COLOR_FRAME_INCOGNITO_INACTIVE :
        ThemeProperties::COLOR_FRAME_INACTIVE);
  }
  // Never theme app and popup windows.
  if (ShouldPaintAsActive()) {
    return ThemeProperties::GetDefaultColor(is_incognito ?
        ThemeProperties::COLOR_FRAME_INCOGNITO : ThemeProperties::COLOR_FRAME);
  }
  return ThemeProperties::GetDefaultColor(is_incognito ?
      ThemeProperties::COLOR_FRAME_INCOGNITO_INACTIVE :
      ThemeProperties::COLOR_FRAME_INACTIVE);
}

gfx::ImageSkia* OpaqueBrowserFrameView::GetFrameImage() const {
  bool is_incognito = browser_view()->IsOffTheRecord();
  int resource_id;
  if (browser_view()->IsBrowserTypeNormal()) {
    if (ShouldPaintAsActive()) {
      resource_id = is_incognito ?
          IDR_THEME_FRAME_INCOGNITO : IDR_THEME_FRAME;
    } else {
      resource_id = is_incognito ?
          IDR_THEME_FRAME_INCOGNITO_INACTIVE : IDR_THEME_FRAME_INACTIVE;
    }
    return GetThemeProvider()->GetImageSkiaNamed(resource_id);
  }
  // Never theme app and popup windows.
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  if (ShouldPaintAsActive()) {
    resource_id = is_incognito ?
        IDR_THEME_FRAME_INCOGNITO : IDR_FRAME;
  } else {
    resource_id = is_incognito ?
        IDR_THEME_FRAME_INCOGNITO_INACTIVE : IDR_THEME_FRAME_INACTIVE;
  }
  return rb.GetImageSkiaNamed(resource_id);
}

gfx::ImageSkia* OpaqueBrowserFrameView::GetFrameOverlayImage() const {
  ui::ThemeProvider* tp = GetThemeProvider();
  if (tp->HasCustomImage(IDR_THEME_FRAME_OVERLAY) &&
      browser_view()->IsBrowserTypeNormal() &&
      !browser_view()->IsOffTheRecord()) {
    return tp->GetImageSkiaNamed(ShouldPaintAsActive() ?
        IDR_THEME_FRAME_OVERLAY : IDR_THEME_FRAME_OVERLAY_INACTIVE);
  }
  return NULL;
}

int OpaqueBrowserFrameView::GetTopAreaHeight() const {
  gfx::ImageSkia* frame_image = GetFrameImage();
  int top_area_height = frame_image->height();
  if (browser_view()->IsTabStripVisible()) {
    top_area_height = std::max(top_area_height,
      GetBoundsForTabStrip(browser_view()->tabstrip()).bottom());
  }
  return top_area_height;
}

void OpaqueBrowserFrameView::LayoutWindowControls() {
  if (!ShouldAddDefaultCaptionButtons())
    return;
  bool is_maximized = frame()->IsMaximized();
  close_button_->SetImageAlignment(views::ImageButton::ALIGN_LEFT,
                                   views::ImageButton::ALIGN_BOTTOM);
  int caption_y = CaptionButtonY(false);
  // There should always be the same number of non-shadow pixels visible to the
  // side of the caption buttons.  In maximized mode we extend the rightmost
  // button to the screen corner to obey Fitts' Law.
  int right_extra_width = is_maximized ?
      (kFrameBorderThickness - kFrameShadowThickness) : 0;
  gfx::Size close_button_size = close_button_->GetPreferredSize();
  close_button_->SetBounds(width() - FrameBorderThickness(false) -
      right_extra_width - close_button_size.width(), caption_y,
      close_button_size.width() + right_extra_width,
      close_button_size.height());

  // When the window is restored, we show a maximized button; otherwise, we show
  // a restore button.
  bool is_restored = !is_maximized && !frame()->IsMinimized();
  views::ImageButton* invisible_button = is_restored ?
      restore_button_ : maximize_button_;
  invisible_button->SetVisible(false);

  views::ImageButton* visible_button = is_restored ?
      maximize_button_ : restore_button_;
  visible_button->SetVisible(true);
  visible_button->SetImageAlignment(views::ImageButton::ALIGN_LEFT,
                                    views::ImageButton::ALIGN_BOTTOM);
  gfx::Size visible_button_size = visible_button->GetPreferredSize();
  visible_button->SetBounds(close_button_->x() - visible_button_size.width(),
                            caption_y, visible_button_size.width(),
                            visible_button_size.height());

  minimize_button_->SetVisible(true);
  minimize_button_->SetImageAlignment(views::ImageButton::ALIGN_LEFT,
                                      views::ImageButton::ALIGN_BOTTOM);
  gfx::Size minimize_button_size = minimize_button_->GetPreferredSize();
  minimize_button_->SetBounds(
      visible_button->x() - minimize_button_size.width(), caption_y,
      minimize_button_size.width(),
      minimize_button_size.height());
}

void OpaqueBrowserFrameView::LayoutTitleBar() {
  const views::WidgetDelegate* delegate = frame()->widget_delegate();
  if (delegate) {
    gfx::Rect icon_bounds(IconBounds());
    if (delegate->ShouldShowWindowIcon())
      window_icon_->SetBoundsRect(icon_bounds);

    window_title_->SetVisible(delegate->ShouldShowWindowTitle());
    if (delegate->ShouldShowWindowTitle()) {
      window_title_->SetText(delegate->GetWindowTitle());
      const int title_x = delegate->ShouldShowWindowIcon() ?
          icon_bounds.right() + kIconTitleSpacing : icon_bounds.x();
      window_title_->SetBounds(title_x, icon_bounds.y(),
          std::max(0, minimize_button_->x() - kTitleLogoSpacing - title_x),
          icon_bounds.height());
    }
  }
}

void OpaqueBrowserFrameView::LayoutAvatar() {
  // Even though the avatar is used for both incognito and profiles we always
  // use the incognito icon to layout the avatar button. The profile icon
  // can be customized so we can't depend on its size to perform layout.
  gfx::ImageSkia incognito_icon = browser_view()->GetOTRAvatarIcon();

  int avatar_bottom = GetTabStripInsets(false).top +
      browser_view()->GetTabStripHeight() - kAvatarBottomSpacing;
  int avatar_restored_y = avatar_bottom - incognito_icon.height();
  int avatar_y = frame()->IsMaximized() ?
      (NonClientTopBorderHeight(false) + kTabstripTopShadowThickness) :
      avatar_restored_y;
  avatar_bounds_.SetRect(NonClientBorderThickness() + kAvatarLeftSpacing,
      avatar_y, incognito_icon.width(),
      browser_view()->ShouldShowAvatar() ? (avatar_bottom - avatar_y) : 0);
  if (avatar_button())
    avatar_button()->SetBoundsRect(avatar_bounds_);

  if (avatar_label()) {
    // Space between the bottom of the avatar and the bottom of the avatar
    // label.
    const int kAvatarLabelBottomSpacing = 3;
    // Space between the frame border and the left edge of the avatar label.
    const int kAvatarLabelLeftSpacing = -1;
    gfx::Size label_size = avatar_label()->GetPreferredSize();
    gfx::Rect label_bounds(
        FrameBorderThickness(false) + kAvatarLabelLeftSpacing,
        avatar_bottom - kAvatarLabelBottomSpacing - label_size.height(),
        label_size.width(),
        browser_view()->ShouldShowAvatar() ? label_size.height() : 0);
    avatar_label()->SetBoundsRect(label_bounds);
  }
}

gfx::Rect OpaqueBrowserFrameView::CalculateClientAreaBounds(int width,
                                                            int height) const {
  int top_height = NonClientTopBorderHeight(false);
  int border_thickness = NonClientBorderThickness();
  return gfx::Rect(border_thickness, top_height,
                   std::max(0, width - (2 * border_thickness)),
                   std::max(0, height - GetReservedHeight() -
                       top_height - border_thickness));
}
