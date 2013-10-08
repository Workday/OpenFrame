// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/browser_titlebar.h"

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/i18n/rtl.h"
#include "base/memory/singleton.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/profiles/avatar_menu_model.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_info_cache.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/gtk/accelerators_gtk.h"
#include "chrome/browser/ui/gtk/avatar_menu_bubble_gtk.h"
#include "chrome/browser/ui/gtk/avatar_menu_button_gtk.h"
#include "chrome/browser/ui/gtk/browser_window_gtk.h"
#include "chrome/browser/ui/gtk/custom_button.h"
#if defined(USE_GCONF)
#include "chrome/browser/ui/gtk/gconf_titlebar_listener.h"
#endif
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/browser/ui/gtk/menu_gtk.h"
#include "chrome/browser/ui/gtk/nine_box.h"
#include "chrome/browser/ui/gtk/tabs/tab_strip_gtk.h"
#include "chrome/browser/ui/gtk/unity_service.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/encoding_menu_controller.h"
#include "chrome/browser/ui/toolbar/wrench_menu_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/ui_resources.h"
#include "ui/base/gtk/gtk_hig_constants.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/x/active_window_watcher_x.h"
#include "ui/gfx/image/image.h"

using content::WebContents;

namespace {

// The space above the titlebars.
const int kTitlebarHeight = 14;

// The thickness in pixels of the tab border.
const int kTabOuterThickness = 1;

// Amount to offset the tab images relative to the background.
const int kNormalVerticalOffset = kTitlebarHeight + kTabOuterThickness;

// A linux specific menu item for toggling window decorations.
const int kShowWindowDecorationsCommand = 200;

const int kAvatarBottomSpacing = 1;
// There are 2 px on each side of the avatar (between the frame border and
// it on the left, and between it and the tabstrip on the right).
const int kAvatarSideSpacing = 2;

// The thickness of the custom frame border; we need it here to enlarge the
// close button whent the custom frame border isn't showing but the custom
// titlebar is showing.
const int kFrameBorderThickness = 4;

// There is a 4px gap between the icon and the title text.
const int kIconTitleSpacing = 4;

// Padding around the icon when in app mode or popup mode.
const int kAppModePaddingTop = 5;
const int kAppModePaddingBottom = 4;
const int kAppModePaddingLeft = 2;

// The left padding of the tab strip.  In Views, the tab strip has a left
// margin of FrameBorderThickness + kClientEdgeThickness.  This offset is to
// account for kClientEdgeThickness.
const int kTabStripLeftPadding = 1;

// Spacing between buttons of the titlebar.
const int kButtonSpacing = 2;

// Spacing around outside of titlebar buttons.
const int kButtonOuterPadding = 2;

// Spacing between tabstrip and window control buttons (when the window is
// maximized).
const int kMaximizedTabstripPadding = 16;

gboolean OnMouseMoveEvent(GtkWidget* widget, GdkEventMotion* event,
                          BrowserWindowGtk* browser_window) {
  // Reset to the default mouse cursor.
  browser_window->ResetCustomFrameCursor();
  return TRUE;
}

// Converts a GdkColor to a color_utils::HSL.
color_utils::HSL GdkColorToHSL(const GdkColor* color) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(SkColorSetRGB(color->red >> 8,
                                          color->green >> 8,
                                          color->blue >> 8), &hsl);
  return hsl;
}

