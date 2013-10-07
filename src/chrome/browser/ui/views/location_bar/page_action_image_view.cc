// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/page_action_image_view.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/api/commands/command_service.h"
#include "chrome/browser/extensions/extension_action.h"
#include "chrome/browser/extensions/extension_action_icon_factory.h"
#include "chrome/browser/extensions/extension_action_manager.h"
#include "chrome/browser/extensions/extension_context_menu_model.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/location_bar_controller.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_id.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/webui/extensions/extension_info_ui.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "ui/base/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_runner.h"

using content::WebContents;
using extensions::LocationBarController;
using extensions::Extension;

PageActionImageView::PageActionImageView(LocationBarView* owner,
                                         ExtensionAction* page_action,
                                         Browser* browser)
    : owner_(owner),
      page_action_(page_action),
      browser_(browser),
      current_tab_id_(-1),
      preview_enabled_(false),
      popup_(NULL),
      scoped_icon_animation_observer_(
          page_action->GetIconAnimation(
              SessionID::IdForTab(owner->GetWebContents())),
          this) {
  const Extension* extension = owner_->profile()->GetExtensionService()->
      GetExtensionById(page_action->extension_id(), false);
  DCHECK(extension);

  icon_factory_.reset(
      new ExtensionActionIconFactory(
          owner_->profile(), extension, page_action, this));

  set_accessibility_focusable(true);
  set_context_menu_controller(this);

  extensions::CommandService* command_service =
      extensions::CommandService::Get(browser_->profile());
  extensions::Command page_action_command;
  if (command_service->GetPageActionCommand(
          extension->id(),
          extensions::CommandService::ACTIVE_ONLY,
          &page_action_command,
          NULL)) {
    page_action_keybinding_.reset(
        new ui::Accelerator(page_action_command.accelerator()));
    owner_->GetFocusManager()->RegisterAccelerator(
        *page_action_keybinding_.get(),
        ui::AcceleratorManager::kHighPriority,
        this);
  }

  extensions::Command script_badge_command;
  if (command_service->GetScriptBadgeCommand(
          extension->id(),
          extensions::CommandService::ACTIVE_ONLY,
          &script_badge_command,
          NULL)) {
    script_badge_keybinding_.reset(
        new ui::Accelerator(script_badge_command.accelerator()));
    owner_->GetFocusManager()->RegisterAccelerator(
        *script_badge_keybinding_.get(),
        ui::AcceleratorManager::kHighPriority,
        this);
  }
}

PageActionImageView::~PageActionImageView() {
  if (owner_->GetFocusManager()) {
    if (page_action_keybinding_.get()) {
      owner_->GetFocusManager()->UnregisterAccelerator(
          *page_action_keybinding_.get(), this);
    }

    if (script_badge_keybinding_.get()) {
      owner_->GetFocusManager()->UnregisterAccelerator(
          *script_badge_keybinding_.get(), this);
    }
  }

  if (popup_)
    popup_->GetWidget()->RemoveObserver(this);
  HidePopup();
}

void PageActionImageView::ExecuteAction(
    ExtensionPopup::ShowAction show_action) {
  WebContents* web_contents = owner_->GetWebContents();
  if (!web_contents)
    return;

  extensions::TabHelper* extensions_tab_helper =
      extensions::TabHelper::FromWebContents(web_contents);
  LocationBarController* controller =
      extensions_tab_helper->location_bar_controller();

  switch (controller->OnClicked(page_action_->extension_id(), 1)) {
    case LocationBarController::ACTION_NONE:
      break;

    case LocationBarController::ACTION_SHOW_POPUP:
      ShowPopupWithURL(page_action_->GetPopupUrl(current_tab_id_), show_action);
      break;

    case LocationBarController::ACTION_SHOW_CONTEXT_MENU:
      // We are never passing OnClicked a right-click button, so assume that
      // we're never going to be asked to show a context menu.
      // TODO(kalman): if this changes, update this class to pass the real
      // mouse button through to the LocationBarController.
      NOTREACHED();
      break;

    case LocationBarController::ACTION_SHOW_SCRIPT_POPUP:
      ShowPopupWithURL(
          extensions::ExtensionInfoUI::GetURL(page_action_->extension_id()),
          show_action);
      break;
  }
}

void PageActionImageView::GetAccessibleState(ui::AccessibleViewState* state) {
  state->role = ui::AccessibilityTypes::ROLE_PUSHBUTTON;
  state->name = UTF8ToUTF16(tooltip_);
}

bool PageActionImageView::OnMousePressed(const ui::MouseEvent& event) {
  // We want to show the bubble on mouse release; that is the standard behavior
  // for buttons.  (Also, triggering on mouse press causes bugs like
  // http://crbug.com/33155.)
  return true;
}

