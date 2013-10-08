// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/global_error_bubble_view.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/global_error/global_error.h"
#include "chrome/browser/ui/global_error/global_error_service.h"
#include "chrome/browser/ui/global_error/global_error_service_factory.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar_view.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/layout/layout_constants.h"

namespace {

enum {
  TAG_ACCEPT_BUTTON = 1,
  TAG_CANCEL_BUTTON,
};

const int kMaxBubbleViewWidth = 262;

// The horizontal padding between the title and the icon.
const int kTitleHorizontalPadding = 5;

// The vertical inset of the wrench bubble anchor from the wrench menu button.
const int kAnchorVerticalInset = 5;

const int kBubblePadding = 6;

}  // namespace

// GlobalErrorBubbleViewBase ---------------------------------------------------

// static
GlobalErrorBubbleViewBase* GlobalErrorBubbleViewBase::ShowBubbleView(
    Browser* browser,
    const base::WeakPtr<GlobalError>& error) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  views::View* wrench_button = browser_view->toolbar()->app_menu();
  GlobalErrorBubbleView* bubble_view =
      new GlobalErrorBubbleView(wrench_button,
                                views::BubbleBorder::TOP_RIGHT,
                                browser,
                                error);
  views::BubbleDelegateView::CreateBubble(bubble_view);
  bubble_view->StartFade(true);
  return bubble_view;
}

// GlobalErrorBubbleView -------------------------------------------------------

GlobalErrorBubbleView::GlobalErrorBubbleView(
    views::View* anchor_view,
    views::BubbleBorder::Arrow arrow,
    Browser* browser,
    const base::WeakPtr<GlobalError>& error)
    : BubbleDelegateView(anchor_view, arrow),
      browser_(browser),
      error_(error) {
  // Compensate for built-in vertical padding in the anchor view's image.
  set_anchor_view_insets(
      gfx::Insets(kAnchorVerticalInset, 0, kAnchorVerticalInset, 0));

  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  gfx::Image image = error_->GetBubbleViewIcon();
  CHECK(!image.IsEmpty());
  scoped_ptr<views::ImageView> image_view(new views::ImageView());
  image_view->SetImage(image.ToImageSkia());

  string16 title_string(error_->GetBubbleViewTitle());
  scoped_ptr<views::Label> title_label(new views::Label(title_string));
  title_label->SetMultiLine(true);
  title_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label->SetFont(rb.GetFont(ui::ResourceBundle::MediumFont));

  std::vector<string16> message_strings(error_->GetBubbleViewMessages());
  std::vector<views::Label*> message_labels;
  for (size_t i = 0; i < message_strings.size(); ++i) {
    views::Label* message_label = new views::Label(message_strings[i]);
    message_label->SetMultiLine(true);
    message_label->SizeToFit(kMaxBubbleViewWidth);
    message_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    message_labels.push_back(message_label);
  }

  string16 accept_string(error_->GetBubbleViewAcceptButtonLabel());
  scoped_ptr<views::LabelButton> accept_button(
      new views::LabelButton(this, accept_string));
  accept_button->SetStyle(views::Button::STYLE_NATIVE_TEXTBUTTON);
  accept_button->SetIsDefault(true);
  accept_button->set_tag(TAG_ACCEPT_BUTTON);

  string16 cancel_string(error_->GetBubbleViewCancelButtonLabel());
  scoped_ptr<views::LabelButton> cancel_button;
  if (!cancel_string.empty()) {
    cancel_button.reset(new views::LabelButton(this, cancel_string));
    cancel_button->SetStyle(views::Button::STYLE_NATIVE_TEXTBUTTON);
    cancel_button->set_tag(TAG_CANCEL_BUTTON);
  }

  views::GridLayout* layout = new views::GridLayout(this);
  SetLayoutManager(layout);
  layout->SetInsets(kBubblePadding, kBubblePadding,
                    kBubblePadding, kBubblePadding);

  // Top row, icon and title.
  views::ColumnSet* cs = layout->AddColumnSet(0);
  cs->AddColumn(views::GridLayout::LEADING, views::GridLayout::LEADING,
                0, views::GridLayout::USE_PREF, 0, 0);
  cs->AddPaddingColumn(0, kTitleHorizontalPadding);
  cs->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL,
                1, views::GridLayout::USE_PREF, 0, 0);

  // Middle rows, message labels.
  cs = layout->AddColumnSet(1);
  cs->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL,
                1, views::GridLayout::USE_PREF, 0, 0);

  // Bottom row, accept and cancel button.
  cs = layout->AddColumnSet(2);
  cs->AddPaddingColumn(1, views::kRelatedControlHorizontalSpacing);
  cs->AddColumn(views::GridLayout::TRAILING, views::GridLayout::LEADING,
                0, views::GridLayout::USE_PREF, 0, 0);
  if (cancel_button.get()) {
    cs->AddPaddingColumn(0, views::kRelatedButtonHSpacing);
    cs->AddColumn(views::GridLayout::TRAILING, views::GridLayout::LEADING,
                  0, views::GridLayout::USE_PREF, 0, 0);
  }

  layout->StartRow(1, 0);
  layout->AddView(image_view.release());
  layout->AddView(title_label.release());
  layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

  for (size_t i = 0; i < message_labels.size(); ++i) {
    layout->StartRow(1, 1);
    layout->AddView(message_labels[i]);
    if (i < message_labels.size() - 1)
      layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
  }
  layout->AddPaddingRow(0, views::kLabelToControlVerticalSpacing);

  layout->StartRow(0, 2);
  layout->AddView(accept_button.release());
  if (cancel_button.get())
    layout->AddView(cancel_button.release());

  // Adjust the message label size in case buttons are too long.
  for (size_t i = 0; i < message_labels.size(); ++i)
    message_labels[i]->SizeToFit(layout->GetPreferredSize(this).width());
}

GlobalErrorBubbleView::~GlobalErrorBubbleView() {
}

void GlobalErrorBubbleView::ButtonPressed(views::Button* sender,
                                          const ui::Event& event) {
  if (error_) {
    if (sender->tag() == TAG_ACCEPT_BUTTON)
      error_->BubbleViewAcceptButtonPressed(browser_);
    else if (sender->tag() == TAG_CANCEL_BUTTON)
      error_->BubbleViewCancelButtonPressed(browser_);
    else
      NOTREACHED();
  }
  GetWidget()->Close();
}

void GlobalErrorBubbleView::WindowClosing() {
  if (error_)
    error_->BubbleViewDidClose(browser_);
}

void GlobalErrorBubbleView::CloseBubbleView() {
  GetWidget()->Close();
}