// Returns either |one| or |two| based on which has a greater difference in
// luminosity.
GdkColor PickLuminosityContrastingColor(const GdkColor* base,
                                        const GdkColor* one,
                                        const GdkColor* two) {
  // Convert all GdkColors to color_utils::HSLs.
  color_utils::HSL baseHSL = GdkColorToHSL(base);
  color_utils::HSL oneHSL = GdkColorToHSL(one);
  color_utils::HSL twoHSL = GdkColorToHSL(two);
  double one_difference = fabs(baseHSL.l - oneHSL.l);
  double two_difference = fabs(baseHSL.l - twoHSL.l);

  // Be biased towards the first color presented.
  if (two_difference > one_difference + 0.1)
    return *two;
  else
    return *one;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// PopupPageMenuModel

// A menu model that builds the contents of the menu shown for popups (when the
// user clicks on the favicon) and all of its submenus.
class PopupPageMenuModel : public ui::SimpleMenuModel {
 public:
  PopupPageMenuModel(ui::SimpleMenuModel::Delegate* delegate, Browser* browser);
  virtual ~PopupPageMenuModel() { }

 private:
  void Build();

  // Models for submenus referenced by this model. SimpleMenuModel only uses
  // weak references so these must be kept for the lifetime of the top-level
  // model.
  scoped_ptr<ZoomMenuModel> zoom_menu_model_;
  scoped_ptr<EncodingMenuModel> encoding_menu_model_;
  Browser* browser_;  // weak

  DISALLOW_COPY_AND_ASSIGN(PopupPageMenuModel);
};

PopupPageMenuModel::PopupPageMenuModel(
    ui::SimpleMenuModel::Delegate* delegate,
    Browser* browser)
    : ui::SimpleMenuModel(delegate), browser_(browser) {
  Build();
}

void PopupPageMenuModel::Build() {
  AddItemWithStringId(IDC_BACK, IDS_CONTENT_CONTEXT_BACK);
  AddItemWithStringId(IDC_FORWARD, IDS_CONTENT_CONTEXT_FORWARD);
  AddItemWithStringId(IDC_RELOAD, IDS_APP_MENU_RELOAD);
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddItemWithStringId(IDC_SHOW_AS_TAB, IDS_SHOW_AS_TAB);
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddItemWithStringId(IDC_CUT, IDS_CUT);
  AddItemWithStringId(IDC_COPY, IDS_COPY);
  AddItemWithStringId(IDC_PASTE, IDS_PASTE);
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddItemWithStringId(IDC_FIND, IDS_FIND);
  AddItemWithStringId(IDC_PRINT, IDS_PRINT);
  zoom_menu_model_.reset(new ZoomMenuModel(delegate()));
  AddSubMenuWithStringId(IDC_ZOOM_MENU, IDS_ZOOM_MENU, zoom_menu_model_.get());

  encoding_menu_model_.reset(new EncodingMenuModel(browser_));
  AddSubMenuWithStringId(IDC_ENCODING_MENU, IDS_ENCODING_MENU,
                         encoding_menu_model_.get());

  AddSeparator(ui::NORMAL_SEPARATOR);
  AddItemWithStringId(IDC_CLOSE_WINDOW, IDS_CLOSE);
}

////////////////////////////////////////////////////////////////////////////////
// BrowserTitlebar

// static
const char BrowserTitlebar::kDefaultButtonString[] = ":minimize,maximize,close";

BrowserTitlebar::BrowserTitlebar(BrowserWindowGtk* browser_window,
                                 GtkWindow* window)
    : browser_window_(browser_window),
      window_(window),
      container_(NULL),
      container_hbox_(NULL),
      titlebar_left_buttons_vbox_(NULL),
      titlebar_right_buttons_vbox_(NULL),
      titlebar_left_buttons_hbox_(NULL),
      titlebar_right_buttons_hbox_(NULL),
      titlebar_left_avatar_frame_(NULL),
      titlebar_right_avatar_frame_(NULL),
      titlebar_left_label_frame_(NULL),
      titlebar_right_label_frame_(NULL),
      avatar_(NULL),
      avatar_label_(NULL),
      avatar_label_bg_(NULL),
      top_padding_left_(NULL),
      top_padding_right_(NULL),
      titlebar_alignment_(NULL),
      app_mode_favicon_(NULL),
      app_mode_title_(NULL),
      using_custom_frame_(false),
      window_has_focus_(false),
      display_avatar_on_left_(false),
      theme_service_(NULL) {
}

void BrowserTitlebar::Init() {
  // The widget hierarchy is shown below.
  //
  // +- EventBox (container_) ------------------------------------------------+
  // +- HBox (container_hbox_) -----------------------------------------------+
  // |+ VBox ---++- Algn. -++- Alignment --------------++- Algn. -++ VBox ---+|
  // || titlebar||titlebar ||   (titlebar_alignment_)  ||titlebar || titlebar||
  // || left    ||left     ||                          ||right    || right   ||
  // || button  ||spy      ||                          ||spy      || button  ||
  // || vbox    ||frame    ||+- TabStripGtk  ---------+||frame    || vbox    ||
  // ||         ||         || tab   tab   tabclose    |||         ||         ||
  // ||         ||         ||+------------------------+||         ||         ||
  // |+---------++---------++--------------------------++---------++---------+|
  // +------------------------------------------------------------------------+
  //
  // There are two vboxes on either side of |container_hbox_| because when the
  // desktop is GNOME, the button placement is configurable based on a metacity
  // gconf key. We can't just have one vbox and move it back and forth because
  // the gconf language allows you to place buttons on both sides of the
  // window.  This being Linux, I'm sure there's a bunch of people who have
  // already configured their window manager to do so and we don't want to get
  // confused when that happens. The actual contents of these vboxes are lazily
  // generated so they don't interfere with alignment when everything is
  // stuffed in the other box.
  //
  // Each vbox has the following hierarchy if it contains buttons:
  //
  // +- VBox (titlebar_{l,r}_buttons_vbox_) ---------+
  // |+- Fixed (top_padding_{l,r}_) ----------------+|
  // ||+- HBox (titlebar_{l,r}_buttons_hbox_ ------+||
  // ||| (buttons of a configurable layout)        |||
  // ||+-------------------------------------------+||
  // |+---------------------------------------------+|
  // +-----------------------------------------------+
  //
  // The two spy alignments are only allocated if this window is an incognito
  // window. Only one of them holds the spy image.
  //
  // If we're a popup window or in app mode, we don't display the spy guy or
  // the tab strip.  Instead, put an hbox in titlebar_alignment_ in place of
  // the tab strip.
  // +- Alignment (titlebar_alignment_) -----------------------------------+
  // |+ HBox -------------------------------------------------------------+|
  // ||+- TabStripGtk -++- Image ----------------++- Label --------------+||
  // ||| hidden        ++    (app_mode_favicon_) ||    (app_mode_title_) |||
  // |||               ||  favicon               ||  page title          |||
  // ||+---------------++------------------------++----------------------+||
  // |+-------------------------------------------------------------------+|
  // +---------------------------------------------------------------------+
  container_hbox_ = gtk_hbox_new(FALSE, 0);

  container_ = gtk_event_box_new();
  gtk_widget_set_name(container_, "chrome-browser-titlebar");
  gtk_event_box_set_visible_window(GTK_EVENT_BOX(container_), FALSE);
  gtk_container_add(GTK_CONTAINER(container_), container_hbox_);

  g_signal_connect(container_, "scroll-event", G_CALLBACK(OnScrollThunk), this);

  g_signal_connect(window_, "window-state-event",
                   G_CALLBACK(OnWindowStateChangedThunk), this);

  // Allocate the two button boxes on the left and right parts of the bar. These
  // are always allocated, but only displayed in incognito mode or when using
  // multiple profiles.
  titlebar_left_buttons_vbox_ = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(container_hbox_), titlebar_left_buttons_vbox_,
                     FALSE, FALSE, 0);
  titlebar_left_avatar_frame_ = gtk_alignment_new(0.0, 0.0, 1.0, 1.0);
  gtk_widget_set_no_show_all(titlebar_left_avatar_frame_, TRUE);
  gtk_alignment_set_padding(GTK_ALIGNMENT(titlebar_left_avatar_frame_), 0,
      kAvatarBottomSpacing, kAvatarSideSpacing, kAvatarSideSpacing);
  gtk_box_pack_start(GTK_BOX(container_hbox_), titlebar_left_avatar_frame_,
                     FALSE, FALSE, 0);

  titlebar_left_label_frame_ = gtk_alignment_new(0.0, 0.5, 0.0, 0.0);
  gtk_widget_set_no_show_all(titlebar_left_label_frame_, TRUE);
  gtk_alignment_set_padding(GTK_ALIGNMENT(titlebar_left_label_frame_), 0,
      kAvatarBottomSpacing, kAvatarSideSpacing, kAvatarSideSpacing);
  gtk_box_pack_start(GTK_BOX(container_hbox_), titlebar_left_label_frame_,
                   FALSE, FALSE, 0);

  titlebar_right_avatar_frame_ = gtk_alignment_new(0.0, 0.0, 1.0, 1.0);
  gtk_widget_set_no_show_all(titlebar_right_avatar_frame_, TRUE);
  gtk_alignment_set_padding(GTK_ALIGNMENT(titlebar_right_avatar_frame_), 0,
      kAvatarBottomSpacing, kAvatarSideSpacing, kAvatarSideSpacing);
  gtk_box_pack_end(GTK_BOX(container_hbox_), titlebar_right_avatar_frame_,
                   FALSE, FALSE, 0);

  titlebar_right_label_frame_ = gtk_alignment_new(0.0, 0.5, 0.0, 0.0);
  gtk_widget_set_no_show_all(titlebar_right_label_frame_, TRUE);
  gtk_alignment_set_padding(GTK_ALIGNMENT(titlebar_right_label_frame_), 0,
      kAvatarBottomSpacing, kAvatarSideSpacing, kAvatarSideSpacing);
  gtk_box_pack_end(GTK_BOX(container_hbox_), titlebar_right_label_frame_,
                   FALSE, FALSE, 0);

  titlebar_right_buttons_vbox_ = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_end(GTK_BOX(container_hbox_), titlebar_right_buttons_vbox_,
                   FALSE, FALSE, 0);

  // Create the Avatar button and listen for notifications. It must always be
  // created because a new profile can be added at any time.
  avatar_button_.reset(new AvatarMenuButtonGtk(browser_window_->browser()));

  registrar_.Add(this, chrome::NOTIFICATION_PROFILE_CACHED_INFO_CHANGED,
                 content::NotificationService::AllSources());

#if defined(USE_GCONF)
  // Either read the gconf database and register for updates (on GNOME), or use
  // the default value (anywhere else).
  GConfTitlebarListener::GetInstance()->SetTitlebarButtons(this);
#else
  BuildButtons(kDefaultButtonString);
#endif

  UpdateAvatar();

  // We use an alignment to control the titlebar height.
  titlebar_alignment_ = gtk_alignment_new(0.0, 0.0, 1.0, 1.0);
  if (browser_window_->browser()->is_type_tabbed()) {
    gtk_box_pack_start(GTK_BOX(container_hbox_), titlebar_alignment_, TRUE,
                       TRUE, 0);

    // Put the tab strip in the titlebar.
    gtk_container_add(GTK_CONTAINER(titlebar_alignment_),
                      browser_window_->tabstrip()->widget());
  } else {
    // App mode specific widgets.
    gtk_box_pack_start(GTK_BOX(container_hbox_), titlebar_alignment_, TRUE,
                       TRUE, 0);
    GtkWidget* app_mode_hbox = gtk_hbox_new(FALSE, kIconTitleSpacing);
    gtk_container_add(GTK_CONTAINER(titlebar_alignment_), app_mode_hbox);

    // Put the tab strip in the hbox even though we don't show it.  Sometimes
    // we need the position of the tab strip so make sure it's in our widget
    // hierarchy.
    gtk_box_pack_start(GTK_BOX(app_mode_hbox),
        browser_window_->tabstrip()->widget(), FALSE, FALSE, 0);

    GtkWidget* favicon_event_box = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(favicon_event_box), FALSE);
    g_signal_connect(favicon_event_box, "button-press-event",
                     G_CALLBACK(OnFaviconMenuButtonPressedThunk), this);
    gtk_box_pack_start(GTK_BOX(app_mode_hbox), favicon_event_box, FALSE,
                       FALSE, 0);
    // We use the app logo as a placeholder image so the title doesn't jump
    // around.
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    app_mode_favicon_ = gtk_image_new_from_pixbuf(rb.GetNativeImageNamed(
        IDR_PRODUCT_LOGO_16, ui::ResourceBundle::RTL_ENABLED).ToGdkPixbuf());
    g_object_set_data(G_OBJECT(app_mode_favicon_), "left-align-popup",
                      reinterpret_cast<void*>(true));
    gtk_container_add(GTK_CONTAINER(favicon_event_box), app_mode_favicon_);

    app_mode_title_ = gtk_label_new(NULL);
    gtk_label_set_ellipsize(GTK_LABEL(app_mode_title_), PANGO_ELLIPSIZE_END);
    gtk_misc_set_alignment(GTK_MISC(app_mode_title_), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(app_mode_hbox), app_mode_title_, TRUE, TRUE,
                       0);

    UpdateTitleAndIcon();
  }

  theme_service_ = GtkThemeService::GetFrom(
      browser_window_->browser()->profile());
  registrar_.Add(this, chrome::NOTIFICATION_BROWSER_THEME_CHANGED,
                 content::Source<ThemeService>(theme_service_));
  theme_service_->InitThemesFor(this);

  gtk_widget_show_all(container_);

  ui::ActiveWindowWatcherX::AddObserver(this);
}

