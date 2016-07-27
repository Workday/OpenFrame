// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/toolbar/toolbar_view.h"

#include <algorithm>

#include "base/command_line.h"
#include "base/i18n/number_formatting.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/trace_event/trace_event.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/command_updater.h"
#include "chrome/browser/extensions/extension_commands_global_registry.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_content_setting_bubble_model_delegate.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/global_error/global_error_service.h"
#include "chrome/browser/ui/global_error/global_error_service_factory.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/autofill/save_card_icon_view.h"
#include "chrome/browser/ui/views/extensions/extension_popup.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/layout_constants.h"
#include "chrome/browser/ui/views/location_bar/page_action_image_view.h"
#include "chrome/browser/ui/views/location_bar/page_action_with_badge_view.h"
#include "chrome/browser/ui/views/location_bar/star_view.h"
#include "chrome/browser/ui/views/outdated_upgrade_bubble_view.h"
#include "chrome/browser/ui/views/toolbar/app_menu_button.h"
#include "chrome/browser/ui/views/toolbar/back_button.h"
#include "chrome/browser/ui/views/toolbar/browser_actions_container.h"
#include "chrome/browser/ui/views/toolbar/home_button.h"
#include "chrome/browser/ui/views/toolbar/reload_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "chrome/browser/ui/views/translate/translate_icon_view.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "components/omnibox/browser/omnibox_view.h"
#include "content/public/browser/browser_accessibility_state.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "grit/theme_resources.h"
#include "ui/accessibility/ax_view_state.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/material_design/material_design_controller.h"
#include "ui/base/theme_provider.h"
#include "ui/base/window_open_disposition.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/vector_icons_public.h"
#include "ui/keyboard/keyboard_controller.h"
#include "ui/native_theme/native_theme_aura.h"
#include "ui/views/focus/view_storage.h"
#include "ui/views/view_targeter.h"
#include "ui/views/widget/tooltip_manager.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

#if defined(OS_WIN)
#include "chrome/browser/recovery/recovery_install_global_error_factory.h"
#include "chrome/browser/ui/views/conflicting_module_view_win.h"
#include "chrome/browser/ui/views/critical_notification_bubble_view.h"
#endif

#if !defined(OS_CHROMEOS)
#include "chrome/browser/signin/signin_global_error_factory.h"
#include "chrome/browser/sync/sync_global_error_factory.h"
#endif

#if defined(USE_ASH)
#include "ash/shell.h"
#endif

using base::UserMetricsAction;
using content::WebContents;

namespace {

#if !defined(OS_CHROMEOS)
bool HasAshShell() {
#if defined(USE_ASH)
  return ash::Shell::HasInstance();
#else
  return false;
#endif  // USE_ASH
}
#endif  // OS_CHROMEOS

// Returns the y-position that will center an element of height
// |child_height| inside an element of height |parent_height|. For
// material design excess padding is placed below, for non-material
// it is placed above.
int CenteredChildY(int parent_height, int child_height) {
  int roundoff_amount = ui::MaterialDesignController::IsModeMaterial() ? 0 : 1;
  return (parent_height - child_height + roundoff_amount) / 2;
}

}  // namespace

// static
const char ToolbarView::kViewClassName[] = "ToolbarView";

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, public:

