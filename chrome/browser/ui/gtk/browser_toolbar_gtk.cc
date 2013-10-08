// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/browser_toolbar_gtk.h"

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <X11/XF86keysym.h>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/debug/trace_event.h"
#include "base/i18n/rtl.h"
#include "base/logging.h"
#include "base/memory/singleton.h"
#include "base/path_service.h"
#include "base/prefs/pref_service.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/global_error/global_error.h"
#include "chrome/browser/ui/global_error/global_error_service.h"
#include "chrome/browser/ui/global_error/global_error_service_factory.h"
#include "chrome/browser/ui/gtk/accelerators_gtk.h"
#include "chrome/browser/ui/gtk/back_forward_button_gtk.h"
#include "chrome/browser/ui/gtk/bookmarks/bookmark_sub_menu_model_gtk.h"
#include "chrome/browser/ui/gtk/browser_actions_toolbar_gtk.h"
#include "chrome/browser/ui/gtk/browser_window_gtk.h"
#include "chrome/browser/ui/gtk/custom_button.h"
#include "chrome/browser/ui/gtk/event_utils.h"
#include "chrome/browser/ui/gtk/gtk_chrome_button.h"
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/browser/ui/gtk/location_bar_view_gtk.h"
#include "chrome/browser/ui/gtk/reload_button_gtk.h"
#include "chrome/browser/ui/gtk/rounded_window.h"
#include "chrome/browser/ui/gtk/tabs/tab_strip_gtk.h"
#include "chrome/browser/ui/gtk/view_id_util.h"
#include "chrome/browser/ui/toolbar/encoding_menu_controller.h"
#include "chrome/browser/upgrade_detector.h"
#include "chrome/common/net/url_fixer_upper.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/dragdrop/gtk_dnd_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas_skia_paint.h"
#include "ui/gfx/gtk_util.h"
#include "ui/gfx/image/cairo_cached_surface.h"
#include "ui/gfx/skbitmap_operations.h"

using content::HostZoomMap;
using content::UserMetricsAction;
using content::WebContents;

namespace {

// Padding on left and right of the left toolbar buttons (back, forward, reload,
// etc.).
const int kToolbarLeftAreaPadding = 4;

// Height of the toolbar in pixels (not counting padding).
const int kToolbarHeight = 29;

// Padding within the toolbar above the buttons and location bar.
const int kTopBottomPadding = 3;

// Height of the toolbar in pixels when we only show the location bar.
const int kToolbarHeightLocationBarOnly = kToolbarHeight - 2;

// Interior spacing between toolbar widgets.
const int kToolbarWidgetSpacing = 1;

// Amount of rounding on top corners of toolbar. Only used in Gtk theme mode.
const int kToolbarCornerSize = 3;

void SetWidgetHeightRequest(GtkWidget* widget, gpointer user_data) {
  gtk_widget_set_size_request(widget, -1, GPOINTER_TO_INT(user_data));
}

}  // namespace

// BrowserToolbarGtk, public ---------------------------------------------------

BrowserToolbarGtk::BrowserToolbarGtk(Browser* browser, BrowserWindowGtk* window)
    : toolbar_(NULL),
      location_bar_(new LocationBarViewGtk(browser)),
      model_(browser->toolbar_model()),
      is_wrench_menu_model_valid_(true),
      browser_(browser),
      window_(window),
      zoom_callback_(base::Bind(&BrowserToolbarGtk::OnZoomLevelChanged,
                                base::Unretained(this))) {
  wrench_menu_model_.reset(new WrenchMenuModel(this, browser_, false));

  chrome::AddCommandObserver(browser_, IDC_BACK, this);
  chrome::AddCommandObserver(browser_, IDC_FORWARD, this);
  chrome::AddCommandObserver(browser_, IDC_HOME, this);
  chrome::AddCommandObserver(browser_, IDC_BOOKMARK_PAGE, this);

  registrar_.Add(this,
                 chrome::NOTIFICATION_UPGRADE_RECOMMENDED,
                 content::NotificationService::AllSources());
  registrar_.Add(this,
                 chrome::NOTIFICATION_GLOBAL_ERRORS_CHANGED,
                 content::Source<Profile>(browser_->profile()));
}