void PageActionImageView::OnMouseReleased(const ui::MouseEvent& event) {
  if (!HitTestPoint(event.location()))
    return;

  if (event.IsRightMouseButton()) {
    // Don't show a menu here, its handled in View::ProcessMouseReleased. We
    // show the context menu by way of being the ContextMenuController.
    return;
  }

  ExecuteAction(ExtensionPopup::SHOW);
}

bool PageActionImageView::OnKeyPressed(const ui::KeyEvent& event) {
  if (event.key_code() == ui::VKEY_SPACE ||
      event.key_code() == ui::VKEY_RETURN) {
    ExecuteAction(ExtensionPopup::SHOW);
    return true;
  }
  return false;
}

void PageActionImageView::ShowContextMenuForView(
    View* source,
    const gfx::Point& point,
    ui::MenuSourceType source_type) {
  const Extension* extension = owner_->profile()->GetExtensionService()->
      GetExtensionById(page_action()->extension_id(), false);
  if (!extension->ShowConfigureContextMenus())
    return;

  scoped_refptr<ExtensionContextMenuModel> context_menu_model(
      new ExtensionContextMenuModel(extension, browser_, this));
  menu_runner_.reset(new views::MenuRunner(context_menu_model.get()));
  gfx::Point screen_loc;
  views::View::ConvertPointToScreen(this, &screen_loc);
  if (menu_runner_->RunMenuAt(GetWidget(), NULL, gfx::Rect(screen_loc, size()),
          views::MenuItemView::TOPLEFT, source_type,
          views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU) ==
      views::MenuRunner::MENU_DELETED)
    return;
}

bool PageActionImageView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  DCHECK(visible());  // Should not have happened due to CanHandleAccelerator.

  ExecuteAction(ExtensionPopup::SHOW);
  return true;
}

bool PageActionImageView::CanHandleAccelerators() const {
  // While visible, we don't handle accelerators and while so we also don't
  // count as a priority accelerator handler.
  return visible();
}

void PageActionImageView::UpdateVisibility(WebContents* contents,
                                           const GURL& url) {
  // Save this off so we can pass it back to the extension when the action gets
  // executed. See PageActionImageView::OnMousePressed.
  current_tab_id_ = contents ? ExtensionTabUtil::GetTabId(contents) : -1;
  current_url_ = url;

  if (!contents ||
      (!preview_enabled_ && !page_action_->GetIsVisible(current_tab_id_))) {
    SetVisible(false);
    return;
  }

  // Set the tooltip.
  tooltip_ = page_action_->GetTitle(current_tab_id_);
  SetTooltipText(UTF8ToUTF16(tooltip_));

  // Set the image.
  gfx::Image icon = icon_factory_->GetIcon(current_tab_id_);
  if (!icon.IsEmpty())
    SetImage(*icon.ToImageSkia());

  SetVisible(true);
}

void PageActionImageView::InspectPopup(ExtensionAction* action) {
  ExecuteAction(ExtensionPopup::SHOW_AND_INSPECT);
}

void PageActionImageView::OnWidgetDestroying(views::Widget* widget) {
  DCHECK_EQ(popup_->GetWidget(), widget);
  popup_->GetWidget()->RemoveObserver(this);
  popup_ = NULL;
}

void PageActionImageView::OnIconUpdated() {
  WebContents* web_contents = owner_->GetWebContents();
  if (web_contents)
    UpdateVisibility(web_contents, current_url_);
}

void PageActionImageView::OnIconChanged() {
  OnIconUpdated();
}

void PageActionImageView::PaintChildren(gfx::Canvas* canvas) {
  View::PaintChildren(canvas);
  if (current_tab_id_ >= 0)
    page_action_->PaintBadge(canvas, GetLocalBounds(), current_tab_id_);
}

void PageActionImageView::ShowPopupWithURL(
    const GURL& popup_url,
    ExtensionPopup::ShowAction show_action) {
  bool popup_showing = popup_ != NULL;

  // Always hide the current popup. Only one popup at a time.
  HidePopup();

  // If we were already showing, then treat this click as a dismiss.
  if (popup_showing)
    return;

  views::BubbleBorder::Arrow arrow = base::i18n::IsRTL() ?
      views::BubbleBorder::TOP_LEFT : views::BubbleBorder::TOP_RIGHT;

  popup_ = ExtensionPopup::ShowPopup(popup_url, browser_, this, arrow,
                                     show_action);
  popup_->GetWidget()->AddObserver(this);
}

void PageActionImageView::HidePopup() {
  if (popup_)
    popup_->GetWidget()->Close();
}