ToolbarView::ToolbarView(Browser* browser)
    : back_(NULL),
      forward_(NULL),
      reload_(NULL),
      home_(NULL),
      location_bar_(NULL),
      browser_actions_(NULL),
      app_menu_button_(NULL),
      browser_(browser),
      badge_controller_(browser->profile(), this) {
  set_id(VIEW_ID_TOOLBAR);

  SetEventTargeter(
      scoped_ptr<views::ViewTargeter>(new views::ViewTargeter(this)));

  chrome::AddCommandObserver(browser_, IDC_BACK, this);
  chrome::AddCommandObserver(browser_, IDC_FORWARD, this);
  chrome::AddCommandObserver(browser_, IDC_RELOAD, this);
  chrome::AddCommandObserver(browser_, IDC_HOME, this);
  chrome::AddCommandObserver(browser_, IDC_LOAD_NEW_TAB_PAGE, this);

  display_mode_ = DISPLAYMODE_LOCATION;
  if (browser->SupportsWindowFeature(Browser::FEATURE_TABSTRIP))
    display_mode_ = DISPLAYMODE_NORMAL;

  if (OutdatedUpgradeBubbleView::IsAvailable()) {
    registrar_.Add(this, chrome::NOTIFICATION_OUTDATED_INSTALL,
                   content::NotificationService::AllSources());
    registrar_.Add(this, chrome::NOTIFICATION_OUTDATED_INSTALL_NO_AU,
                   content::NotificationService::AllSources());
  }
#if defined(OS_WIN)
  registrar_.Add(this, chrome::NOTIFICATION_CRITICAL_UPGRADE_INSTALLED,
                 content::NotificationService::AllSources());
#endif
}

ToolbarView::~ToolbarView() {
  // NOTE: Don't remove the command observers here.  This object gets destroyed
  // after the Browser (which owns the CommandUpdater), so the CommandUpdater is
  // already gone.
}

void ToolbarView::Init() {
  GetWidget()->AddObserver(this);

  back_ = new BackButton(this, new BackForwardMenuModel(
      browser_, BackForwardMenuModel::BACKWARD_MENU));
  back_->set_triggerable_event_flags(
      ui::EF_LEFT_MOUSE_BUTTON | ui::EF_MIDDLE_MOUSE_BUTTON);
  back_->set_tag(IDC_BACK);
  back_->SetTooltipText(l10n_util::GetStringUTF16(IDS_TOOLTIP_BACK));
  back_->SetAccessibleName(l10n_util::GetStringUTF16(IDS_ACCNAME_BACK));
  back_->set_id(VIEW_ID_BACK_BUTTON);
  back_->Init();

  forward_ = new ToolbarButton(this, new BackForwardMenuModel(
      browser_, BackForwardMenuModel::FORWARD_MENU));
  forward_->set_triggerable_event_flags(
      ui::EF_LEFT_MOUSE_BUTTON | ui::EF_MIDDLE_MOUSE_BUTTON);
  forward_->set_tag(IDC_FORWARD);
  forward_->SetTooltipText(l10n_util::GetStringUTF16(IDS_TOOLTIP_FORWARD));
  forward_->SetAccessibleName(l10n_util::GetStringUTF16(IDS_ACCNAME_FORWARD));
  forward_->set_id(VIEW_ID_FORWARD_BUTTON);
  forward_->Init();

  location_bar_ = new LocationBarView(
      browser_, browser_->profile(),
      browser_->command_controller()->command_updater(), this,
      display_mode_ == DISPLAYMODE_LOCATION);

  reload_ = new ReloadButton(browser_->command_controller()->command_updater());
  reload_->set_triggerable_event_flags(
      ui::EF_LEFT_MOUSE_BUTTON | ui::EF_MIDDLE_MOUSE_BUTTON);
  reload_->set_tag(IDC_RELOAD);
  reload_->SetAccessibleName(l10n_util::GetStringUTF16(IDS_ACCNAME_RELOAD));
  reload_->set_id(VIEW_ID_RELOAD_BUTTON);
  reload_->Init();

  home_ = new HomeButton(this, browser_);
  home_->set_triggerable_event_flags(
      ui::EF_LEFT_MOUSE_BUTTON | ui::EF_MIDDLE_MOUSE_BUTTON);
  home_->set_tag(IDC_HOME);
  home_->SetTooltipText(l10n_util::GetStringUTF16(IDS_TOOLTIP_HOME));
  home_->SetAccessibleName(l10n_util::GetStringUTF16(IDS_ACCNAME_HOME));
  home_->set_id(VIEW_ID_HOME_BUTTON);
  home_->Init();

  browser_actions_ = new BrowserActionsContainer(
      browser_,
      NULL);  // No master container for this one (it is master).

  app_menu_button_ = new AppMenuButton(this);
  app_menu_button_->EnableCanvasFlippingForRTLUI(true);
  app_menu_button_->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_ACCNAME_APP));
  app_menu_button_->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_APPMENU_TOOLTIP));
  app_menu_button_->set_id(VIEW_ID_APP_MENU);

  // Always add children in order from left to right, for accessibility.
  AddChildView(back_);
  AddChildView(forward_);
  AddChildView(reload_);
  AddChildView(home_);
  AddChildView(location_bar_);
  AddChildView(browser_actions_);
  AddChildView(app_menu_button_);

  LoadImages();

  // Start global error services now so we badge the menu correctly.