BrowserToolbarGtk::~BrowserToolbarGtk() {
  chrome::RemoveCommandObserver(browser_, IDC_BACK, this);
  chrome::RemoveCommandObserver(browser_, IDC_FORWARD, this);
  chrome::RemoveCommandObserver(browser_, IDC_HOME, this);
  chrome::RemoveCommandObserver(browser_, IDC_BOOKMARK_PAGE, this);

  offscreen_entry_.Destroy();

  wrench_menu_.reset();

  HostZoomMap::GetForBrowserContext(
      browser()->profile())->RemoveZoomLevelChangedCallback(zoom_callback_);
}

void BrowserToolbarGtk::Init(GtkWindow* top_level_window) {
  Profile* profile = browser_->profile();
  theme_service_ = GtkThemeService::GetFrom(profile);
  registrar_.Add(this,
                 chrome::NOTIFICATION_BROWSER_THEME_CHANGED,
                 content::Source<ThemeService>(theme_service_));

  offscreen_entry_.Own(gtk_entry_new());

  base::Closure callback =
      base::Bind(&BrowserToolbarGtk::SetUpDragForHomeButton,
                 base::Unretained(this));

  show_home_button_.Init(prefs::kShowHomeButton, profile->GetPrefs(),
                         base::Bind(&BrowserToolbarGtk::UpdateShowHomeButton,
                                    base::Unretained(this)));
  home_page_.Init(prefs::kHomePage, profile->GetPrefs(), callback);
  home_page_is_new_tab_page_.Init(prefs::kHomePageIsNewTabPage,
                                  profile->GetPrefs(),
                                  callback);

  event_box_ = gtk_event_box_new();
  // Make the event box transparent so themes can use transparent toolbar
  // backgrounds.
  if (!theme_service_->UsingNativeTheme())
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(event_box_), FALSE);

  toolbar_ = gtk_hbox_new(FALSE, 0);
  alignment_ = gtk_alignment_new(0.0, 0.0, 1.0, 1.0);
  UpdateForBookmarkBarVisibility(false);
  g_signal_connect(alignment_, "expose-event",
                   G_CALLBACK(&OnAlignmentExposeThunk), this);
  gtk_container_add(GTK_CONTAINER(event_box_), alignment_);
  gtk_container_add(GTK_CONTAINER(alignment_), toolbar_);

  toolbar_left_ = gtk_hbox_new(FALSE, kToolbarWidgetSpacing);

  GtkSizeGroup* size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  back_.reset(new BackForwardButtonGtk(browser_, false));
  g_signal_connect(back_->widget(), "clicked",
                   G_CALLBACK(OnButtonClickThunk), this);
  gtk_size_group_add_widget(size_group, back_->widget());
  gtk_box_pack_start(GTK_BOX(toolbar_left_), back_->widget(), FALSE,
                     FALSE, 0);

  forward_.reset(new BackForwardButtonGtk(browser_, true));
  g_signal_connect(forward_->widget(), "clicked",
                   G_CALLBACK(OnButtonClickThunk), this);
  gtk_size_group_add_widget(size_group, forward_->widget());
  gtk_box_pack_start(GTK_BOX(toolbar_left_), forward_->widget(), FALSE,
                     FALSE, 0);

  reload_.reset(new ReloadButtonGtk(location_bar_.get(), browser_));
  gtk_size_group_add_widget(size_group, reload_->widget());
  gtk_box_pack_start(GTK_BOX(toolbar_left_), reload_->widget(), FALSE, FALSE,
                     0);

  home_.reset(new CustomDrawButton(theme_service_, IDR_HOME, IDR_HOME_P,
      IDR_HOME_H, 0, GTK_STOCK_HOME, GTK_ICON_SIZE_SMALL_TOOLBAR));
  gtk_widget_set_tooltip_text(home_->widget(),
      l10n_util::GetStringUTF8(IDS_TOOLTIP_HOME).c_str());
  g_signal_connect(home_->widget(), "clicked",
                   G_CALLBACK(OnButtonClickThunk), this);
  gtk_size_group_add_widget(size_group, home_->widget());
  gtk_box_pack_start(GTK_BOX(toolbar_left_), home_->widget(), FALSE, FALSE,
                     kToolbarWidgetSpacing);
  gtk_util::SetButtonTriggersNavigation(home_->widget());

  gtk_box_pack_start(GTK_BOX(toolbar_), toolbar_left_, FALSE, FALSE,
                     kToolbarLeftAreaPadding);

  g_object_unref(size_group);

  location_hbox_ = gtk_hbox_new(FALSE, 0);
  location_bar_->Init(ShouldOnlyShowLocation());
  gtk_box_pack_start(GTK_BOX(location_hbox_), location_bar_->widget(), TRUE,
                     TRUE, 0);

  g_signal_connect(location_hbox_, "expose-event",
                   G_CALLBACK(OnLocationHboxExposeThunk), this);
  gtk_box_pack_start(GTK_BOX(toolbar_), location_hbox_, TRUE, TRUE,
      ShouldOnlyShowLocation() ? 1 : 0);

  if (!ShouldOnlyShowLocation()) {
    actions_toolbar_.reset(new BrowserActionsToolbarGtk(browser_));
    gtk_box_pack_start(GTK_BOX(toolbar_), actions_toolbar_->widget(),
                       FALSE, FALSE, 0);
  }

  wrench_menu_image_ = gtk_image_new_from_pixbuf(
      theme_service_->GetRTLEnabledPixbufNamed(IDR_TOOLS));
  wrench_menu_button_.reset(new CustomDrawButton(theme_service_, IDR_TOOLS,
      IDR_TOOLS_P, IDR_TOOLS_H, 0, wrench_menu_image_));
  GtkWidget* wrench_button = wrench_menu_button_->widget();

  gtk_widget_set_tooltip_text(
      wrench_button,
      l10n_util::GetStringUTF8(IDS_APPMENU_TOOLTIP).c_str());
  g_signal_connect(wrench_button, "button-press-event",
                   G_CALLBACK(OnMenuButtonPressEventThunk), this);
  gtk_widget_set_can_focus(wrench_button, FALSE);

  // Put the wrench button in a box so that we can paint the update notification
  // over it.
  GtkWidget* wrench_box = gtk_alignment_new(0, 0, 1, 1);
  g_signal_connect_after(wrench_box, "expose-event",
                         G_CALLBACK(OnWrenchMenuButtonExposeThunk), this);
  gtk_container_add(GTK_CONTAINER(wrench_box), wrench_button);
  gtk_box_pack_start(GTK_BOX(toolbar_), wrench_box, FALSE, FALSE, 4);

  wrench_menu_.reset(new MenuGtk(this, wrench_menu_model_.get()));
  // The bookmark menu model needs to be able to force the wrench menu to close.
  wrench_menu_model_->bookmark_sub_menu_model()->SetMenuGtk(wrench_menu_.get());

  HostZoomMap::GetForBrowserContext(
      browser()->profile())->AddZoomLevelChangedCallback(zoom_callback_);

  if (ShouldOnlyShowLocation()) {
    gtk_widget_show(event_box_);
    gtk_widget_show(alignment_);
    gtk_widget_show(toolbar_);
    gtk_widget_show_all(location_hbox_);
    gtk_widget_hide(reload_->widget());
  } else {
    gtk_widget_show_all(event_box_);
    if (actions_toolbar_->button_count() == 0)
      gtk_widget_hide(actions_toolbar_->widget());
  }

  // Initialize pref-dependent UI state.
  UpdateShowHomeButton();
  SetUpDragForHomeButton();

  // Because the above does a recursive show all on all widgets we need to
  // update the icon visibility to hide them.
  location_bar_->UpdateContentSettingsIcons();

  SetViewIDs();
  theme_service_->InitThemesFor(this);
}