BrowserTitlebar::~BrowserTitlebar() {
  ui::ActiveWindowWatcherX::RemoveObserver(this);
#if defined(USE_GCONF)
  GConfTitlebarListener::GetInstance()->RemoveObserver(this);
#endif
}

void BrowserTitlebar::BuildButtons(const std::string& button_string) {
  // Clear out all previous data.
  close_button_.reset();
  restore_button_.reset();
  maximize_button_.reset();
  minimize_button_.reset();
  gtk_util::RemoveAllChildren(titlebar_left_buttons_vbox_);
  gtk_util::RemoveAllChildren(titlebar_right_buttons_vbox_);
  titlebar_left_buttons_hbox_ = NULL;
  titlebar_right_buttons_hbox_ = NULL;
  top_padding_left_ = NULL;
  top_padding_right_ = NULL;

  bool left_side = true;
  base::StringTokenizer tokenizer(button_string, ":,");
  tokenizer.set_options(base::StringTokenizer::RETURN_DELIMS);
  int left_count = 0;
  int right_count = 0;
  while (tokenizer.GetNext()) {
    if (tokenizer.token_is_delim()) {
      if (*tokenizer.token_begin() == ':')
        left_side = false;
    } else {
      base::StringPiece token = tokenizer.token_piece();
      if (BuildButton(token.as_string(), left_side))
        (left_side ? left_count : right_count)++;
    }
  }

  // If we are in incognito mode, add the spy guy to either the end of the left
  // or the beginning of the right depending on which side has fewer buttons.
  display_avatar_on_left_ = right_count > left_count;

  // Now show the correct widgets in the two hierarchies.
  if (using_custom_frame_) {
    gtk_widget_show_all(titlebar_left_buttons_vbox_);
    gtk_widget_show_all(titlebar_right_buttons_vbox_);
  }
  UpdateMaximizeRestoreVisibility();
}