#if !defined(OS_CHROMEOS)
  if (!HasAshShell()) {
    SigninGlobalErrorFactory::GetForProfile(browser_->profile());
#if !defined(OS_ANDROID)
    SyncGlobalErrorFactory::GetForProfile(browser_->profile());
#endif
  }

#if defined(OS_WIN)
  RecoveryInstallGlobalErrorFactory::GetForProfile(browser_->profile());
#endif
#endif  // OS_CHROMEOS

  // Add any necessary badges to the menu item based on the system state.
  // Do this after |app_menu_button_| has been added as a bubble may be shown
  // that needs the widget (widget found by way of app_menu_button_->
  // GetWidget()).
  badge_controller_.UpdateDelegate();

  location_bar_->Init();

  show_home_button_.Init(prefs::kShowHomeButton,
                         browser_->profile()->GetPrefs(),
                         base::Bind(&ToolbarView::OnShowHomeButtonChanged,
                                    base::Unretained(this)));

  browser_actions_->Init();

  // Accessibility specific tooltip text.
  if (content::BrowserAccessibilityState::GetInstance()->
          IsAccessibleBrowser()) {
    back_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_ACCNAME_TOOLTIP_BACK));
    forward_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_ACCNAME_TOOLTIP_FORWARD));
  }
}

void ToolbarView::OnWidgetActivationChanged(views::Widget* widget,
                                            bool active) {
  extensions::ExtensionCommandsGlobalRegistry* registry =
      extensions::ExtensionCommandsGlobalRegistry::Get(browser_->profile());
  if (active) {
    registry->set_registry_for_active_window(
        browser_actions_->extension_keybinding_registry());
  } else if (registry->registry_for_active_window() ==
             browser_actions_->extension_keybinding_registry()) {
    registry->set_registry_for_active_window(nullptr);
  }
}

void ToolbarView::Update(WebContents* tab) {
  if (location_bar_)
    location_bar_->Update(tab);
  if (browser_actions_)
    browser_actions_->RefreshToolbarActionViews();
  if (reload_)
    reload_->set_menu_enabled(chrome::IsDebuggerAttachedToCurrentTab(browser_));
}

void ToolbarView::ResetTabState(WebContents* tab) {
  if (location_bar_)
    location_bar_->ResetTabState(tab);
}

void ToolbarView::SetPaneFocusAndFocusAppMenu() {
  SetPaneFocus(app_menu_button_);
}

bool ToolbarView::IsAppMenuFocused() {
  return app_menu_button_->HasFocus();
}

views::View* ToolbarView::GetBookmarkBubbleAnchor() {
  views::View* star_view = location_bar()->star_view();
  return (star_view && star_view->visible()) ? star_view : app_menu_button_;
}

views::View* ToolbarView::GetSaveCreditCardBubbleAnchor() {
  views::View* save_credit_card_icon_view =
      location_bar()->save_credit_card_icon_view();
  return (save_credit_card_icon_view && save_credit_card_icon_view->visible())
             ? save_credit_card_icon_view
             : app_menu_button_;
}