void BrowserToolbarGtk::SetViewIDs() {
  ViewIDUtil::SetID(widget(), VIEW_ID_TOOLBAR);
  ViewIDUtil::SetID(back_->widget(), VIEW_ID_BACK_BUTTON);
  ViewIDUtil::SetID(forward_->widget(), VIEW_ID_FORWARD_BUTTON);
  ViewIDUtil::SetID(reload_->widget(), VIEW_ID_RELOAD_BUTTON);
  ViewIDUtil::SetID(home_->widget(), VIEW_ID_HOME_BUTTON);
  ViewIDUtil::SetID(location_bar_->widget(), VIEW_ID_OMNIBOX);
  ViewIDUtil::SetID(wrench_menu_button_->widget(), VIEW_ID_APP_MENU);
}

void BrowserToolbarGtk::Show() {
  gtk_widget_show(toolbar_);
}

void BrowserToolbarGtk::Hide() {
  gtk_widget_hide(toolbar_);
}

LocationBar* BrowserToolbarGtk::GetLocationBar() const {
  return location_bar_.get();
}

void BrowserToolbarGtk::UpdateForBookmarkBarVisibility(
    bool show_bottom_padding) {
  gtk_alignment_set_padding(GTK_ALIGNMENT(alignment_),
      ShouldOnlyShowLocation() ? 0 : kTopBottomPadding,
      !show_bottom_padding || ShouldOnlyShowLocation() ? 0 : kTopBottomPadding,
      0, 0);
}