bool BrowserTitlebar::BuildButton(const std::string& button_token,
                                  bool left_side) {
  if (button_token == "minimize") {
    minimize_button_.reset(CreateTitlebarButton("minimize", left_side));

    gtk_widget_size_request(minimize_button_->widget(),
                            &minimize_button_req_);
    return true;
  } else if (button_token == "maximize") {
    restore_button_.reset(CreateTitlebarButton("unmaximize", left_side));
    maximize_button_.reset(CreateTitlebarButton("maximize", left_side));

    gtk_util::SetButtonClickableByMouseButtons(maximize_button_->widget(),
                                               true, true, true);
    gtk_widget_size_request(restore_button_->widget(),
                            &restore_button_req_);
    return true;
  } else if (button_token == "close") {
    close_button_.reset(CreateTitlebarButton("close", left_side));
    close_button_->set_flipped(left_side);

    gtk_widget_size_request(close_button_->widget(), &close_button_req_);
    return true;
  }
  // Ignore any other values like "pin" since we don't have images for
  // those.
  return false;
}

GtkWidget* BrowserTitlebar::GetButtonHBox(bool left_side) {
  if (left_side && titlebar_left_buttons_hbox_)
    return titlebar_left_buttons_hbox_;
  else if (!left_side && titlebar_right_buttons_hbox_)
    return titlebar_right_buttons_hbox_;

  // We put the min/max/restore/close buttons in a vbox so they are top aligned
  // (up to padding) and don't vertically stretch.
  GtkWidget* vbox = left_side ? titlebar_left_buttons_vbox_ :
                    titlebar_right_buttons_vbox_;

  GtkWidget* top_padding = gtk_fixed_new();
  gtk_widget_set_size_request(top_padding, -1, kButtonOuterPadding);
  gtk_box_pack_start(GTK_BOX(vbox), top_padding, FALSE, FALSE, 0);

  GtkWidget* buttons_hbox = gtk_hbox_new(FALSE, kButtonSpacing);
  gtk_box_pack_start(GTK_BOX(vbox), buttons_hbox, FALSE, FALSE, 0);

  if (left_side) {
    titlebar_left_buttons_hbox_ = buttons_hbox;
    top_padding_left_ = top_padding;
  } else {
    titlebar_right_buttons_hbox_ = buttons_hbox;
    top_padding_right_ = top_padding;
  }

  return buttons_hbox;
}

CustomDrawButton* BrowserTitlebar::CreateTitlebarButton(
    const std::string& button_name, bool left_side) {
  int normal_image_id;
  int pressed_image_id;
  int hover_image_id;
  int tooltip_id;
  GetButtonResources(button_name, &normal_image_id, &pressed_image_id,
                     &hover_image_id, &tooltip_id);

  CustomDrawButton* button = new CustomDrawButton(normal_image_id,
                                                  pressed_image_id,
                                                  hover_image_id,
                                                  0);
  gtk_widget_add_events(GTK_WIDGET(button->widget()), GDK_POINTER_MOTION_MASK);
  g_signal_connect(button->widget(), "clicked",
                   G_CALLBACK(OnButtonClickedThunk), this);
  g_signal_connect(button->widget(), "motion-notify-event",
                   G_CALLBACK(OnMouseMoveEvent), browser_window_);

  std::string localized_tooltip = l10n_util::GetStringUTF8(tooltip_id);
  gtk_widget_set_tooltip_text(button->widget(),
                              localized_tooltip.c_str());

  GtkWidget* box = GetButtonHBox(left_side);
  gtk_box_pack_start(GTK_BOX(box), button->widget(), FALSE, FALSE, 0);
  return button;
}