views::View* ToolbarView::GetTranslateBubbleAnchor() {
  views::View* translate_icon_view = location_bar()->translate_icon_view();
  return (translate_icon_view && translate_icon_view->visible())
             ? translate_icon_view
             : app_menu_button_;
}

void ToolbarView::ExecuteExtensionCommand(
    const extensions::Extension* extension,
    const extensions::Command& command) {
  browser_actions_->ExecuteExtensionCommand(extension, command);
}

int ToolbarView::GetMaxBrowserActionsWidth() const {
  // The browser actions container is allowed to grow, but only up until the
  // omnibox reaches its minimum size. So its maximum allowed width is its
  // current size, plus any that the omnibox could give up.
  return browser_actions_->width() +
         (location_bar_->width() - location_bar_->GetMinimumSize().width());
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, AccessiblePaneView overrides:

bool ToolbarView::SetPaneFocus(views::View* initial_focus) {
  if (!AccessiblePaneView::SetPaneFocus(initial_focus))
    return false;

  location_bar_->SetShowFocusRect(true);
  return true;
}

void ToolbarView::GetAccessibleState(ui::AXViewState* state) {
  state->role = ui::AX_ROLE_TOOLBAR;
  state->name = l10n_util::GetStringUTF16(IDS_ACCNAME_TOOLBAR);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, Menu::Delegate overrides:

bool ToolbarView::GetAcceleratorInfo(int id, ui::Accelerator* accel) {
  return GetWidget()->GetAccelerator(id, accel);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, views::MenuButtonListener implementation:

void ToolbarView::OnMenuButtonClicked(views::View* source,
                                      const gfx::Point& point) {
  TRACE_EVENT0("views", "ToolbarView::OnMenuButtonClicked");
  DCHECK_EQ(VIEW_ID_APP_MENU, source->id());
  app_menu_button_->ShowMenu(false);  // Not for drop.
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, LocationBarView::Delegate implementation:

WebContents* ToolbarView::GetWebContents() {
  return browser_->tab_strip_model()->GetActiveWebContents();
}

ToolbarModel* ToolbarView::GetToolbarModel() {
  return browser_->toolbar_model();
}

const ToolbarModel* ToolbarView::GetToolbarModel() const {
  return browser_->toolbar_model();
}

ContentSettingBubbleModelDelegate*
ToolbarView::GetContentSettingBubbleModelDelegate() {
  return browser_->content_setting_bubble_model_delegate();
}

void ToolbarView::ShowWebsiteSettings(
    content::WebContents* web_contents,
    const GURL& url,
    const SecurityStateModel::SecurityInfo& security_info) {
  chrome::ShowWebsiteSettings(browser_, web_contents, url, security_info);
}

views::Widget* ToolbarView::CreateViewsBubble(
    views::BubbleDelegateView* bubble_delegate) {
  return views::BubbleDelegateView::CreateBubble(bubble_delegate);
}

PageActionImageView* ToolbarView::CreatePageActionImageView(
    LocationBarView* owner, ExtensionAction* action) {
  return new PageActionImageView(owner, action, browser_);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, CommandObserver implementation:

void ToolbarView::EnabledStateChangedForCommand(int id, bool enabled) {
  views::Button* button = NULL;
  switch (id) {
    case IDC_BACK:
      button = back_;
      break;
    case IDC_FORWARD:
      button = forward_;
      break;
    case IDC_RELOAD:
      button = reload_;
      break;
    case IDC_HOME:
      button = home_;
      break;
  }
  if (button)
    button->SetEnabled(enabled);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, views::Button::ButtonListener implementation:

void ToolbarView::ButtonPressed(views::Button* sender,
                                const ui::Event& event) {
  chrome::ExecuteCommandWithDisposition(
      browser_, sender->tag(), ui::DispositionFromEventFlags(event.flags()));
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, content::NotificationObserver implementation:

void ToolbarView::Observe(int type,
                          const content::NotificationSource& source,
                          const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_OUTDATED_INSTALL:
      ShowOutdatedInstallNotification(true);
      break;
    case chrome::NOTIFICATION_OUTDATED_INSTALL_NO_AU:
      ShowOutdatedInstallNotification(false);
      break;
#if defined(OS_WIN)
    case chrome::NOTIFICATION_CRITICAL_UPGRADE_INSTALLED:
      ShowCriticalNotification();
      break;
#endif
    default:
      NOTREACHED();
  }
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, ui::AcceleratorProvider implementation:

bool ToolbarView::GetAcceleratorForCommandId(int command_id,
    ui::Accelerator* accelerator) {
  return GetWidget()->GetAccelerator(command_id, accelerator);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, views::View overrides:

gfx::Size ToolbarView::GetPreferredSize() const {
  gfx::Size size(location_bar_->GetPreferredSize());
  if (is_display_mode_normal()) {
    const int element_padding = GetLayoutConstant(TOOLBAR_ELEMENT_PADDING);
    const int browser_actions_width =
        browser_actions_->GetPreferredSize().width();
    const int content_width =
        GetLayoutInsets(TOOLBAR).width() +
        back_->GetPreferredSize().width() + element_padding +
        forward_->GetPreferredSize().width() + element_padding +
        reload_->GetPreferredSize().width() +
        (show_home_button_.GetValue()
             ? element_padding + home_->GetPreferredSize().width()
             : 0) +
        GetLayoutConstant(TOOLBAR_STANDARD_SPACING) +
        GetLayoutConstant(TOOLBAR_LOCATION_BAR_RIGHT_PADDING) +
        browser_actions_width +
        (browser_actions_width > 0 ? element_padding : 0) +
        app_menu_button_->GetPreferredSize().width();
    size.Enlarge(content_width, 0);
  }
  return SizeForContentSize(size);
}

gfx::Size ToolbarView::GetMinimumSize() const {
  gfx::Size size(location_bar_->GetMinimumSize());
  if (is_display_mode_normal()) {
    const int element_padding = GetLayoutConstant(TOOLBAR_ELEMENT_PADDING);
    const int browser_actions_width =
        browser_actions_->GetMinimumSize().width();
    const int content_width =
        GetLayoutInsets(TOOLBAR).width() +
        back_->GetMinimumSize().width() + element_padding +
        forward_->GetMinimumSize().width() + element_padding +
        reload_->GetMinimumSize().width() +
        (show_home_button_.GetValue()
             ? element_padding + home_->GetMinimumSize().width()
             : 0) +
        GetLayoutConstant(TOOLBAR_STANDARD_SPACING) +
        GetLayoutConstant(TOOLBAR_LOCATION_BAR_RIGHT_PADDING) +
        browser_actions_width +
        (browser_actions_width > 0 ? element_padding : 0) +
        app_menu_button_->GetMinimumSize().width();
    size.Enlarge(content_width, 0);
  }
  return SizeForContentSize(size);
}

void ToolbarView::Layout() {
  // If we have not been initialized yet just do nothing.
  if (back_ == NULL)
    return;

  if (!is_display_mode_normal()) {
    location_bar_->SetBounds(0, PopupTopSpacing(), width(),
                             location_bar_->GetPreferredSize().height());
    return;
  }

  // We assume all child elements except the location bar are the same height.
  // Set child_y such that buttons appear vertically centered.
  int child_height =
      std::min(back_->GetPreferredSize().height(), height());
  int child_y = CenteredChildY(height(), child_height);

  // If the window is maximized, we extend the back button to the left so that
  // clicking on the left-most pixel will activate the back button.
  // TODO(abarth):  If the window becomes maximized but is not resized,
  //                then Layout() might not be called and the back button
  //                will be slightly the wrong size.  We should force a
  //                Layout() in this case.
  //                http://crbug.com/5540
  bool maximized = browser_->window() && browser_->window()->IsMaximized();
  int back_width = back_->GetPreferredSize().width();
  const gfx::Insets insets(GetLayoutInsets(TOOLBAR));
  if (maximized) {
    back_->SetBounds(0, child_y, back_width + insets.left(), child_height);
    back_->SetLeadingMargin(insets.left());
  } else {
    back_->SetBounds(insets.left(), child_y, back_width, child_height);
    back_->SetLeadingMargin(0);
  }
  const int element_padding = GetLayoutConstant(TOOLBAR_ELEMENT_PADDING);
  int next_element_x = back_->bounds().right() + element_padding;

  forward_->SetBounds(next_element_x, child_y,
                      forward_->GetPreferredSize().width(), child_height);
  next_element_x = forward_->bounds().right() + element_padding;

  reload_->SetBounds(next_element_x, child_y,
                     reload_->GetPreferredSize().width(), child_height);
  next_element_x = reload_->bounds().right();

  if (show_home_button_.GetValue() ||
      (browser_->is_app() && extensions::util::IsNewBookmarkAppsEnabled())) {
    next_element_x += element_padding;
    home_->SetVisible(true);
    home_->SetBounds(next_element_x, child_y,
                     home_->GetPreferredSize().width(), child_height);
  } else {
    home_->SetVisible(false);
    home_->SetBounds(next_element_x, child_y, 0, child_height);
  }
  next_element_x =
      home_->bounds().right() + GetLayoutConstant(TOOLBAR_STANDARD_SPACING);

  int browser_actions_desired_width =
      browser_actions_->GetPreferredSize().width();
  int app_menu_width = app_menu_button_->GetPreferredSize().width();
  const int location_bar_right_padding =
      GetLayoutConstant(TOOLBAR_LOCATION_BAR_RIGHT_PADDING);

  int available_width = std::max(
      0, width() - insets.right() - app_menu_width -
             (browser_actions_desired_width > 0 ? element_padding : 0) -
             location_bar_right_padding - next_element_x);
  // Don't allow the omnibox to shrink to the point of non-existence, so
  // subtract its minimum width from the available width to reserve it.
  int minimum_location_bar_width = location_bar_->GetMinimumSize().width();
  int browser_actions_width = browser_actions_->GetWidthForMaxWidth(
      available_width - minimum_location_bar_width);
  available_width -= browser_actions_width;
  int location_bar_width = available_width;

  int location_height = location_bar_->GetPreferredSize().height();
  int location_y = CenteredChildY(height(), location_height);

  location_bar_->SetBounds(next_element_x, location_y,
                           location_bar_width, location_height);
  next_element_x = location_bar_->bounds().right() + location_bar_right_padding;

  browser_actions_->SetBounds(
      next_element_x, child_y, browser_actions_width, child_height);
  next_element_x = browser_actions_->bounds().right();
  if (browser_actions_width > 0)
    next_element_x += element_padding;

  // The browser actions need to do a layout explicitly, because when an
  // extension is loaded/unloaded/changed, BrowserActionContainer removes and
  // re-adds everything, regardless of whether it has a page action. For a
  // page action, browser action bounds do not change, as a result of which
  // SetBounds does not do a layout at all.
  // TODO(sidchat): Rework the above behavior so that explicit layout is not
  //                required.
  browser_actions_->Layout();

  // Extend the app menu to the screen's right edge in maximized mode just like
  // we extend the back button to the left edge.
  if (maximized)
    app_menu_width += insets.right();
  app_menu_button_->SetBounds(next_element_x, child_y, app_menu_width,
                              child_height);
  app_menu_button_->SetTrailingMargin(maximized ? insets.right() : 0);
}

void ToolbarView::OnThemeChanged() {
  LoadImages();
}

const char* ToolbarView::GetClassName() const {
  return kViewClassName;
}

bool ToolbarView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  const views::View* focused_view = focus_manager()->GetFocusedView();
  if (focused_view && (focused_view->id() == VIEW_ID_OMNIBOX))
    return false;  // Let the omnibox handle all accelerator events.
  return AccessiblePaneView::AcceleratorPressed(accelerator);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, protected:

// Override this so that when the user presses F6 to rotate toolbar panes,
// the location bar gets focus, not the first control in the toolbar - and
// also so that it selects all content in the location bar.
bool ToolbarView::SetPaneFocusAndFocusDefault() {
  if (!location_bar_->HasFocus()) {
    SetPaneFocus(location_bar_);
    location_bar_->FocusLocation(true);
    return true;
  }

  if (!AccessiblePaneView::SetPaneFocusAndFocusDefault())
    return false;
  browser_->window()->RotatePaneFocus(true);
  return true;
}

void ToolbarView::RemovePaneFocus() {
  AccessiblePaneView::RemovePaneFocus();
  location_bar_->SetShowFocusRect(false);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, private:

// views::ViewTargeterDelegate:
bool ToolbarView::DoesIntersectRect(const views::View* target,
                                    const gfx::Rect& rect) const {
  CHECK_EQ(target, this);

  // Fall through to the tab strip above us if none of |rect| intersects
  // with this view (intersection with the top shadow edge does not
  // count as intersection with this view).
  if (rect.bottom() < content_shadow_height())
    return false;
  // Otherwise let our superclass take care of it.
  return ViewTargeterDelegate::DoesIntersectRect(this, rect);
}

void ToolbarView::UpdateBadgeSeverity(AppMenuBadgeController::BadgeType type,
                                      AppMenuIconPainter::Severity severity,
                                      bool animate) {
  // Showing the bubble requires |app_menu_button_| to be in a widget. See
  // comment in ConflictingModuleView for details.
  DCHECK(app_menu_button_->GetWidget());

  base::string16 accname_app = l10n_util::GetStringUTF16(IDS_ACCNAME_APP);
  if (type == AppMenuBadgeController::BADGE_TYPE_UPGRADE_NOTIFICATION) {
    accname_app = l10n_util::GetStringFUTF16(
        IDS_ACCNAME_APP_UPGRADE_RECOMMENDED, accname_app);
  }
  app_menu_button_->SetAccessibleName(accname_app);
  app_menu_button_->SetSeverity(severity, animate);

  // Keep track of whether we were showing the badge before, so we don't send
  // multiple UMA events for example when multiple Chrome windows are open.
  static bool incompatibility_badge_showing = false;
  // Save the old value before resetting it.
  bool was_showing = incompatibility_badge_showing;
  incompatibility_badge_showing = false;

  if (type == AppMenuBadgeController::BADGE_TYPE_INCOMPATIBILITY_WARNING) {
    if (!was_showing) {
      content::RecordAction(UserMetricsAction("ConflictBadge"));
#if defined(OS_WIN)
      ConflictingModuleView::MaybeShow(browser_, app_menu_button_);
#endif
    }
    incompatibility_badge_showing = true;
    return;
  }
}

int ToolbarView::PopupTopSpacing() const {
  const int kAdditionalPopupTopSpacingNonGlass = 2;
  return views::NonClientFrameView::kClientEdgeThickness +
      (GetWidget()->ShouldWindowContentsBeTransparent() ?
          0 : kAdditionalPopupTopSpacingNonGlass);
}

gfx::Size ToolbarView::SizeForContentSize(gfx::Size size) const {
  if (is_display_mode_normal()) {
    // For Material Design the size of the toolbar is computed using the size
    // of the location bar and constant padding values. For non-material the
    // size is based on the provided assets.
    if (ui::MaterialDesignController::IsModeMaterial()) {
      int content_height = std::max(back_->GetPreferredSize().height(),
                                    location_bar_->GetPreferredSize().height());
      int padding = GetLayoutInsets(TOOLBAR).height();
      size.SetToMax(gfx::Size(0, content_height + padding));
    } else {
      gfx::ImageSkia* normal_background =
          GetThemeProvider()->GetImageSkiaNamed(IDR_CONTENT_TOP_CENTER);
      size.SetToMax(
          gfx::Size(0, normal_background->height() - content_shadow_height()));
    }
  } else if (size.height() == 0) {
    // Location mode with a 0 height location bar. If on ash, expand by one
    // pixel to show a border in the title bar, otherwise leave the size as zero
    // height.
    const int kAshBorderSpacing = 1;
    if (browser_->host_desktop_type() == chrome::HOST_DESKTOP_TYPE_ASH)
      size.Enlarge(0, kAshBorderSpacing);
  } else {
    size.Enlarge(
        0, PopupTopSpacing() + views::NonClientFrameView::kClientEdgeThickness);
  }
  return size;
}

void ToolbarView::LoadImages() {
  const ui::ThemeProvider* tp = GetThemeProvider();

  if (ui::MaterialDesignController::IsModeMaterial()) {
    const int kButtonSize = 16;
    const SkColor normal_color =
        tp->GetColor(ThemeProperties::COLOR_TOOLBAR_BUTTON_ICON);
    const SkColor disabled_color =
        tp->GetColor(ThemeProperties::COLOR_TOOLBAR_BUTTON_ICON_INACTIVE);

    back_->SetImage(views::Button::STATE_NORMAL,
                    gfx::CreateVectorIcon(gfx::VectorIconId::NAVIGATE_BACK,
                                          kButtonSize, normal_color));
    back_->SetImage(views::Button::STATE_DISABLED,
                    gfx::CreateVectorIcon(gfx::VectorIconId::NAVIGATE_BACK,
                                          kButtonSize, disabled_color));
    forward_->SetImage(
        views::Button::STATE_NORMAL,
        gfx::CreateVectorIcon(gfx::VectorIconId::NAVIGATE_FORWARD, kButtonSize,
                              normal_color));
    forward_->SetImage(
        views::Button::STATE_DISABLED,
        gfx::CreateVectorIcon(gfx::VectorIconId::NAVIGATE_FORWARD, kButtonSize,
                              disabled_color));
    home_->SetImage(views::Button::STATE_NORMAL,
                    gfx::CreateVectorIcon(gfx::VectorIconId::NAVIGATE_HOME,
                                          kButtonSize, normal_color));
    app_menu_button_->UpdateIcon();
  } else {
    back_->SetImage(views::Button::STATE_NORMAL,
                    *(tp->GetImageSkiaNamed(IDR_BACK)));
    back_->SetImage(views::Button::STATE_DISABLED,
                    *(tp->GetImageSkiaNamed(IDR_BACK_D)));
    forward_->SetImage(views::Button::STATE_NORMAL,
                       *(tp->GetImageSkiaNamed(IDR_FORWARD)));
    forward_->SetImage(views::Button::STATE_DISABLED,
                       *(tp->GetImageSkiaNamed(IDR_FORWARD_D)));
    home_->SetImage(views::Button::STATE_NORMAL,
                    *(tp->GetImageSkiaNamed(IDR_HOME)));
  }

  reload_->LoadImages();
}

void ToolbarView::ShowCriticalNotification() {
#if defined(OS_WIN)
  CriticalNotificationBubbleView* bubble_delegate =
      new CriticalNotificationBubbleView(app_menu_button_);
  views::BubbleDelegateView::CreateBubble(bubble_delegate)->Show();
#endif
}

void ToolbarView::ShowOutdatedInstallNotification(bool auto_update_enabled) {
  if (OutdatedUpgradeBubbleView::IsAvailable()) {
    OutdatedUpgradeBubbleView::ShowBubble(app_menu_button_, browser_,
                                          auto_update_enabled);
  }
}

void ToolbarView::OnShowHomeButtonChanged() {
  Layout();
  SchedulePaint();
}

int ToolbarView::content_shadow_height() const {
  return GetLayoutConstant(
      (browser_->host_desktop_type() == chrome::HOST_DESKTOP_TYPE_ASH) ?
          TOOLBAR_CONTENT_SHADOW_HEIGHT_ASH : TOOLBAR_CONTENT_SHADOW_HEIGHT);
}