void BrowserToolbarGtk::ShowAppMenu() {
  wrench_menu_->Cancel();

  if (!is_wrench_menu_model_valid_)
    RebuildWrenchMenu();

  wrench_menu_button_->SetPaintOverride(GTK_STATE_ACTIVE);
  content::RecordAction(UserMetricsAction("ShowAppMenu"));
  wrench_menu_->PopupAsFromKeyEvent(wrench_menu_button_->widget());
}

// CommandObserver -------------------------------------------------------------

void BrowserToolbarGtk::EnabledStateChangedForCommand(int id, bool enabled) {
  GtkWidget* widget = NULL;
  switch (id) {
    case IDC_BACK:
      widget = back_->widget();
      break;
    case IDC_FORWARD:
      widget = forward_->widget();
      break;
    case IDC_HOME:
      if (home_.get())
        widget = home_->widget();
      break;
  }
  if (widget) {
    if (!enabled && gtk_widget_get_state(widget) == GTK_STATE_PRELIGHT) {
      // If we're disabling a widget, GTK will helpfully restore it to its
      // previous state when we re-enable it, even if that previous state
      // is the prelight.  This looks bad.  See the bug for a simple repro.
      // http://code.google.com/p/chromium/issues/detail?id=13729
      gtk_widget_set_state(widget, GTK_STATE_NORMAL);
    }
    gtk_widget_set_sensitive(widget, enabled);
  }
}

// MenuGtk::Delegate -----------------------------------------------------------

void BrowserToolbarGtk::StoppedShowing() {
  // Without these calls, the hover state can get stuck since the leave-notify
  // event is not sent when clicking a button brings up the menu.
  gtk_chrome_button_set_hover_state(
      GTK_CHROME_BUTTON(wrench_menu_button_->widget()), 0.0);
  wrench_menu_button_->UnsetPaintOverride();
}

GtkIconSet* BrowserToolbarGtk::GetIconSetForId(int idr) {
  return theme_service_->GetIconSetForId(idr);
}

// Always show images because we desire that some icons always show
// regardless of the system setting.
bool BrowserToolbarGtk::AlwaysShowIconForCmd(int command_id) const {
  return command_id == IDC_UPGRADE_DIALOG ||
      BookmarkSubMenuModel::IsBookmarkItemCommandId(command_id);
}

// ui::AcceleratorProvider

bool BrowserToolbarGtk::GetAcceleratorForCommandId(
    int id,
    ui::Accelerator* out_accelerator) {
  const ui::Accelerator* accelerator =
      AcceleratorsGtk::GetInstance()->GetPrimaryAcceleratorForCommand(id);
  if (!accelerator)
    return false;
  *out_accelerator = *accelerator;
  return true;
}

// content::NotificationObserver -----------------------------------------------

void BrowserToolbarGtk::Observe(int type,
                                const content::NotificationSource& source,
                                const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_BROWSER_THEME_CHANGED) {
    // Update the spacing around the menu buttons
    bool use_gtk = theme_service_->UsingNativeTheme();
    int border = use_gtk ? 0 : 2;
    gtk_container_set_border_width(
        GTK_CONTAINER(wrench_menu_button_->widget()), border);

    // Force the height of the toolbar so we get the right amount of padding
    // above and below the location bar. We always force the size of the widgets
    // to either side of the location box, but we only force the location box
    // size in chrome-theme mode because that's the only time we try to control
    // the font size.
    int toolbar_height = ShouldOnlyShowLocation() ?
                         kToolbarHeightLocationBarOnly : kToolbarHeight;
    gtk_container_foreach(GTK_CONTAINER(toolbar_), SetWidgetHeightRequest,
                          GINT_TO_POINTER(toolbar_height));
    gtk_widget_set_size_request(location_hbox_, -1,
                                use_gtk ? -1 : toolbar_height);

    // When using the GTK+ theme, we need to have the event box be visible so
    // buttons don't get a halo color from the background.  When using Chromium
    // themes, we want to let the background show through the toolbar.
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(event_box_), use_gtk);

    if (use_gtk) {
      // We need to manually update the icon if we are in GTK mode. (Note that
      // we set the initial value in Init()).
      gtk_image_set_from_pixbuf(
          GTK_IMAGE(wrench_menu_image_),
          theme_service_->GetRTLEnabledPixbufNamed(IDR_TOOLS));
    }

    UpdateRoundedness();
  } else if (type == chrome::NOTIFICATION_UPGRADE_RECOMMENDED) {
    // Redraw the wrench menu to update the badge.
    gtk_widget_queue_draw(wrench_menu_button_->widget());
  } else if (type == chrome::NOTIFICATION_GLOBAL_ERRORS_CHANGED) {
    is_wrench_menu_model_valid_ = false;
    gtk_widget_queue_draw(wrench_menu_button_->widget());
  } else {
    NOTREACHED();
  }
}