void BrowserTitlebar::GetButtonResources(const std::string& button_name,
                                         int* normal_image_id,
                                         int* pressed_image_id,
                                         int* hover_image_id,
                                         int* tooltip_id) const {
  if (button_name == "close") {
    *normal_image_id = IDR_CLOSE;
    *pressed_image_id = IDR_CLOSE_P;
    *hover_image_id = IDR_CLOSE_H;
    *tooltip_id = IDS_XPFRAME_CLOSE_TOOLTIP;
  } else if (button_name == "minimize") {
    *normal_image_id = IDR_MINIMIZE;
    *pressed_image_id = IDR_MINIMIZE_P;
    *hover_image_id = IDR_MINIMIZE_H;
    *tooltip_id = IDS_XPFRAME_MINIMIZE_TOOLTIP;
  } else if (button_name == "maximize") {
    *normal_image_id = IDR_MAXIMIZE;
    *pressed_image_id = IDR_MAXIMIZE_P;
    *hover_image_id = IDR_MAXIMIZE_H;
    *tooltip_id = IDS_XPFRAME_MAXIMIZE_TOOLTIP;
  } else if (button_name == "unmaximize") {
    *normal_image_id = IDR_RESTORE;
    *pressed_image_id = IDR_RESTORE_P;
    *hover_image_id = IDR_RESTORE_H;
    *tooltip_id = IDS_XPFRAME_RESTORE_TOOLTIP;
  }
}

void BrowserTitlebar::UpdateButtonBackground(CustomDrawButton* button) {
  SkColor color = theme_service_->GetColor(
      ThemeProperties::COLOR_BUTTON_BACKGROUND);
  SkBitmap background = theme_service_->GetImageNamed(
      IDR_THEME_WINDOW_CONTROL_BACKGROUND).AsBitmap();

  // TODO(erg): For now, we just use a completely black mask and we can get
  // away with this in the short term because our buttons are rectangles. We
  // should get Glen to make properly hinted masks that match our min/max/close
  // buttons (which have some odd alpha blending around the
  // edges). http://crbug.com/103661
  SkBitmap mask;
  mask.setConfig(SkBitmap::kARGB_8888_Config,
                 button->SurfaceWidth(), button->SurfaceHeight(), 0);
  mask.allocPixels();
  mask.eraseColor(SK_ColorBLACK);

  button->SetBackground(color, background, mask);
}

void BrowserTitlebar::UpdateCustomFrame(bool use_custom_frame) {
  using_custom_frame_ = use_custom_frame;
  if (!use_custom_frame ||
      (browser_window_->IsMaximized() && unity::IsRunning())) {
    if (titlebar_left_buttons_vbox_)
      gtk_widget_hide(titlebar_left_buttons_vbox_);
    if (titlebar_right_buttons_vbox_)
      gtk_widget_hide(titlebar_right_buttons_vbox_);
  } else {
    if (titlebar_left_buttons_vbox_)
      gtk_widget_show_all(titlebar_left_buttons_vbox_);
    if (titlebar_right_buttons_vbox_)
      gtk_widget_show_all(titlebar_right_buttons_vbox_);
  }
  UpdateTitlebarAlignment();
  UpdateMaximizeRestoreVisibility();
}

void BrowserTitlebar::UpdateTitleAndIcon() {
  if (!app_mode_title_)
    return;

  // Get the page title and elide it to the available space.
  string16 title = browser_window_->browser()->GetWindowTitleForCurrentTab();
  gtk_label_set_text(GTK_LABEL(app_mode_title_), UTF16ToUTF8(title).c_str());

  if (browser_window_->browser()->is_app()) {
    switch (browser_window_->browser()->type()) {
      case Browser::TYPE_POPUP: {
        // Update the system app icon.  We don't need to update the icon in the
        // top left of the custom frame, that will get updated when the
        // throbber is updated.
        Profile* profile = browser_window_->browser()->profile();
        gfx::Image icon = browser_window_->browser()->GetCurrentPageIcon();
        if (icon.IsEmpty()) {
          gtk_util::SetWindowIcon(window_, profile);
        } else {
          gtk_util::SetWindowIcon(window_, profile, icon.ToGdkPixbuf());
        }
        break;
      }
      case Browser::TYPE_TABBED: {
        NOTREACHED() << "We should never have a tabbed app window.";
        break;
      }
    }
  }
}

void BrowserTitlebar::UpdateThrobber(WebContents* web_contents) {
  DCHECK(app_mode_favicon_);

  if (web_contents && web_contents->IsLoading()) {
    GdkPixbuf* icon_pixbuf =
        throbber_.GetNextFrame(web_contents->IsWaitingForResponse());
    gtk_image_set_from_pixbuf(GTK_IMAGE(app_mode_favicon_), icon_pixbuf);
  } else {
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();

    // Note: we want to exclude the application popup/panel window.
    if ((browser_window_->browser()->is_app() &&
        !browser_window_->browser()->is_type_tabbed())) {
      gfx::Image icon = browser_window_->browser()->GetCurrentPageIcon();
      if (icon.IsEmpty()) {
        // Fallback to the Chromium icon if the page has no icon.
        gtk_image_set_from_pixbuf(GTK_IMAGE(app_mode_favicon_),
            rb.GetNativeImageNamed(IDR_PRODUCT_LOGO_16).ToGdkPixbuf());
      } else {
        gtk_image_set_from_pixbuf(GTK_IMAGE(app_mode_favicon_),
                                  icon.ToGdkPixbuf());
      }
    } else {
      gtk_image_set_from_pixbuf(GTK_IMAGE(app_mode_favicon_),
          rb.GetNativeImageNamed(IDR_PRODUCT_LOGO_16).ToGdkPixbuf());
    }
    throbber_.Reset();
  }
}

