// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tab_contents/render_view_context_menu_views.h"

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/generated_resources.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/gfx/point.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_runner.h"

using content::WebContents;

////////////////////////////////////////////////////////////////////////////////
// RenderViewContextMenuViews, public:

RenderViewContextMenuViews::RenderViewContextMenuViews(
    WebContents* web_contents,
    const content::ContextMenuParams& params)
    : RenderViewContextMenu(web_contents, params) {
}

RenderViewContextMenuViews::~RenderViewContextMenuViews() {
}

#if !defined(OS_WIN)
// static
RenderViewContextMenuViews* RenderViewContextMenuViews::Create(
    content::WebContents* web_contents,
    const content::ContextMenuParams& params) {
  return new RenderViewContextMenuViews(web_contents, params);
}
#endif  // OS_WIN

void RenderViewContextMenuViews::RunMenuAt(views::Widget* parent,
                                           const gfx::Point& point,
                                           ui::MenuSourceType type) {
  views::MenuItemView::AnchorPosition anchor_position =
      (type == ui::MENU_SOURCE_TOUCH ||
          type == ui::MENU_SOURCE_TOUCH_EDIT_MENU) ?
          views::MenuItemView::BOTTOMCENTER : views::MenuItemView::TOPLEFT;

  if (menu_runner_->RunMenuAt(parent, NULL, gfx::Rect(point, gfx::Size()),
      anchor_position, type, views::MenuRunner::HAS_MNEMONICS |
          views::MenuRunner::CONTEXT_MENU) ==
      views::MenuRunner::MENU_DELETED)
    return;
}

////////////////////////////////////////////////////////////////////////////////
// RenderViewContextMenuViews, protected:

void RenderViewContextMenuViews::PlatformInit() {
  menu_runner_.reset(new views::MenuRunner(&menu_model_));
}

void RenderViewContextMenuViews::PlatformCancel() {
  DCHECK(menu_runner_.get());
  menu_runner_->Cancel();
}

bool RenderViewContextMenuViews::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accel) {
  // There are no formally defined accelerators we can query so we assume
  // that Ctrl+C, Ctrl+V, Ctrl+X, Ctrl-A, etc do what they normally do.
  switch (command_id) {
    case IDC_CONTENT_CONTEXT_UNDO:
      *accel = ui::Accelerator(ui::VKEY_Z, ui::EF_CONTROL_DOWN);
      return true;

    case IDC_CONTENT_CONTEXT_REDO:
      // TODO(jcampan): should it be Ctrl-Y?
      *accel = ui::Accelerator(ui::VKEY_Z,
                               ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN);
      return true;

    case IDC_CONTENT_CONTEXT_CUT:
      *accel = ui::Accelerator(ui::VKEY_X, ui::EF_CONTROL_DOWN);
      return true;

    case IDC_CONTENT_CONTEXT_COPY:
      *accel = ui::Accelerator(ui::VKEY_C, ui::EF_CONTROL_DOWN);
      return true;

    case IDC_CONTENT_CONTEXT_PASTE:
      *accel = ui::Accelerator(ui::VKEY_V, ui::EF_CONTROL_DOWN);
      return true;

    case IDC_CONTENT_CONTEXT_PASTE_AND_MATCH_STYLE:
      *accel = ui::Accelerator(ui::VKEY_V,
                               ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN);
      return true;

    case IDC_CONTENT_CONTEXT_SELECTALL:
      *accel = ui::Accelerator(ui::VKEY_A, ui::EF_CONTROL_DOWN);
      return true;

    default:
      return false;
  }
}

void RenderViewContextMenuViews::UpdateMenuItem(int command_id,
                                                bool enabled,
                                                bool hidden,
                                                const string16& title) {
  views::MenuItemView* item =
      menu_runner_->GetMenu()->GetMenuItemByID(command_id);
  if (!item)
    return;

  item->SetEnabled(enabled);
  item->SetTitle(title);
  item->SetVisible(!hidden);

  views::MenuItemView* parent = item->GetParentMenuItem();
  if (!parent)
    return;

  parent->ChildrenChanged();
}