// BrowserToolbarGtk, public ---------------------------------------------------

void BrowserToolbarGtk::UpdateWebContents(WebContents* contents,
                                          bool should_restore_state) {
  location_bar_->Update(should_restore_state ? contents : NULL);

  if (actions_toolbar_.get())
    actions_toolbar_->Update();
}

bool BrowserToolbarGtk::IsWrenchMenuShowing() const {
  return wrench_menu_.get() && gtk_widget_get_visible(wrench_menu_->widget());
}

// BrowserToolbarGtk, private --------------------------------------------------

void BrowserToolbarGtk::OnZoomLevelChanged(
    const HostZoomMap::ZoomLevelChange& change) {
  // Since BrowserToolbarGtk create a new |wrench_menu_model_| in
  // RebuildWrenchMenu(), the ordering of the observers of HostZoomMap
  // can change, and result in subtle bugs like http://crbug.com/118823.
  // Rather than depending on the ordering of the observers, always update
  // the WrenchMenuModel before updating the WrenchMenu.
  wrench_menu_model_->UpdateZoomControls();

  // If our zoom level changed, we need to tell the menu to update its state,
  // since the menu could still be open.
  wrench_menu_->UpdateMenu();
}


void BrowserToolbarGtk::SetUpDragForHomeButton() {
  if (!home_page_.IsManaged() && !home_page_is_new_tab_page_.IsManaged()) {
    gtk_drag_dest_set(home_->widget(), GTK_DEST_DEFAULT_ALL,
                      NULL, 0, GDK_ACTION_COPY);
    static const int targets[] = { ui::TEXT_PLAIN, ui::TEXT_URI_LIST, -1 };
    ui::SetDestTargetList(home_->widget(), targets);

    drop_handler_.reset(new ui::GtkSignalRegistrar());
    drop_handler_->Connect(home_->widget(), "drag-data-received",
                           G_CALLBACK(OnDragDataReceivedThunk), this);
  } else {
    gtk_drag_dest_unset(home_->widget());
    drop_handler_.reset(NULL);
  }
}

bool BrowserToolbarGtk::UpdateRoundedness() {
  // We still round the corners if we are in chrome theme mode, but we do it by
  // drawing theme resources rather than changing the physical shape of the
  // widget.
  bool should_be_rounded = theme_service_->UsingNativeTheme() &&
      window_->ShouldDrawContentDropShadow();

  if (should_be_rounded == gtk_util::IsActingAsRoundedWindow(alignment_))
    return false;

  if (should_be_rounded) {
    gtk_util::ActAsRoundedWindow(alignment_, GdkColor(), kToolbarCornerSize,
                                 gtk_util::ROUNDED_TOP,
                                 gtk_util::BORDER_NONE);
  } else {
    gtk_util::StopActingAsRoundedWindow(alignment_);
  }

  return true;
}