void BrowserTitlebar::UpdateTitlebarAlignment() {
  if (browser_window_->browser()->is_type_tabbed()) {
    int top_padding = 0;
    int side_padding = 0;
    int vertical_offset = kNormalVerticalOffset;

    if (using_custom_frame_) {
      if (!browser_window_->IsMaximized()) {
        top_padding = kTitlebarHeight;
      } else if (using_custom_frame_ && browser_window_->IsMaximized()) {
        vertical_offset = 0;
        if (!unity::IsRunning())
          side_padding = kMaximizedTabstripPadding;
      }
    }

    int right_padding = 0;
    int left_padding = kTabStripLeftPadding;
    if (titlebar_right_buttons_hbox_)
      right_padding = side_padding;
    if (titlebar_left_buttons_hbox_)
      left_padding = side_padding;

    gtk_alignment_set_padding(GTK_ALIGNMENT(titlebar_alignment_),
                              top_padding, 0,
                              left_padding, right_padding);
    browser_window_->tabstrip()->SetVerticalOffset(vertical_offset);
  } else {
    if (using_custom_frame_ && !browser_window_->IsFullscreen()) {
      gtk_alignment_set_padding(GTK_ALIGNMENT(titlebar_alignment_),
          kAppModePaddingTop, kAppModePaddingBottom, kAppModePaddingLeft, 0);
      gtk_widget_show(titlebar_alignment_);
    } else {
      gtk_widget_hide(titlebar_alignment_);
    }
  }

  // Resize the buttons so that the clickable area extends all the way to the
  // edge of the browser window.
  GtkRequisition close_button_req = close_button_req_;
  GtkRequisition minimize_button_req = minimize_button_req_;
  GtkRequisition restore_button_req = restore_button_req_;
  if (using_custom_frame_ && browser_window_->IsMaximized()) {
    close_button_req.width += kButtonOuterPadding;
    close_button_req.height += kButtonOuterPadding;
    minimize_button_req.height += kButtonOuterPadding;
    restore_button_req.height += kButtonOuterPadding;
    if (top_padding_left_)
      gtk_widget_hide(top_padding_left_);
    if (top_padding_right_)
      gtk_widget_hide(top_padding_right_);
  } else {
    if (top_padding_left_)
      gtk_widget_show(top_padding_left_);
    if (top_padding_right_)
      gtk_widget_show(top_padding_right_);
  }
  if (close_button_.get()) {
    gtk_widget_set_size_request(close_button_->widget(),
                                close_button_req.width,
                                close_button_req.height);
  }
  if (minimize_button_.get()) {
    gtk_widget_set_size_request(minimize_button_->widget(),
                                minimize_button_req.width,
                                minimize_button_req.height);
  }
  if (maximize_button_.get()) {
    gtk_widget_set_size_request(restore_button_->widget(),
                                restore_button_req.width,
                                restore_button_req.height);
  }
}

void BrowserTitlebar::UpdateTextColor() {
  if (!app_mode_title_)
    return;

  if (theme_service_ && theme_service_->UsingNativeTheme()) {
    // We don't really have any good options here.
    //
    // Colors from window manager themes aren't exposed in GTK; the window
    // manager is a separate component and when there is information sharing
    // (in the case of metacity), it's one way where the window manager reads
    // data from the GTK theme (which allows us to do a decent job with
    // picking the frame color).
    //
    // We probably won't match in the majority of cases, but we can at the
    // very least make things legible. The default metacity and xfwm themes
    // on ubuntu have white text hardcoded. Determine whether black or white
    // has more luminosity contrast and then set that color as the text
    // color.
    GdkColor frame_color;
    if (window_has_focus_) {
      frame_color = theme_service_->GetGdkColor(
          ThemeProperties::COLOR_FRAME);
    } else {
      frame_color = theme_service_->GetGdkColor(
          ThemeProperties::COLOR_FRAME_INACTIVE);
    }
    GdkColor text_color = PickLuminosityContrastingColor(
        &frame_color, &ui::kGdkWhite, &ui::kGdkBlack);
    gtk_util::SetLabelColor(app_mode_title_, &text_color);
  } else {
    gtk_util::SetLabelColor(app_mode_title_, &ui::kGdkWhite);
  }
}

void BrowserTitlebar::UpdateAvatarLabel() {
  if (theme_service_ && avatar_label_) {
    GdkColor text_color =
        theme_service_->GetGdkColor(ThemeProperties::COLOR_MANAGED_USER_LABEL);
    GdkColor label_background = theme_service_->GetGdkColor(
        ThemeProperties::COLOR_MANAGED_USER_LABEL_BACKGROUND);
    gtk_util::SetLabelColor(avatar_label_, &text_color);
    gtk_widget_modify_bg(
        GTK_WIDGET(avatar_label_bg_), GTK_STATE_NORMAL, &label_background);
    char* markup = g_markup_printf_escaped(
        "<span size='small'>%s</span>",
        l10n_util::GetStringUTF8(IDS_MANAGED_USER_AVATAR_LABEL).c_str());
    gtk_label_set_markup(GTK_LABEL(avatar_label_), markup);
    g_free(markup);
  }
}

