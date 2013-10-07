// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/autofill/autocheckout_bubble_views.h"

#include "chrome/browser/ui/autofill/autocheckout_bubble.h"
#include "chrome/browser/ui/autofill/autocheckout_bubble_controller.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/rect.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/layout/layout_constants.h"
#include "ui/views/widget/widget.h"

#if defined(USE_AURA)
#include "ui/aura/window.h"
#endif

namespace autofill {

AutocheckoutBubbleViews::AutocheckoutBubbleViews(
    scoped_ptr<AutocheckoutBubbleController> controller,
    views::View* anchor_view)
  : views::BubbleDelegateView(anchor_view, views::BubbleBorder::TOP_LEFT),
    controller_(controller.Pass()),
    ok_button_(NULL),
    cancel_button_(NULL) {
  set_parent_window(controller_->native_window());
  controller_->BubbleCreated();
}

AutocheckoutBubbleViews::~AutocheckoutBubbleViews() {
  controller_->BubbleDestroyed();
}

void AutocheckoutBubbleViews::ShowBubble() {
  StartFade(true);
}

void AutocheckoutBubbleViews::HideBubble() {
  StartFade(false);
}

void AutocheckoutBubbleViews::Init() {
  views::GridLayout* layout = new views::GridLayout(this);
  SetLayoutManager(layout);

  // Add the message label to the first row.
  views::ColumnSet* cs = layout->AddColumnSet(1);
  views::Label* message_label = new views::Label(controller_->PromptText());

  // Maximum width for the message field in pixels. The message text will be
  // wrapped when its width is wider than this.
  const int kMaxMessageWidth = 300;

  int message_width =
      std::min(kMaxMessageWidth, message_label->GetPreferredSize().width());
  cs->AddColumn(views::GridLayout::LEADING,
                views::GridLayout::CENTER,
                0,
                views::GridLayout::FIXED,
                message_width,
                false);
  message_label->SetBounds(0, 0, message_width, 0);
  message_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  message_label->SetMultiLine(true);
  layout->StartRow(0, 1);
  layout->AddView(message_label);

  // Padding between message and buttons.
  layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

  cs = layout->AddColumnSet(2);
  cs->AddPaddingColumn(1, views::kRelatedControlHorizontalSpacing);
  cs->AddColumn(views::GridLayout::CENTER,
                views::GridLayout::CENTER,
                0,
                views::GridLayout::USE_PREF,
                0,
                0);
  cs->AddPaddingColumn(0, views::kRelatedButtonHSpacing);
  cs->AddColumn(views::GridLayout::CENTER,
                views::GridLayout::CENTER,
                0,
                views::GridLayout::USE_PREF,
                0,
                0);
  layout->StartRow(0, 2);

  if (!controller_->NormalImage().IsEmpty()) {
    views::ImageButton* image_button = new views::ImageButton(this);
    image_button->SetImage(views::Button::STATE_NORMAL,
                           controller_->NormalImage().ToImageSkia());
    image_button->SetImage(views::Button::STATE_HOVERED,
                           controller_->HoverImage().ToImageSkia());
    image_button->SetImage(views::Button::STATE_PRESSED,
                           controller_->PressedImage().ToImageSkia());
    ok_button_ = image_button;
  } else {
    views::LabelButton* label_button = new views::LabelButton(
        this, AutocheckoutBubbleController::AcceptText());
    label_button->SetStyle(views::Button::STYLE_NATIVE_TEXTBUTTON);
    ok_button_ = label_button;
  }
  layout->AddView(ok_button_);

  cancel_button_ =
      new views::LabelButton(this, AutocheckoutBubbleController::CancelText());
  cancel_button_->SetStyle(views::Button::STYLE_NATIVE_TEXTBUTTON);
  layout->AddView(cancel_button_);
}

gfx::Rect AutocheckoutBubbleViews::GetAnchorRect() {
  return controller_->anchor_rect();
}

void AutocheckoutBubbleViews::OnWidgetBoundsChanged(
    views::Widget* widget,
    const gfx::Rect& new_bounds) {
  if (anchor_widget() == widget)
    HideBubble();
}

void AutocheckoutBubbleViews::ButtonPressed(views::Button* sender,
                                            const ui::Event& event)  {
  if (sender == ok_button_) {
    controller_->BubbleAccepted();
  } else if (sender == cancel_button_) {
    controller_->BubbleCanceled();
  } else {
    NOTREACHED();
  }
  GetWidget()->Close();
}

// static
base::WeakPtr<AutocheckoutBubble> AutocheckoutBubble::Create(
    scoped_ptr<AutocheckoutBubbleController> controller) {
#if defined(USE_AURA)
  // If the page hasn't yet been attached to a RootWindow,
  // Aura code for creating the bubble will fail.
  if (!controller->native_window()->GetRootWindow())
    return base::WeakPtr<AutocheckoutBubble>();
#endif // defined(USE_AURA)

  views::Widget* widget = views::Widget::GetTopLevelWidgetForNativeView(
      controller->native_window());
  // The bubble owns itself.
  AutocheckoutBubbleViews* delegate =
      new AutocheckoutBubbleViews(controller.Pass(),
                                  widget ? widget->GetContentsView() : NULL);
  views::BubbleDelegateView::CreateBubble(delegate);
  delegate->SetAlignment(views::BubbleBorder::ALIGN_EDGE_TO_ANCHOR_EDGE);
  return delegate->AsWeakPtr();
}

}  // namespace autofill