gboolean BrowserToolbarGtk::OnAlignmentExpose(GtkWidget* widget,
                                              GdkEventExpose* e) {
  TRACE_EVENT0("ui::gtk", "BrowserToolbarGtk::OnAlignmentExpose");

  // We may need to update the roundedness of the toolbar's top corners. In
  // this case, don't draw; we'll be called again soon enough.
  if (UpdateRoundedness())
    return TRUE;

  // We don't need to render the toolbar image in GTK mode.
  if (theme_service_->UsingNativeTheme())
    return FALSE;

  cairo_t* cr = gdk_cairo_create(gtk_widget_get_window(widget));
  gdk_cairo_rectangle(cr, &e->area);
  cairo_clip(cr);

  gfx::Point tabstrip_origin =
      window_->tabstrip()->GetTabStripOriginForWidget(widget);
  // Fill the entire region with the toolbar color.
  GdkColor color = theme_service_->GetGdkColor(
      ThemeProperties::COLOR_TOOLBAR);
  gdk_cairo_set_source_color(cr, &color);
  cairo_fill(cr);

  // The horizontal size of the top left and right corner images.
  const int kCornerWidth = 4;
  // The thickness of the shadow outside the toolbar's bounds; the offset
  // between the edge of the toolbar and where we anchor the corner images.
  const int kShadowThickness = 2;

  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  gfx::Rect area(e->area);
  gfx::Rect right(allocation.x + allocation.width - kCornerWidth,
                  allocation.y - kShadowThickness,
                  kCornerWidth,
                  allocation.height + kShadowThickness);
  gfx::Rect left(allocation.x - kShadowThickness,
                 allocation.y - kShadowThickness,
                 kCornerWidth,
                 allocation.height + kShadowThickness);

  if (window_->ShouldDrawContentDropShadow()) {
    // Leave room to draw rounded corners.
    area.Subtract(right);
    area.Subtract(left);
  }

  gfx::Image background = theme_service_->GetImageNamed(IDR_THEME_TOOLBAR);
  background.ToCairo()->SetSource(
      cr, widget, tabstrip_origin.x(), tabstrip_origin.y());
  cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
  cairo_rectangle(cr, area.x(), area.y(), area.width(), area.height());
  cairo_fill(cr);

  if (!window_->ShouldDrawContentDropShadow()) {
    // The rest of this function is for rounded corners. Our work is done here.
    cairo_destroy(cr);
    return FALSE;
  }

  bool draw_left_corner = left.Intersects(gfx::Rect(e->area));
  bool draw_right_corner = right.Intersects(gfx::Rect(e->area));

  if (draw_left_corner || draw_right_corner) {
    // Create a mask which is composed of the left and/or right corners.
    cairo_surface_t* target = cairo_surface_create_similar(
        cairo_get_target(cr),
        CAIRO_CONTENT_COLOR_ALPHA,
        allocation.x + allocation.width,
        allocation.y + allocation.height);
    cairo_t* copy_cr = cairo_create(target);

    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();

    cairo_set_operator(copy_cr, CAIRO_OPERATOR_SOURCE);
    if (draw_left_corner) {
      rb.GetNativeImageNamed(IDR_CONTENT_TOP_LEFT_CORNER_MASK).ToCairo()->
          SetSource(copy_cr, widget, left.x(), left.y());
      cairo_paint(copy_cr);
    }
    if (draw_right_corner) {
      rb.GetNativeImageNamed(IDR_CONTENT_TOP_RIGHT_CORNER_MASK).ToCairo()->
          SetSource(copy_cr, widget, right.x(), right.y());
      // We fill a path rather than just painting because we don't want to
      // overwrite the left corner.
      cairo_rectangle(copy_cr, right.x(), right.y(),
                      right.width(), right.height());
      cairo_fill(copy_cr);
    }

    // Draw the background. CAIRO_OPERATOR_IN uses the existing pixel data as
    // an alpha mask.
    background.ToCairo()->SetSource(copy_cr, widget,
                                     tabstrip_origin.x(), tabstrip_origin.y());
    cairo_set_operator(copy_cr, CAIRO_OPERATOR_IN);
    cairo_pattern_set_extend(cairo_get_source(copy_cr), CAIRO_EXTEND_REPEAT);
    cairo_paint(copy_cr);
    cairo_destroy(copy_cr);

    // Copy the temporary surface to the screen.
    cairo_set_source_surface(cr, target, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(target);
  }

  cairo_destroy(cr);

  return FALSE;  // Allow subwidgets to paint.
}

gboolean BrowserToolbarGtk::OnLocationHboxExpose(GtkWidget* location_hbox,
                                                 GdkEventExpose* e) {
  TRACE_EVENT0("ui::gtk", "BrowserToolbarGtk::OnLocationHboxExpose");
  if (theme_service_->UsingNativeTheme()) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(location_hbox, &allocation);
    gtk_util::DrawTextEntryBackground(offscreen_entry_.get(),
                                      location_hbox, &e->area,
                                      &allocation);
  }

  return FALSE;
}