void BrowserTitlebar::UpdateAvatar() {
  // Remove previous state.
  gtk_util::RemoveAllChildren(titlebar_left_avatar_frame_);
  gtk_util::RemoveAllChildren(titlebar_right_avatar_frame_);
  gtk_util::RemoveAllChildren(titlebar_left_label_frame_);
  gtk_util::RemoveAllChildren(titlebar_right_label_frame_);

  if (!ShouldDisplayAvatar())
    return;

  if (!avatar_) {
    if (IsOffTheRecord()) {
      ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
      gfx::Image avatar_image =
          rb.GetNativeImageNamed(IDR_OTR_ICON, ui::ResourceBundle::RTL_ENABLED);
      avatar_ = gtk_image_new_from_pixbuf(avatar_image.ToGdkPixbuf());
      gtk_misc_set_alignment(GTK_MISC(avatar_), 0.0, 1.0);
      gtk_widget_set_size_request(avatar_, -1, 0);
    } else {
      // Use a clickable avatar.
      avatar_ = avatar_button_->widget();
    }
  }

  gtk_widget_show_all(avatar_);

  Profile* profile = browser_window_->browser()->profile();
  if (profile->IsManaged()) {
    avatar_label_ = gtk_label_new(NULL);
    gtk_misc_set_padding(GTK_MISC(avatar_label_), 10, 2);
    avatar_label_bg_ = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(avatar_label_bg_), avatar_label_);
    g_signal_connect(avatar_label_bg_, "button-press-event",
                     G_CALLBACK(OnAvatarLabelButtonPressedThunk), this);
    UpdateAvatarLabel();
    gtk_widget_show(avatar_label_bg_);
    gtk_widget_show(avatar_label_);
    if (display_avatar_on_left_) {
      gtk_container_add(GTK_CONTAINER(titlebar_left_label_frame_),
                        avatar_label_bg_);
      gtk_widget_show(titlebar_left_label_frame_);
      gtk_widget_hide(titlebar_right_label_frame_);
    } else {
      gtk_container_add(GTK_CONTAINER(titlebar_right_label_frame_),
                        avatar_label_bg_);
      gtk_widget_show(titlebar_right_label_frame_);
      gtk_widget_hide(titlebar_left_label_frame_);
    }
  }

  if (display_avatar_on_left_) {
    gtk_container_add(GTK_CONTAINER(titlebar_left_avatar_frame_), avatar_);
    gtk_widget_show(titlebar_left_avatar_frame_);
    gtk_widget_hide(titlebar_right_avatar_frame_);
  } else {
    gtk_container_add(GTK_CONTAINER(titlebar_right_avatar_frame_), avatar_);
    gtk_widget_show(titlebar_right_avatar_frame_);
    gtk_widget_hide(titlebar_left_avatar_frame_);
  }

  if (IsOffTheRecord())
    return;

  bool is_gaia_picture = false;
  gfx::Image avatar;
  ProfileInfoCache& cache =
      g_browser_process->profile_manager()->GetProfileInfoCache();
  size_t index = cache.GetIndexOfProfileWithPath(profile->GetPath());
  if (index == std::string::npos)
    return;

  is_gaia_picture =
      cache.IsUsingGAIAPictureOfProfileAtIndex(index) &&
      cache.GetGAIAPictureOfProfileAtIndex(index);
  avatar = cache.GetAvatarIconOfProfileAtIndex(index);
  avatar_button_->SetIcon(avatar, is_gaia_picture);
  avatar_button_->set_menu_frame_style(display_avatar_on_left_ ?
      BubbleGtk::ANCHOR_TOP_LEFT : BubbleGtk::ANCHOR_TOP_RIGHT);
}

void BrowserTitlebar::MaximizeButtonClicked() {
  GdkEvent* event = gtk_get_current_event();
  if (event->button.button == 1) {
    gtk_window_maximize(window_);
  } else {
    GtkWidget* widget = GTK_WIDGET(window_);
    GdkScreen* screen = gtk_widget_get_screen(widget);
    gint monitor = gdk_screen_get_monitor_at_window(
        screen, gtk_widget_get_window(widget));
    GdkRectangle screen_rect;
    gdk_screen_get_monitor_geometry(screen, monitor, &screen_rect);

    gint x, y;
    gtk_window_get_position(window_, &x, &y);

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    gint width = allocation.width;
    gint height = allocation.height;

    if (event->button.button == 3) {
      x = 0;
      width = screen_rect.width;
    } else if (event->button.button == 2) {
      y = 0;
      height = screen_rect.height;
    }

    browser_window_->SetBounds(gfx::Rect(x, y, width, height));
  }
  gdk_event_free(event);
}

void BrowserTitlebar::UpdateMaximizeRestoreVisibility() {
  if (maximize_button_.get()) {
    if (browser_window_->IsMaximized()) {
      gtk_widget_hide(maximize_button_->widget());
      gtk_widget_show(restore_button_->widget());
    } else {
      gtk_widget_hide(restore_button_->widget());
      gtk_widget_show(maximize_button_->widget());
    }
  }
}

gboolean BrowserTitlebar::OnWindowStateChanged(GtkWindow* window,
                                               GdkEventWindowState* event) {
  UpdateMaximizeRestoreVisibility();
  UpdateTitlebarAlignment();
  UpdateTextColor();
  return FALSE;
}

gboolean BrowserTitlebar::OnScroll(GtkWidget* widget, GdkEventScroll* event) {
  Browser* browser = browser_window_->browser();
  int index = browser->tab_strip_model()->active_index();
  if (event->direction == GDK_SCROLL_LEFT ||
      event->direction == GDK_SCROLL_UP) {
    if (index != 0)
      chrome::SelectPreviousTab(browser);
  } else if (index + 1 < browser->tab_strip_model()->count()) {
    chrome::SelectNextTab(browser);
  }
  return TRUE;
}

void BrowserTitlebar::OnButtonClicked(GtkWidget* button) {
  if (close_button_.get() && close_button_->widget() == button) {
    browser_window_->Close();
  } else if (restore_button_.get() && restore_button_->widget() == button) {
    browser_window_->UnMaximize();
  } else if (maximize_button_.get() && maximize_button_->widget() == button) {
    MaximizeButtonClicked();
  } else if (minimize_button_.get() && minimize_button_->widget() == button) {
    gtk_window_iconify(window_);
  }
}

gboolean BrowserTitlebar::OnFaviconMenuButtonPressed(GtkWidget* widget,
                                                     GdkEventButton* event) {
  if (event->button != 1)
    return FALSE;

  if (!favicon_menu_model_.get()) {
    favicon_menu_model_.reset(
        new PopupPageMenuModel(this, browser_window_->browser()));

    favicon_menu_.reset(new MenuGtk(NULL, favicon_menu_model_.get()));
  }

  favicon_menu_->PopupForWidget(app_mode_favicon_, event->button, event->time);

  return TRUE;
}

gboolean BrowserTitlebar::OnAvatarLabelButtonPressed(GtkWidget* widget,
                                                     GdkEventButton* event) {
  if (event->button != 1)
    return FALSE;

  // Show the avatar menu bubble with the upward arrow at the x position where
  // the user has clicked.
  gfx::Rect rect = gtk_util::WidgetBounds(widget);
  rect.set_x(event->x);
  rect.set_width(0);
  new AvatarMenuBubbleGtk(
      browser_window_->browser(), widget, BubbleGtk::ANCHOR_TOP_RIGHT, &rect);
  return TRUE;
}

void BrowserTitlebar::ShowContextMenu(GdkEventButton* event) {
  if (!context_menu_.get()) {
    context_menu_model_.reset(new ContextMenuModel(this));
    context_menu_.reset(new MenuGtk(NULL, context_menu_model_.get()));
  }

  context_menu_->PopupAsContext(gfx::Point(event->x_root, event->y_root),
                                event->time);
}

bool BrowserTitlebar::IsCommandIdEnabled(int command_id) const {
  if (command_id == kShowWindowDecorationsCommand)
    return true;

  return chrome::IsCommandEnabled(browser_window_->browser(), command_id);
}

bool BrowserTitlebar::IsCommandIdChecked(int command_id) const {
  if (command_id == kShowWindowDecorationsCommand) {
    PrefService* prefs = browser_window_->browser()->profile()->GetPrefs();
    return !prefs->GetBoolean(prefs::kUseCustomChromeFrame);
  }

  EncodingMenuController controller;
  if (controller.DoesCommandBelongToEncodingMenu(command_id)) {
    WebContents* web_contents =
        browser_window_->browser()->tab_strip_model()->GetActiveWebContents();
    if (web_contents) {
      return controller.IsItemChecked(browser_window_->browser()->profile(),
                                      web_contents->GetEncoding(),
                                      command_id);
    }
    return false;
  }

  NOTREACHED();
  return false;
}

void BrowserTitlebar::ExecuteCommand(int command_id, int event_flags) {
  if (command_id == kShowWindowDecorationsCommand) {
    PrefService* prefs = browser_window_->browser()->profile()->GetPrefs();
    prefs->SetBoolean(prefs::kUseCustomChromeFrame,
                  !prefs->GetBoolean(prefs::kUseCustomChromeFrame));
    return;
  }

  chrome::ExecuteCommand(browser_window_->browser(), command_id);
}

bool BrowserTitlebar::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* out_accelerator) {
  const ui::Accelerator* accelerator =
      AcceleratorsGtk::GetInstance()->GetPrimaryAcceleratorForCommand(
          command_id);
  if (!accelerator)
    return false;
  *out_accelerator = *accelerator;
  return true;
}

void BrowserTitlebar::Observe(int type,
                              const content::NotificationSource& source,
                              const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_BROWSER_THEME_CHANGED: {
      UpdateTextColor();
      UpdateAvatarLabel();

      if (minimize_button_.get())
        UpdateButtonBackground(minimize_button_.get());
      if (maximize_button_.get())
        UpdateButtonBackground(maximize_button_.get());
      if (restore_button_.get())
        UpdateButtonBackground(restore_button_.get());
      if (close_button_.get())
        UpdateButtonBackground(close_button_.get());
      break;
    }

    case chrome::NOTIFICATION_PROFILE_CACHED_INFO_CHANGED:
      if (!IsOffTheRecord())
        UpdateAvatar();
      break;

    default:
      NOTREACHED();
  }
}

void BrowserTitlebar::ActiveWindowChanged(GdkWindow* active_window) {
  // Can be called during shutdown; BrowserWindowGtk will set our |window_|
  // to NULL during that time.
  if (!window_)
    return;

  window_has_focus_ =
      gtk_widget_get_window(GTK_WIDGET(window_)) == active_window;
  UpdateTextColor();
}

bool BrowserTitlebar::ShouldDisplayAvatar() {
  if (IsOffTheRecord())
    return true;

  if (!browser_window_->browser()->is_type_tabbed())
    return false;

  return AvatarMenuModel::ShouldShowAvatarMenu();
}

bool BrowserTitlebar::IsOffTheRecord() {
  return browser_window_->browser()->profile()->IsOffTheRecord();
}

BrowserTitlebar::ContextMenuModel::ContextMenuModel(
    ui::SimpleMenuModel::Delegate* delegate)
    : SimpleMenuModel(delegate) {
  AddItemWithStringId(IDC_NEW_TAB, IDS_TAB_CXMENU_NEWTAB);
  AddItemWithStringId(IDC_RESTORE_TAB, IDS_RESTORE_TAB);
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddItemWithStringId(IDC_TASK_MANAGER, IDS_TASK_MANAGER);
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddCheckItemWithStringId(kShowWindowDecorationsCommand,
                           IDS_SHOW_WINDOW_DECORATIONS_MENU);
}