void BrowserToolbarGtk::OnButtonClick(GtkWidget* button) {
  if ((button == back_->widget()) || (button == forward_->widget())) {
    if (event_utils::DispositionForCurrentButtonPressEvent() == CURRENT_TAB)
      location_bar_->Revert();
    return;
  }

  DCHECK(home_.get() && button == home_->widget()) <<
      "Unexpected button click callback";
  chrome::Home(browser_, event_utils::DispositionForCurrentButtonPressEvent());
}

gboolean BrowserToolbarGtk::OnMenuButtonPressEvent(GtkWidget* button,
                                                   GdkEventButton* event) {
  if (event->button != 1)
    return FALSE;

  if (!is_wrench_menu_model_valid_)
    RebuildWrenchMenu();

  wrench_menu_button_->SetPaintOverride(GTK_STATE_ACTIVE);
  wrench_menu_->PopupForWidget(button, event->button, event->time);

  return TRUE;
}

void BrowserToolbarGtk::OnDragDataReceived(GtkWidget* widget,
    GdkDragContext* drag_context, gint x, gint y,
    GtkSelectionData* data, guint info, guint time) {
  if (info != ui::TEXT_PLAIN) {
    NOTIMPLEMENTED() << "Only support plain text drops for now, sorry!";
    return;
  }

  GURL url(reinterpret_cast<const char*>(gtk_selection_data_get_data(data)));
  if (!url.is_valid())
    return;

  bool url_is_newtab = url.SchemeIs(chrome::kChromeUIScheme) &&
                       url.host() == chrome::kChromeUINewTabHost;
  home_page_is_new_tab_page_.SetValue(url_is_newtab);
  if (!url_is_newtab)
    home_page_.SetValue(url.spec());
}

bool BrowserToolbarGtk::ShouldOnlyShowLocation() const {
  // If we're a popup window, only show the location bar (omnibox).
  return !browser_->is_type_tabbed();
}

void BrowserToolbarGtk::RebuildWrenchMenu() {
  wrench_menu_model_.reset(new WrenchMenuModel(this, browser_, false));
  wrench_menu_.reset(new MenuGtk(this, wrench_menu_model_.get()));
  // The bookmark menu model needs to be able to force the wrench menu to close.
  wrench_menu_model_->bookmark_sub_menu_model()->SetMenuGtk(wrench_menu_.get());
  is_wrench_menu_model_valid_ = true;
}

gboolean BrowserToolbarGtk::OnWrenchMenuButtonExpose(GtkWidget* sender,
                                                     GdkEventExpose* expose) {
  TRACE_EVENT0("ui::gtk", "BrowserToolbarGtk::OnWrenchMenuButtonExpose");
  int resource_id = 0;
  if (UpgradeDetector::GetInstance()->notify_upgrade()) {
    resource_id = UpgradeDetector::GetInstance()->GetIconResourceID(
            UpgradeDetector::UPGRADE_ICON_TYPE_BADGE);
  } else {
    GlobalError* error = GlobalErrorServiceFactory::GetForProfile(
        browser_->profile())->GetHighestSeverityGlobalErrorWithWrenchMenuItem();
    if (error) {
      switch (error->GetSeverity()) {
        case GlobalError::SEVERITY_LOW:
          resource_id = IDR_UPDATE_BADGE;
          break;
        case GlobalError::SEVERITY_MEDIUM:
          resource_id = IDR_UPDATE_BADGE4;
          break;
        case GlobalError::SEVERITY_HIGH:
          resource_id = IDR_UPDATE_BADGE3;
          break;
      }
    }
  }

  if (!resource_id)
    return FALSE;

  GtkAllocation allocation;
  gtk_widget_get_allocation(sender, &allocation);

  // Draw the chrome app menu icon onto the canvas.
  const gfx::ImageSkia* badge = theme_service_->GetImageSkiaNamed(resource_id);
  gfx::CanvasSkiaPaint canvas(expose, false);
  int x_offset = base::i18n::IsRTL() ? 0 : allocation.width - badge->width();
  int y_offset = 0;
  canvas.DrawImageInt(*badge,
                      allocation.x + x_offset,
                      allocation.y + y_offset);

  return FALSE;
}

void BrowserToolbarGtk::UpdateShowHomeButton() {
  bool visible = show_home_button_.GetValue() && !ShouldOnlyShowLocation();
  gtk_widget_set_visible(home_->widget(), visible);
}
