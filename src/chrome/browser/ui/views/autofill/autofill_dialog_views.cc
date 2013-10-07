// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/autofill/autofill_dialog_views.h"

#include <utility>

#include "base/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/autofill/autofill_dialog_sign_in_delegate.h"
#include "chrome/browser/ui/autofill/autofill_dialog_view_delegate.h"
#include "chrome/browser/ui/views/autofill/decorated_textfield.h"
#include "chrome/browser/ui/views/constrained_window_views.h"
#include "components/autofill/content/browser/wallet/wallet_service_url.h"
#include "components/autofill/core/browser/autofill_type.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "components/web_modal/web_contents_modal_dialog_manager_delegate.h"
#include "content/public/browser/native_web_keyboard_event.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/theme_resources.h"
#include "grit/ui_resources.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/animation/multi_animation.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/combobox_model.h"
#include "ui/base/models/menu_model.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/path.h"
#include "ui/gfx/skia_util.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/button/blue_button.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/label_button_border.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/focusable_border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/layout/layout_constants.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_client_view.h"

using web_modal::WebContentsModalDialogManager;

namespace autofill {

namespace {

// The minimum useful height of the contents area of the dialog.
const int kMinimumContentsHeight = 100;

// Horizontal padding between text and other elements (in pixels).
const int kAroundTextPadding = 4;

// The space between the edges of a notification bar and the text within (in
// pixels).
const int kNotificationPadding = 17;

// Vertical padding above and below each detail section (in pixels).
const int kDetailSectionVerticalPadding = 10;

const int kAutocheckoutStepsAreaPadding = 28;
const int kAutocheckoutStepInset = 20;

const int kAutocheckoutProgressBarWidth = 375;
const int kAutocheckoutProgressBarHeight = 15;

const int kArrowHeight = 7;
const int kArrowWidth = 2 * kArrowHeight;

// The padding inside the edges of the dialog, in pixels.
const int kDialogEdgePadding = 20;

// Slight shading for mouse hover and legal document background.
SkColor kShadingColor = SkColorSetARGB(7, 0, 0, 0);

// A border color for the legal document view.
SkColor kSubtleBorderColor = SkColorSetARGB(10, 0, 0, 0);

// The top and bottom padding, in pixels, for the suggestions menu dropdown
// arrows.
const int kMenuButtonTopInset = 3;
const int kMenuButtonBottomInset = 6;

// Spacing between lines of text in the overlay view.
const int kOverlayTextInterlineSpacing = 10;

// Spacing below image and above text messages in overlay view.
const int kOverlayImageBottomMargin = 50;

// A dimmer text color used in various parts of the dialog. TODO(estade): should
// this be part of NativeTheme? Currently the value is duplicated in several
// places.
const SkColor kGreyTextColor = SkColorSetRGB(102, 102, 102);

const char kNotificationAreaClassName[] = "autofill/NotificationArea";
const char kOverlayViewClassName[] = "autofill/OverlayView";

typedef ui::MultiAnimation::Part Part;
typedef ui::MultiAnimation::Parts Parts;

// Draws an arrow at the top of |canvas| pointing to |tip_x|.
void DrawArrow(gfx::Canvas* canvas,
               int tip_x,
               const SkColor& fill_color,
               const SkColor& stroke_color) {
  const int arrow_half_width = kArrowWidth / 2.0f;

  SkPath arrow;
  arrow.moveTo(tip_x - arrow_half_width, kArrowHeight);
  arrow.lineTo(tip_x, 0);
  arrow.lineTo(tip_x + arrow_half_width, kArrowHeight);

  SkPaint fill_paint;
  fill_paint.setColor(fill_color);
  canvas->DrawPath(arrow, fill_paint);

  if (stroke_color != SK_ColorTRANSPARENT) {
    SkPaint stroke_paint;
    stroke_paint.setColor(stroke_color);
    stroke_paint.setStyle(SkPaint::kStroke_Style);
    canvas->DrawPath(arrow, stroke_paint);
  }
}

// This class handles layout for the first row of a SuggestionView.
// It exists to circumvent shortcomings of GridLayout and BoxLayout (namely that
// the former doesn't fully respect child visibility, and that the latter won't
// expand a single child).
class SectionRowView : public views::View {
 public:
  SectionRowView() {}
  virtual ~SectionRowView() {}

  // views::View implementation:
  virtual gfx::Size GetPreferredSize() OVERRIDE {
    int height = 0;
    int width = 0;
    for (int i = 0; i < child_count(); ++i) {
      if (child_at(i)->visible()) {
        if (width > 0)
          width += kAroundTextPadding;

        gfx::Size size = child_at(i)->GetPreferredSize();
        height = std::max(height, size.height());
        width += size.width();
      }
    }

    return gfx::Size(width, height);
  }

  virtual void Layout() OVERRIDE {
    const gfx::Rect bounds = GetContentsBounds();

    // Icon is left aligned.
    int start_x = bounds.x();
    views::View* icon = child_at(0);
    if (icon->visible()) {
      icon->SizeToPreferredSize();
      icon->SetX(start_x);
      icon->SetY(bounds.y() +
          (bounds.height() - icon->bounds().height()) / 2);
      start_x += icon->bounds().width() + kAroundTextPadding;
    }

    // Textfield is right aligned.
    int end_x = bounds.width();
    views::View* decorated = child_at(2);
    if (decorated->visible()) {
      decorated->SizeToPreferredSize();
      decorated->SetX(bounds.width() - decorated->bounds().width());
      decorated->SetY(bounds.y());
      end_x = decorated->bounds().x() - kAroundTextPadding;
    }

    // Label takes up all the space in between.
    views::View* label = child_at(1);
    if (label->visible())
      label->SetBounds(start_x, bounds.y(), end_x - start_x, bounds.height());

    views::View::Layout();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(SectionRowView);
};

// This view is used for the contents of the error bubble widget.
class ErrorBubbleContents : public views::View {
 public:
  explicit ErrorBubbleContents(const base::string16& message)
      : color_(kWarningColor) {
    set_border(views::Border::CreateEmptyBorder(kArrowHeight - 3, 0, 0, 0));

    views::Label* label = new views::Label();
    label->SetText(message);
    label->SetAutoColorReadabilityEnabled(false);
    label->SetEnabledColor(SK_ColorWHITE);
    label->set_border(
        views::Border::CreateSolidSidedBorder(5, 10, 5, 10, color_));
    label->set_background(
        views::Background::CreateSolidBackground(color_));
    SetLayoutManager(new views::FillLayout());
    AddChildView(label);
  }
  virtual ~ErrorBubbleContents() {}

  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE {
    views::View::OnPaint(canvas);
    DrawArrow(canvas, width() / 2.0f, color_, SK_ColorTRANSPARENT);
  }

 private:
  SkColor color_;

  DISALLOW_COPY_AND_ASSIGN(ErrorBubbleContents);
};

// A view that runs a callback whenever its bounds change.
class DetailsContainerView : public views::View {
 public:
  explicit DetailsContainerView(const base::Closure& callback)
      : bounds_changed_callback_(callback) {}
  virtual ~DetailsContainerView() {}

  // views::View implementation.
  virtual void OnBoundsChanged(const gfx::Rect& previous_bounds) OVERRIDE {
    bounds_changed_callback_.Run();
  }

 private:
  base::Closure bounds_changed_callback_;

  DISALLOW_COPY_AND_ASSIGN(DetailsContainerView);
};

// A view that propagates visibility and preferred size changes.
class LayoutPropagationView : public views::View {
 public:
  LayoutPropagationView() {}
  virtual ~LayoutPropagationView() {}

 protected:
  virtual void ChildVisibilityChanged(views::View* child) OVERRIDE {
    PreferredSizeChanged();
  }
  virtual void ChildPreferredSizeChanged(views::View* child) OVERRIDE {
    PreferredSizeChanged();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(LayoutPropagationView);
};

// A class which displays the status of an individual step in an
// Autocheckout flow.
class AutocheckoutStepProgressView : public views::View {
 public:
  AutocheckoutStepProgressView(const base::string16& description,
                               const gfx::Font& font,
                               const SkColor color,
                               const bool is_icon_visible) {
    views::GridLayout* layout = new views::GridLayout(this);
    SetLayoutManager(layout);
    const int kColumnSetId = 0;
    views::ColumnSet* columns = layout->AddColumnSet(kColumnSetId);
    columns->AddColumn(views::GridLayout::LEADING,
                       views::GridLayout::CENTER,
                       0,
                       views::GridLayout::USE_PREF,
                       0,
                       0);
    columns->AddPaddingColumn(0, 8);
    columns->AddColumn(views::GridLayout::LEADING,
                       views::GridLayout::CENTER,
                       0,
                       views::GridLayout::USE_PREF,
                       0,
                       0);
    layout->StartRow(0, kColumnSetId);
    views::Label* label = new views::Label();
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->set_border(views::Border::CreateEmptyBorder(0, 0, 0, 0));
    label->SetText(description);
    label->SetFont(font);
    label->SetEnabledColor(color);

    views::ImageView* icon = new views::ImageView();
    icon->SetVisible(is_icon_visible);
    icon->SetImage(ui::ResourceBundle::GetSharedInstance().GetImageNamed(
        IDR_WALLET_STEP_CHECK).ToImageSkia());

    layout->AddView(icon);
    layout->AddView(label);
  }

  virtual ~AutocheckoutStepProgressView() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(AutocheckoutStepProgressView);
};

// A tooltip icon (just an ImageView with a tooltip). Looks like (?).
class TooltipIcon : public views::ImageView {
 public:
  explicit TooltipIcon(const base::string16& tooltip) : tooltip_(tooltip) {
    SetImage(ui::ResourceBundle::GetSharedInstance().GetImageNamed(
        IDR_AUTOFILL_TOOLTIP_ICON).ToImageSkia());
  }
  virtual ~TooltipIcon() {}

  // views::View implementation
  virtual bool GetTooltipText(const gfx::Point& p,
                              base::string16* tooltip) const OVERRIDE {
    *tooltip = tooltip_;
    return !tooltip_.empty();
  }

 private:
  base::string16 tooltip_;

  DISALLOW_COPY_AND_ASSIGN(TooltipIcon);
};

// A View for a single notification banner.
class NotificationView : public views::View {
 public:
  explicit NotificationView(const DialogNotification& data) : checkbox_(NULL) {
    scoped_ptr<views::View> label_view;
    if (data.HasCheckbox()) {
      scoped_ptr<views::Checkbox> checkbox(
          new views::Checkbox(base::string16()));
      if (!data.interactive())
        checkbox->SetState(views::Button::STATE_DISABLED);
      checkbox->SetText(data.display_text());
      checkbox->SetTextMultiLine(true);
      checkbox->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      checkbox->SetTextColor(views::Button::STATE_NORMAL,
                             data.GetTextColor());
      checkbox->SetTextColor(views::Button::STATE_HOVERED,
                             data.GetTextColor());
      checkbox->SetChecked(data.checked());
      checkbox_ = checkbox.get();
      label_view.reset(checkbox.release());
    } else {
      scoped_ptr<views::Label> label(new views::Label());
      label->SetText(data.display_text());
      label->SetMultiLine(true);
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      label->SetAutoColorReadabilityEnabled(false);
      label->SetEnabledColor(data.GetTextColor());
      label_view.reset(label.release());
    }

    AddChildView(label_view.release());

    if (!data.tooltip_text().empty())
      AddChildView(new TooltipIcon(data.tooltip_text()));

    set_background(
       views::Background::CreateSolidBackground(data.GetBackgroundColor()));
    set_border(views::Border::CreateSolidSidedBorder(1, 0, 1, 0,
                                                     data.GetBorderColor()));
  }

  virtual ~NotificationView() {}

  views::Checkbox* checkbox() {
    return checkbox_;
  }

  // views::View implementation
  virtual gfx::Insets GetInsets() const OVERRIDE {
    int vertical_padding = kNotificationPadding;
    if (checkbox_)
      vertical_padding -= 3;
    return gfx::Insets(vertical_padding, kDialogEdgePadding,
                       vertical_padding, kDialogEdgePadding);
  }

  virtual int GetHeightForWidth(int width) OVERRIDE {
    int label_width = width - GetInsets().width();
    if (child_count() > 1) {
      views::View* tooltip_icon = child_at(1);
      label_width -= tooltip_icon->GetPreferredSize().width() +
          kDialogEdgePadding;
    }

    return child_at(0)->GetHeightForWidth(label_width) + GetInsets().height();
  }

  virtual void Layout() OVERRIDE {
    // Surprisingly, GetContentsBounds() doesn't consult GetInsets().
    gfx::Rect bounds = GetLocalBounds();
    bounds.Inset(GetInsets());
    int right_bound = bounds.right();

    if (child_count() > 1) {
      // The icon takes up the entire vertical space and an extra 20px on
      // each side. This increases the hover target for the tooltip.
      views::View* tooltip_icon = child_at(1);
      gfx::Size icon_size = tooltip_icon->GetPreferredSize();
      int icon_width = icon_size.width() + kDialogEdgePadding;
      right_bound -= icon_width;
      tooltip_icon->SetBounds(
          right_bound, 0,
          icon_width + kDialogEdgePadding, GetLocalBounds().height());
    }

    child_at(0)->SetBounds(bounds.x(), bounds.y(),
                           right_bound - bounds.x(), bounds.height());
  }

 private:
  // The checkbox associated with this notification, or NULL if there is none.
  views::Checkbox* checkbox_;

  DISALLOW_COPY_AND_ASSIGN(NotificationView);
};

}  // namespace

// AutofillDialogViews::ErrorBubble --------------------------------------------

AutofillDialogViews::ErrorBubble::ErrorBubble(views::View* anchor,
                                              const base::string16& message)
    : anchor_(anchor),
      contents_(new ErrorBubbleContents(message)),
      observer_(this) {
  widget_ = new views::Widget;
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_POPUP);
  params.opacity = views::Widget::InitParams::TRANSLUCENT_WINDOW;
  views::Widget* anchor_widget = anchor->GetWidget();
  DCHECK(anchor_widget);
  params.parent = anchor_widget->GetNativeView();

  widget_->Init(params);
  widget_->SetContentsView(contents_);
  UpdatePosition();
  observer_.Add(widget_);
}

AutofillDialogViews::ErrorBubble::~ErrorBubble() {
  if (widget_)
    widget_->Close();
}

bool AutofillDialogViews::ErrorBubble::IsShowing() {
  return widget_ && widget_->IsVisible();
}

void AutofillDialogViews::ErrorBubble::UpdatePosition() {
  if (!widget_)
    return;

  if (!anchor_->GetVisibleBounds().IsEmpty()) {
    widget_->SetBounds(GetBoundsForWidget());
    widget_->SetVisibilityChangedAnimationsEnabled(true);
    widget_->Show();
  } else {
    widget_->SetVisibilityChangedAnimationsEnabled(false);
    widget_->Hide();
  }
}

void AutofillDialogViews::ErrorBubble::OnWidgetClosing(views::Widget* widget) {
  DCHECK_EQ(widget_, widget);
  observer_.Remove(widget_);
  widget_ = NULL;
}

gfx::Rect AutofillDialogViews::ErrorBubble::GetBoundsForWidget() {
  gfx::Rect anchor_bounds = anchor_->GetBoundsInScreen();
  gfx::Rect bubble_bounds;
  bubble_bounds.set_size(contents_->GetPreferredSize());
  bubble_bounds.set_x(anchor_bounds.right() -
      (anchor_bounds.width() + bubble_bounds.width()) / 2);
  const int kErrorBubbleOverlap = 3;
  bubble_bounds.set_y(anchor_bounds.bottom() - kErrorBubbleOverlap);

  return bubble_bounds;
}

// AutofillDialogViews::AccountChooser -----------------------------------------

AutofillDialogViews::AccountChooser::AccountChooser(
    AutofillDialogViewDelegate* delegate)
    : image_(new views::ImageView()),
      label_(new views::Label()),
      arrow_(new views::ImageView()),
      link_(new views::Link()),
      delegate_(delegate) {
  SetLayoutManager(
      new views::BoxLayout(views::BoxLayout::kHorizontal, 0, 0,
                           kAroundTextPadding));
  AddChildView(image_);
  AddChildView(label_);

  arrow_->SetImage(ui::ResourceBundle::GetSharedInstance().GetImageNamed(
      IDR_MENU_DROPARROW).ToImageSkia());
  AddChildView(arrow_);

  link_->set_listener(this);
  AddChildView(link_);
}

AutofillDialogViews::AccountChooser::~AccountChooser() {}

void AutofillDialogViews::AccountChooser::Update() {
  SetVisible(!delegate_->ShouldShowSpinner());

  gfx::Image icon = delegate_->AccountChooserImage();
  image_->SetImage(icon.AsImageSkia());
  label_->SetText(delegate_->AccountChooserText());

  bool show_link = !delegate_->MenuModelForAccountChooser();
  label_->SetVisible(!show_link);
  arrow_->SetVisible(!show_link);
  link_->SetText(delegate_->SignInLinkText());
  link_->SetVisible(show_link);

  menu_runner_.reset();

  PreferredSizeChanged();
}

bool AutofillDialogViews::AccountChooser::OnMousePressed(
    const ui::MouseEvent& event) {
  // Return true so we get the release event.
  if (delegate_->MenuModelForAccountChooser())
    return event.IsOnlyLeftMouseButton();

  return false;
}

void AutofillDialogViews::AccountChooser::OnMouseReleased(
    const ui::MouseEvent& event) {
  if (!HitTestPoint(event.location()))
    return;

  ui::MenuModel* model = delegate_->MenuModelForAccountChooser();
  if (!model)
    return;

  menu_runner_.reset(new views::MenuRunner(model));
  ignore_result(
      menu_runner_->RunMenuAt(GetWidget(),
                              NULL,
                              GetBoundsInScreen(),
                              views::MenuItemView::TOPRIGHT,
                              ui::MENU_SOURCE_MOUSE,
                              0));
}

void AutofillDialogViews::AccountChooser::LinkClicked(views::Link* source,
                                                      int event_flags) {
  delegate_->SignInLinkClicked();
}

// AutofillDialogViews::OverlayView --------------------------------------------

AutofillDialogViews::OverlayView::OverlayView(views::ButtonListener* listener)
    : image_view_(new views::ImageView()),
      message_stack_(new views::View()),
      button_(new views::BlueButton(listener, base::string16())) {
  set_background(views::Background::CreateSolidBackground(GetNativeTheme()->
      GetSystemColor(ui::NativeTheme::kColorId_DialogBackground)));

  AddChildView(image_view_);

  AddChildView(message_stack_);
  message_stack_->SetLayoutManager(
      new views::BoxLayout(views::BoxLayout::kVertical, 0, 0,
                           kOverlayTextInterlineSpacing));
  message_stack_->set_border(views::Border::CreateEmptyBorder(
      kDialogEdgePadding, kDialogEdgePadding, 0, kDialogEdgePadding));

  AddChildView(button_);
  button_->set_focusable(true);
}

AutofillDialogViews::OverlayView::~OverlayView() {}

int AutofillDialogViews::OverlayView::GetHeightForContentsForWidth(int width) {
  // In this case, 0 means "no preference".
  if (!message_stack_->visible())
    return 0;

  return kOverlayImageBottomMargin +
      views::kButtonVEdgeMarginNew +
      message_stack_->GetHeightForWidth(width) +
      image_view_->GetHeightForWidth(width) +
      (button_->visible() ? button_->GetHeightForWidth(width) +
          views::kButtonVEdgeMarginNew : 0);
}

void AutofillDialogViews::OverlayView::SetState(
    const DialogOverlayState& state,
    views::ButtonListener* listener) {
  // Don't update anything if we're still fading out the old state.
  if (fade_out_)
    return;

  if (state.image.IsEmpty()) {
    SetVisible(false);
    return;
  }

  image_view_->SetImage(state.image.ToImageSkia());

  message_stack_->RemoveAllChildViews(true);
  for (size_t i = 0; i < state.strings.size(); ++i) {
    views::Label* label = new views::Label();
    label->SetAutoColorReadabilityEnabled(false);
    label->SetMultiLine(true);
    label->SetText(state.strings[i].text);
    label->SetFont(state.strings[i].font);
    label->SetEnabledColor(state.strings[i].text_color);
    label->SetHorizontalAlignment(state.strings[i].alignment);
    message_stack_->AddChildView(label);
  }
  message_stack_->SetVisible(message_stack_->child_count() > 0);

  button_->SetVisible(!state.button_text.empty());
  if (!state.button_text.empty())
    button_->SetText(state.button_text);

  SetVisible(true);
  if (parent())
    parent()->Layout();
}

void AutofillDialogViews::OverlayView::BeginFadeOut() {
  Parts parts;
  // For this part of the animation, simply show the splash image.
  parts.push_back(Part(kSplashDisplayDurationMs, ui::Tween::ZERO));
  // For this part of the animation, fade out the splash image.
  parts.push_back(Part(kSplashFadeOutDurationMs, ui::Tween::EASE_IN));
  // For this part of the animation, fade out |this| (fade in the dialog).
  parts.push_back(Part(kSplashFadeInDialogDurationMs, ui::Tween::EASE_OUT));
  fade_out_.reset(
      new ui::MultiAnimation(parts,
                             ui::MultiAnimation::GetDefaultTimerInterval()));
  fade_out_->set_delegate(this);
  fade_out_->set_continuous(false);
  fade_out_->Start();
}

void AutofillDialogViews::OverlayView::AnimationProgressed(
    const ui::Animation* animation) {
  DCHECK_EQ(animation, fade_out_.get());
  if (fade_out_->current_part_index() != 0)
    SchedulePaint();
}

void AutofillDialogViews::OverlayView::AnimationEnded(
    const ui::Animation* animation) {
  DCHECK_EQ(animation, fade_out_.get());
  SetVisible(false);
  fade_out_.reset();
}

gfx::Insets AutofillDialogViews::OverlayView::GetInsets() const {
  return gfx::Insets(12, 12, 12, 12);
}

void AutofillDialogViews::OverlayView::Layout() {
  gfx::Rect bounds = ContentBoundsSansBubbleBorder();
  if (!message_stack_->visible()) {
    image_view_->SetBoundsRect(bounds);
    return;
  }

  int y = bounds.bottom() - views::kButtonVEdgeMarginNew;
  if (button_->visible()) {
    button_->SizeToPreferredSize();
    y -= button_->height();
    button_->SetPosition(gfx::Point(
        bounds.CenterPoint().x() - button_->width() / 2, y));
    y -= views::kButtonVEdgeMarginNew;
  }

  int message_height = message_stack_->GetHeightForWidth(bounds.width());
  y -= message_height;
  message_stack_->SetBounds(bounds.x(), y, bounds.width(), message_height);

  gfx::Size image_size = image_view_->GetPreferredSize();
  y -= image_size.height() + kOverlayImageBottomMargin;
  image_view_->SetBounds(bounds.x(), y, bounds.width(), image_size.height());
}

const char* AutofillDialogViews::OverlayView::GetClassName() const {
  return kOverlayViewClassName;
}

void AutofillDialogViews::OverlayView::OnPaint(gfx::Canvas* canvas) {
  // BubbleFrameView doesn't mask the window, it just draws the border via
  // image assets. Match that rounding here.
  gfx::Rect rect = ContentBoundsSansBubbleBorder();
  const SkScalar kCornerRadius = SkIntToScalar(
      GetBubbleBorder() ? GetBubbleBorder()->GetBorderCornerRadius() : 2);
  gfx::Path window_mask;
  window_mask.addRoundRect(gfx::RectToSkRect(rect),
                           kCornerRadius, kCornerRadius);
  canvas->ClipPath(window_mask);

  // Fade out background (i.e. fade in what's behind |this|).
  if (fade_out_ && fade_out_->current_part_index() == 2)
    canvas->SaveLayerAlpha((1 - fade_out_->GetCurrentValue()) * 255);

  OnPaintBackground(canvas);

  // Draw the arrow, border, and fill for the bottom area.
  if (message_stack_->visible()) {
    const int arrow_half_width = kArrowWidth / 2.0f;
    SkPath arrow;
    int y = message_stack_->y() - 1;
    // Note that we purposely draw slightly outside of |rect| so that the
    // stroke is hidden on the sides.
    arrow.moveTo(rect.x() - 1, y);
    arrow.rLineTo(rect.width() / 2 - arrow_half_width, 0);
    arrow.rLineTo(arrow_half_width, -kArrowHeight);
    arrow.rLineTo(arrow_half_width, kArrowHeight);
    arrow.lineTo(rect.right() + 1, y);
    arrow.lineTo(rect.right() + 1, rect.bottom() + 1);
    arrow.lineTo(rect.x() - 1, rect.bottom() + 1);
    arrow.close();

    SkPaint paint;
    paint.setColor(kShadingColor);
    paint.setStyle(SkPaint::kFill_Style);
    canvas->DrawPath(arrow, paint);
    paint.setColor(kSubtleBorderColor);
    paint.setStyle(SkPaint::kStroke_Style);
    canvas->DrawPath(arrow, paint);
  }

  PaintChildren(canvas);
}

void AutofillDialogViews::OverlayView::PaintChildren(gfx::Canvas* canvas) {
  // Don't draw children.
  if (fade_out_ && fade_out_->current_part_index() == 2)
    return;

  // Fade out children.
  if (fade_out_ && fade_out_->current_part_index() == 1)
    canvas->SaveLayerAlpha((1 - fade_out_->GetCurrentValue()) * 255);

  views::View::PaintChildren(canvas);
}

views::BubbleBorder* AutofillDialogViews::OverlayView::GetBubbleBorder() {
  views::View* frame = GetWidget()->non_client_view()->frame_view();
  std::string bubble_frame_view_name(views::BubbleFrameView::kViewClassName);
  if (frame->GetClassName() == bubble_frame_view_name)
    return static_cast<views::BubbleFrameView*>(frame)->bubble_border();
  NOTREACHED();
  return NULL;
}

gfx::Rect AutofillDialogViews::OverlayView::ContentBoundsSansBubbleBorder() {
  gfx::Rect bounds = GetContentsBounds();
  int bubble_width = 5;
  if (GetBubbleBorder())
    bubble_width = GetBubbleBorder()->GetBorderThickness();
  bounds.Inset(bubble_width, bubble_width, bubble_width, bubble_width);
  return bounds;
}

// AutofillDialogViews::NotificationArea ---------------------------------------

AutofillDialogViews::NotificationArea::NotificationArea(
    AutofillDialogViewDelegate* delegate)
    : delegate_(delegate),
      checkbox_(NULL) {
  // Reserve vertical space for the arrow (regardless of whether one exists).
  // The -1 accounts for the border.
  set_border(views::Border::CreateEmptyBorder(kArrowHeight - 1, 0, 0, 0));

  views::BoxLayout* box_layout =
      new views::BoxLayout(views::BoxLayout::kVertical, 0, 0, 0);
  SetLayoutManager(box_layout);
}

AutofillDialogViews::NotificationArea::~NotificationArea() {}

void AutofillDialogViews::NotificationArea::SetNotifications(
    const std::vector<DialogNotification>& notifications) {
  notifications_ = notifications;

  RemoveAllChildViews(true);
  checkbox_ = NULL;

  if (notifications_.empty())
    return;

  for (size_t i = 0; i < notifications_.size(); ++i) {
    const DialogNotification& notification = notifications_[i];
    scoped_ptr<NotificationView> view(new NotificationView(notification));
    if (view->checkbox())
      checkbox_ = view->checkbox();

    AddChildView(view.release());
  }

  if (checkbox_)
    checkbox_->set_listener(this);

  PreferredSizeChanged();
}

gfx::Size AutofillDialogViews::NotificationArea::GetPreferredSize() {
  gfx::Size size = views::View::GetPreferredSize();
  // Ensure that long notifications wrap and don't enlarge the dialog.
  size.set_width(1);
  return size;
}

const char* AutofillDialogViews::NotificationArea::GetClassName() const {
  return kNotificationAreaClassName;
}

void AutofillDialogViews::NotificationArea::PaintChildren(
    gfx::Canvas* canvas) {}

void AutofillDialogViews::NotificationArea::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  views::View::PaintChildren(canvas);

  if (HasArrow()) {
    DrawArrow(
        canvas,
        GetMirroredXInView(width() - arrow_centering_anchor_->width() / 2.0f),
        notifications_[0].GetBackgroundColor(),
        notifications_[0].GetBorderColor());
  }
}

void AutofillDialogViews::OnWidgetClosing(views::Widget* widget) {
  observer_.Remove(widget);
}

void AutofillDialogViews::OnWidgetBoundsChanged(views::Widget* widget,
                                                const gfx::Rect& new_bounds) {
  ContentsPreferredSizeChanged();
}

void AutofillDialogViews::NotificationArea::ButtonPressed(
    views::Button* sender, const ui::Event& event) {
  DCHECK_EQ(sender, checkbox_);
  delegate_->NotificationCheckboxStateChanged(notifications_.front().type(),
                                                checkbox_->checked());
}

bool AutofillDialogViews::NotificationArea::HasArrow() {
  return !notifications_.empty() && notifications_[0].HasArrow() &&
      arrow_centering_anchor_.get();
}

// AutofillDialogViews::SectionContainer ---------------------------------------

AutofillDialogViews::SectionContainer::SectionContainer(
    const base::string16& label,
    views::View* controls,
    views::Button* proxy_button)
    : proxy_button_(proxy_button),
      forward_mouse_events_(false) {
  set_notify_enter_exit_on_child(true);

  set_border(views::Border::CreateEmptyBorder(kDetailSectionVerticalPadding,
                                              kDialogEdgePadding,
                                              kDetailSectionVerticalPadding,
                                              kDialogEdgePadding));

  // TODO(estade): this label should be semi-bold.
  views::Label* label_view = new views::Label(label);
  label_view->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  views::View* label_bar = new views::View();
  views::GridLayout* label_bar_layout = new views::GridLayout(label_bar);
  label_bar->SetLayoutManager(label_bar_layout);
  const int kColumnSetId = 0;
  views::ColumnSet* columns = label_bar_layout->AddColumnSet(kColumnSetId);
  // TODO(estade): do something about this '480'.
  columns->AddColumn(views::GridLayout::LEADING,
                     views::GridLayout::LEADING,
                     0,
                     views::GridLayout::FIXED,
                     480,
                     0);
  columns->AddColumn(views::GridLayout::LEADING,
                     views::GridLayout::LEADING,
                     0,
                     views::GridLayout::USE_PREF,
                     0,
                     0);
  label_bar_layout->StartRow(0, kColumnSetId);
  label_bar_layout->AddView(label_view);
  label_bar_layout->AddView(proxy_button);

  SetLayoutManager(new views::BoxLayout(views::BoxLayout::kVertical, 0, 0, 0));
  AddChildView(label_bar);
  AddChildView(controls);
}

AutofillDialogViews::SectionContainer::~SectionContainer() {}

void AutofillDialogViews::SectionContainer::SetActive(bool active) {
  bool is_active = active && proxy_button_->visible();
  if (is_active == !!background())
    return;

  set_background(is_active ?
      views::Background::CreateSolidBackground(kShadingColor) :
      NULL);
  SchedulePaint();
}

void AutofillDialogViews::SectionContainer::SetForwardMouseEvents(
    bool forward) {
  forward_mouse_events_ = forward;
  if (!forward)
    set_background(NULL);
}

void AutofillDialogViews::SectionContainer::OnMouseMoved(
    const ui::MouseEvent& event) {
  if (!forward_mouse_events_)
    return;

  SetActive(true);
}

void AutofillDialogViews::SectionContainer::OnMouseEntered(
    const ui::MouseEvent& event) {
  if (!forward_mouse_events_)
    return;

  SetActive(true);
  proxy_button_->OnMouseEntered(ProxyEvent(event));
  SchedulePaint();
}

void AutofillDialogViews::SectionContainer::OnMouseExited(
    const ui::MouseEvent& event) {
  if (!forward_mouse_events_)
    return;

  SetActive(false);
  proxy_button_->OnMouseExited(ProxyEvent(event));
  SchedulePaint();
}

bool AutofillDialogViews::SectionContainer::OnMousePressed(
    const ui::MouseEvent& event) {
  if (!forward_mouse_events_)
    return false;

  return proxy_button_->OnMousePressed(ProxyEvent(event));
}

void AutofillDialogViews::SectionContainer::OnMouseReleased(
    const ui::MouseEvent& event) {
  if (!forward_mouse_events_)
    return;

  proxy_button_->OnMouseReleased(ProxyEvent(event));
}

// static
ui::MouseEvent AutofillDialogViews::SectionContainer::ProxyEvent(
    const ui::MouseEvent& event) {
  ui::MouseEvent event_copy = event;
  event_copy.set_location(gfx::Point());
  return event_copy;
}

// AutofilDialogViews::SuggestionView ------------------------------------------

AutofillDialogViews::SuggestionView::SuggestionView(
    AutofillDialogViews* autofill_dialog)
    : label_(new views::Label()),
      label_line_2_(new views::Label()),
      icon_(new views::ImageView()),
      decorated_(
          new DecoratedTextfield(base::string16(),
                                 base::string16(),
                                 autofill_dialog)) {
  // TODO(estade): Make this the correct color.
  set_border(
      views::Border::CreateSolidSidedBorder(1, 0, 0, 0, SK_ColorLTGRAY));

  SectionRowView* label_container = new SectionRowView();
  AddChildView(label_container);

  // Label and icon.
  label_container->AddChildView(icon_);
  label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label_container->AddChildView(label_);

  // TODO(estade): get the sizing and spacing right on this textfield.
  decorated_->SetVisible(false);
  decorated_->set_default_width_in_chars(10);
  label_container->AddChildView(decorated_);

  // TODO(estade): need to get the line height right.
  label_line_2_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label_line_2_->SetVisible(false);
  label_line_2_->SetMultiLine(true);
  AddChildView(label_line_2_);

  // TODO(estade): do something about this '2'.
  SetLayoutManager(new views::BoxLayout(views::BoxLayout::kVertical, 0, 2, 0));
}

AutofillDialogViews::SuggestionView::~SuggestionView() {}

gfx::Size AutofillDialogViews::SuggestionView::GetPreferredSize() {
  // There's no preferred width. The parent's layout should get the preferred
  // height from GetHeightForWidth().
  return gfx::Size();
}

int AutofillDialogViews::SuggestionView::GetHeightForWidth(int width) {
  int height = 0;
  CanUseVerticallyCompactText(width, &height);
  return height;
}

bool AutofillDialogViews::SuggestionView::CanUseVerticallyCompactText(
    int available_width,
    int* resulting_height) {
  // This calculation may be costly, avoid doing it more than once per width.
  if (!calculated_heights_.count(available_width)) {
    // Changing the state of |this| now will lead to extra layouts and
    // paints we don't want, so create another SuggestionView to calculate
    // which label we have room to show.
    SuggestionView sizing_view(NULL);
    sizing_view.SetLabelText(state_.vertically_compact_text);
    sizing_view.SetIcon(state_.icon);
    sizing_view.SetTextfield(state_.extra_text, state_.extra_icon);

    // Shortcut |sizing_view|'s GetHeightForWidth() to avoid an infinite loop.
    // Its BoxLayout must do these calculations for us.
    views::LayoutManager* layout = sizing_view.GetLayoutManager();
    if (layout->GetPreferredSize(&sizing_view).width() <= available_width) {
      calculated_heights_[available_width] = std::make_pair(
          true,
          layout->GetPreferredHeightForWidth(&sizing_view, available_width));
    } else {
      sizing_view.SetLabelText(state_.horizontally_compact_text);
      calculated_heights_[available_width] = std::make_pair(
          false,
          layout->GetPreferredHeightForWidth(&sizing_view, available_width));
    }
  }

  const std::pair<bool, int>& values = calculated_heights_[available_width];
  *resulting_height = values.second;
  return values.first;
}

void AutofillDialogViews::SuggestionView::OnBoundsChanged(
    const gfx::Rect& previous_bounds) {
  int unused;
  SetLabelText(CanUseVerticallyCompactText(width(), &unused) ?
      state_.vertically_compact_text :
      state_.horizontally_compact_text);
}

void AutofillDialogViews::SuggestionView::SetState(
    const SuggestionState& state) {
  calculated_heights_.clear();
  state_ = state;
  SetVisible(state_.visible);
  // Set to the more compact text for now. |this| will optionally switch to
  // the more vertically expanded view when the bounds are set.
  SetLabelText(state_.vertically_compact_text);
  SetIcon(state_.icon);
  SetTextfield(state_.extra_text, state_.extra_icon);
  PreferredSizeChanged();
}

void AutofillDialogViews::SuggestionView::SetLabelText(
    const base::string16& text) {
  // TODO(estade): does this localize well?
  base::string16 line_return(ASCIIToUTF16("\n"));
  size_t position = text.find(line_return);
  if (position == base::string16::npos) {
    label_->SetText(text);
    label_line_2_->SetVisible(false);
  } else {
    label_->SetText(text.substr(0, position));
    label_line_2_->SetText(text.substr(position + line_return.length()));
    label_line_2_->SetVisible(true);
  }
}

void AutofillDialogViews::SuggestionView::SetIcon(
    const gfx::Image& image) {
  icon_->SetVisible(!image.IsEmpty());
  icon_->SetImage(image.AsImageSkia());
}

void AutofillDialogViews::SuggestionView::SetTextfield(
    const base::string16& placeholder_text,
    const gfx::Image& icon) {
  decorated_->set_placeholder_text(placeholder_text);
  decorated_->SetIcon(icon);
  decorated_->SetVisible(!placeholder_text.empty());
}

// AutofillDialogViews::AutocheckoutStepsArea ---------------------------------

AutofillDialogViews::AutocheckoutStepsArea::AutocheckoutStepsArea() {
  SetLayoutManager(new views::BoxLayout(views::BoxLayout::kVertical,
                                        kAutocheckoutStepsAreaPadding,
                                        0,
                                        kAutocheckoutStepInset));
}

void AutofillDialogViews::AutocheckoutStepsArea::SetSteps(
    const std::vector<DialogAutocheckoutStep>& steps) {
  RemoveAllChildViews(true);
  for (size_t i = 0; i < steps.size(); ++i) {
    const DialogAutocheckoutStep& step = steps[i];
    AutocheckoutStepProgressView* progressView =
        new AutocheckoutStepProgressView(step.GetDisplayText(),
                                         step.GetTextFont(),
                                         step.GetTextColor(),
                                         step.IsIconVisible());

    AddChildView(progressView);
  }

  PreferredSizeChanged();
}

// AutofillDialogViews::AutocheckoutProgressBar

AutofillDialogViews::AutocheckoutProgressBar::AutocheckoutProgressBar() {}
AutofillDialogViews::AutocheckoutProgressBar::~AutocheckoutProgressBar() {}

gfx::Size AutofillDialogViews::AutocheckoutProgressBar::GetPreferredSize() {
  return gfx::Size(kAutocheckoutProgressBarWidth,
                   kAutocheckoutProgressBarHeight);
}

// AutofillDialogView ----------------------------------------------------------

// static
AutofillDialogView* AutofillDialogView::Create(
    AutofillDialogViewDelegate* delegate) {
  return new AutofillDialogViews(delegate);
}

// AutofillDialogViews ---------------------------------------------------------

AutofillDialogViews::AutofillDialogViews(AutofillDialogViewDelegate* delegate)
    : delegate_(delegate),
      window_(NULL),
      notification_area_(NULL),
      account_chooser_(NULL),
      sign_in_webview_(NULL),
      scrollable_area_(NULL),
      details_container_(NULL),
      loading_shield_(NULL),
      overlay_view_(NULL),
      button_strip_extra_view_(NULL),
      save_in_chrome_checkbox_(NULL),
      save_in_chrome_checkbox_container_(NULL),
      button_strip_image_(NULL),
      autocheckout_steps_area_(NULL),
      autocheckout_progress_bar_view_(NULL),
      autocheckout_progress_bar_(NULL),
      footnote_view_(NULL),
      legal_document_view_(NULL),
      focus_manager_(NULL),
      observer_(this) {
  DCHECK(delegate);
  detail_groups_.insert(std::make_pair(SECTION_EMAIL,
                                       DetailsGroup(SECTION_EMAIL)));
  detail_groups_.insert(std::make_pair(SECTION_CC,
                                       DetailsGroup(SECTION_CC)));
  detail_groups_.insert(std::make_pair(SECTION_BILLING,
                                       DetailsGroup(SECTION_BILLING)));
  detail_groups_.insert(std::make_pair(SECTION_CC_BILLING,
                                       DetailsGroup(SECTION_CC_BILLING)));
  detail_groups_.insert(std::make_pair(SECTION_SHIPPING,
                                       DetailsGroup(SECTION_SHIPPING)));
}

AutofillDialogViews::~AutofillDialogViews() {
  DCHECK(!window_);
}

void AutofillDialogViews::Show() {
  InitChildViews();
  UpdateAccountChooser();
  UpdateNotificationArea();
  UpdateButtonStripExtraView();

  // Ownership of |contents_| is handed off by this call. The widget will take
  // care of deleting itself after calling DeleteDelegate().
  WebContentsModalDialogManager* web_contents_modal_dialog_manager =
      WebContentsModalDialogManager::FromWebContents(
          delegate_->GetWebContents());
  window_ = CreateWebContentsModalDialogViews(
      this,
      delegate_->GetWebContents()->GetView()->GetNativeView(),
      web_contents_modal_dialog_manager->delegate()->
          GetWebContentsModalDialogHost());
  web_contents_modal_dialog_manager->ShowDialog(window_->GetNativeView());
  focus_manager_ = window_->GetFocusManager();
  focus_manager_->AddFocusChangeListener(this);

  // Listen for size changes on the browser.
  views::Widget* browser_widget =
      views::Widget::GetTopLevelWidgetForNativeView(
          delegate_->GetWebContents()->GetView()->GetNativeView());
  observer_.Add(browser_widget);

  gfx::Image splash_image = delegate_->SplashPageImage();
  if (!splash_image.IsEmpty()) {
    DialogOverlayState state;
    state.image = splash_image;
    overlay_view_->SetState(state, NULL);
    overlay_view_->BeginFadeOut();
  }
}

void AutofillDialogViews::Hide() {
  if (window_)
    window_->Close();
}

void AutofillDialogViews::UpdateAccountChooser() {
  account_chooser_->Update();
  // TODO(estade): replace this with a better loading image/animation.
  // See http://crbug.com/230932
  base::string16 new_loading_message = (delegate_->ShouldShowSpinner() ?
      ASCIIToUTF16("Loading...") : base::string16());
  if (new_loading_message != loading_shield_->text()) {
    loading_shield_->SetText(new_loading_message);
    loading_shield_->SetVisible(!new_loading_message.empty());
    Layout();
  }

  // Update legal documents for the account.
  if (footnote_view_) {
    const base::string16 text = delegate_->LegalDocumentsText();
    legal_document_view_->SetText(text);

    if (!text.empty()) {
      const std::vector<ui::Range>& link_ranges =
          delegate_->LegalDocumentLinks();
      for (size_t i = 0; i < link_ranges.size(); ++i) {
        legal_document_view_->AddStyleRange(
            link_ranges[i],
            views::StyledLabel::RangeStyleInfo::CreateForLink());
      }
    }

    footnote_view_->SetVisible(!text.empty());
    ContentsPreferredSizeChanged();
  }
}

void AutofillDialogViews::UpdateAutocheckoutStepsArea() {
  autocheckout_steps_area_->SetSteps(delegate_->CurrentAutocheckoutSteps());
  ContentsPreferredSizeChanged();
}

void AutofillDialogViews::UpdateButtonStrip() {
  button_strip_extra_view_->SetVisible(
      GetDialogButtons() != ui::DIALOG_BUTTON_NONE);
  UpdateButtonStripExtraView();
  GetDialogClientView()->UpdateDialogButtons();

  overlay_view_->SetState(delegate_->GetDialogOverlay(), this);

  ContentsPreferredSizeChanged();
}

void AutofillDialogViews::UpdateDetailArea() {
  scrollable_area_->SetVisible(delegate_->ShouldShowDetailArea());
  ContentsPreferredSizeChanged();
}

void AutofillDialogViews::UpdateForErrors() {
  ValidateForm();
}

void AutofillDialogViews::UpdateNotificationArea() {
  DCHECK(notification_area_);
  notification_area_->SetNotifications(delegate_->CurrentNotifications());
  ContentsPreferredSizeChanged();
}

void AutofillDialogViews::UpdateSection(DialogSection section) {
  UpdateSectionImpl(section, true);
}

void AutofillDialogViews::FillSection(DialogSection section,
                                      const DetailInput& originating_input) {
  DetailsGroup* group = GroupForSection(section);
  // Make sure to overwrite the originating input.
  TextfieldMap::iterator text_mapping =
      group->textfields.find(&originating_input);
  if (text_mapping != group->textfields.end())
    text_mapping->second->SetText(base::string16());

  // If the Autofill data comes from a credit card, make sure to overwrite the
  // CC comboboxes (even if they already have something in them). If the
  // Autofill data comes from an AutofillProfile, leave the comboboxes alone.
  if ((section == SECTION_CC || section == SECTION_CC_BILLING) &&
      AutofillType(originating_input.type).group() == CREDIT_CARD) {
    for (ComboboxMap::const_iterator it = group->comboboxes.begin();
         it != group->comboboxes.end(); ++it) {
      if (AutofillType(it->first->type).group() == CREDIT_CARD)
        it->second->SetSelectedIndex(it->second->model()->GetDefaultIndex());
    }
  }

  UpdateSectionImpl(section, false);
}

void AutofillDialogViews::GetUserInput(DialogSection section,
                                       DetailOutputMap* output) {
  DetailsGroup* group = GroupForSection(section);
  for (TextfieldMap::const_iterator it = group->textfields.begin();
       it != group->textfields.end(); ++it) {
    output->insert(std::make_pair(it->first, it->second->text()));
  }
  for (ComboboxMap::const_iterator it = group->comboboxes.begin();
       it != group->comboboxes.end(); ++it) {
    output->insert(std::make_pair(it->first,
        it->second->model()->GetItemAt(it->second->selected_index())));
  }
}

base::string16 AutofillDialogViews::GetCvc() {
  DialogSection billing_section = delegate_->SectionIsActive(SECTION_CC) ?
      SECTION_CC : SECTION_CC_BILLING;
  return GroupForSection(billing_section)->suggested_info->
      decorated_textfield()->text();
}

bool AutofillDialogViews::SaveDetailsLocally() {
  DCHECK(save_in_chrome_checkbox_->visible());
  return save_in_chrome_checkbox_->checked();
}

const content::NavigationController* AutofillDialogViews::ShowSignIn() {
  // TODO(abodenha) We should be able to use the WebContents of the WebView
  // to navigate instead of LoadInitialURL.  Figure out why it doesn't work.

  sign_in_webview_->LoadInitialURL(wallet::GetSignInUrl());

  sign_in_webview_->SetVisible(true);
  sign_in_webview_->RequestFocus();
  UpdateButtonStrip();
  ContentsPreferredSizeChanged();
  return &sign_in_webview_->web_contents()->GetController();
}

void AutofillDialogViews::HideSignIn() {
  sign_in_webview_->SetVisible(false);
  UpdateButtonStrip();
  ContentsPreferredSizeChanged();
}

void AutofillDialogViews::UpdateProgressBar(double value) {
  autocheckout_progress_bar_->SetValue(value);
}

void AutofillDialogViews::ModelChanged() {
  menu_runner_.reset();

  for (DetailGroupMap::const_iterator iter = detail_groups_.begin();
       iter != detail_groups_.end(); ++iter) {
    UpdateDetailsGroupState(iter->second);
  }
}

TestableAutofillDialogView* AutofillDialogViews::GetTestableView() {
  return this;
}

void AutofillDialogViews::OnSignInResize(const gfx::Size& pref_size) {
  sign_in_webview_->SetPreferredSize(pref_size);
  ContentsPreferredSizeChanged();
}

void AutofillDialogViews::SubmitForTesting() {
  Accept();
}

void AutofillDialogViews::CancelForTesting() {
  GetDialogClientView()->CancelWindow();
}

base::string16 AutofillDialogViews::GetTextContentsOfInput(
    const DetailInput& input) {
  views::Textfield* textfield = TextfieldForInput(input);
  if (textfield)
    return textfield->text();

  views::Combobox* combobox = ComboboxForInput(input);
  if (combobox)
    return combobox->model()->GetItemAt(combobox->selected_index());

  NOTREACHED();
  return base::string16();
}

void AutofillDialogViews::SetTextContentsOfInput(
    const DetailInput& input,
    const base::string16& contents) {
  views::Textfield* textfield = TextfieldForInput(input);
  if (textfield) {
    TextfieldForInput(input)->SetText(contents);
    return;
  }

  views::Combobox* combobox = ComboboxForInput(input);
  if (combobox) {
    for (int i = 0; i < combobox->model()->GetItemCount(); ++i) {
      if (contents == combobox->model()->GetItemAt(i)) {
        combobox->SetSelectedIndex(i);
        return;
      }
    }
    // If we don't find a match, return the combobox to its default state.
    combobox->SetSelectedIndex(combobox->model()->GetDefaultIndex());
    return;
  }

  NOTREACHED();
}

void AutofillDialogViews::SetTextContentsOfSuggestionInput(
    DialogSection section,
    const base::string16& text) {
  GroupForSection(section)->suggested_info->decorated_textfield()->
      SetText(text);
}

void AutofillDialogViews::ActivateInput(const DetailInput& input) {
  TextfieldEditedOrActivated(TextfieldForInput(input), false);
}

gfx::Size AutofillDialogViews::GetSize() const {
  return GetWidget() ? GetWidget()->GetRootView()->size() : gfx::Size();
}

gfx::Size AutofillDialogViews::GetPreferredSize() {
  gfx::Insets insets = GetInsets();
  gfx::Size scroll_size = scrollable_area_->contents()->GetPreferredSize();
  int width = scroll_size.width() + insets.width();

  if (sign_in_webview_->visible()) {
    gfx::Size size = static_cast<views::View*>(sign_in_webview_)->
        GetPreferredSize();
    return gfx::Size(width, size.height() + insets.height());
  }

  int base_height = insets.height();
  int notification_height = notification_area_->
      GetHeightForWidth(scroll_size.width());
  if (notification_height > 0)
    base_height += notification_height + views::kRelatedControlVerticalSpacing;

  int steps_height = autocheckout_steps_area_->
      GetHeightForWidth(scroll_size.width());
  if (steps_height > 0)
    base_height += steps_height + views::kRelatedControlVerticalSpacing;

  gfx::Size preferred_size;
  // When the scroll area isn't visible, it still sets the width but doesn't
  // factor into height.
  if (!scrollable_area_->visible()) {
    preferred_size.SetSize(width, base_height);
  } else {
    // Show as much of the scroll view as is possible without going past the
    // bottom of the browser window.
    views::Widget* widget =
        views::Widget::GetTopLevelWidgetForNativeView(
            delegate_->GetWebContents()->GetView()->GetNativeView());
    int browser_window_height =
        widget ? widget->GetContentsView()->bounds().height() : INT_MAX;
    const int kWindowDecorationHeight = 200;
    int height = base_height + std::min(
        scroll_size.height(),
        std::max(kMinimumContentsHeight,
                 browser_window_height - base_height -
                     kWindowDecorationHeight));
    preferred_size.SetSize(width, height);
  }

  if (!overlay_view_->visible())
    return preferred_size;

  int height_of_overlay =
      overlay_view_->GetHeightForContentsForWidth(preferred_size.width());
  if (height_of_overlay > 0)
    preferred_size.set_height(height_of_overlay);

  return preferred_size;
}

void AutofillDialogViews::Layout() {
  gfx::Rect content_bounds = GetContentsBounds();
  if (sign_in_webview_->visible()) {
    sign_in_webview_->SetBoundsRect(content_bounds);
    return;
  }

  const int x = content_bounds.x();
  const int y = content_bounds.y();
  const int width = content_bounds.width();
  // Layout notification area at top of dialog.
  int notification_height = notification_area_->GetHeightForWidth(width);
  notification_area_->SetBounds(x, y, width, notification_height);

  // Layout Autocheckout steps at bottom of dialog.
  int steps_height = autocheckout_steps_area_->GetHeightForWidth(width);
  autocheckout_steps_area_->SetBounds(x, content_bounds.bottom() - steps_height,
                                      width, steps_height);

  // The rest (the |scrollable_area_|) takes up whatever's left.
  if (scrollable_area_->visible()) {
    int scroll_y = y;
    if (notification_height > 0)
      scroll_y += notification_height + views::kRelatedControlVerticalSpacing;

    int scroll_bottom = content_bounds.bottom();
    if (steps_height > 0)
      scroll_bottom -= steps_height + views::kRelatedControlVerticalSpacing;

    scrollable_area_->contents()->SizeToPreferredSize();
    scrollable_area_->SetBounds(x, scroll_y, width, scroll_bottom - scroll_y);
  }

  if (loading_shield_->visible())
    loading_shield_->SetBoundsRect(bounds());

  if (error_bubble_)
    error_bubble_->UpdatePosition();
}

void AutofillDialogViews::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  sign_in_delegate_->SetMinWidth(GetContentsBounds().width());
}

base::string16 AutofillDialogViews::GetWindowTitle() const {
  return delegate_->DialogTitle();
}

void AutofillDialogViews::WindowClosing() {
  focus_manager_->RemoveFocusChangeListener(this);
}

void AutofillDialogViews::DeleteDelegate() {
  window_ = NULL;
  // |this| belongs to the controller (|delegate_|).
  delegate_->ViewClosed();
}

int AutofillDialogViews::GetDialogButtons() const {
  if (sign_in_webview_->visible())
    return ui::DIALOG_BUTTON_NONE;

  return delegate_->GetDialogButtons();
}

base::string16 AutofillDialogViews::GetDialogButtonLabel(
    ui::DialogButton button) const {
  return button == ui::DIALOG_BUTTON_OK ?
      delegate_->ConfirmButtonText() : delegate_->CancelButtonText();
}

bool AutofillDialogViews::ShouldDefaultButtonBeBlue() const {
  return true;
}

bool AutofillDialogViews::IsDialogButtonEnabled(ui::DialogButton button) const {
  return delegate_->IsDialogButtonEnabled(button);
}

views::View* AutofillDialogViews::CreateExtraView() {
  return button_strip_extra_view_;
}

views::View* AutofillDialogViews::CreateTitlebarExtraView() {
  return account_chooser_;
}

views::View* AutofillDialogViews::CreateFootnoteView() {
  footnote_view_ = new LayoutPropagationView();
  footnote_view_->SetLayoutManager(
      new views::BoxLayout(views::BoxLayout::kVertical,
                           kDialogEdgePadding,
                           kDialogEdgePadding,
                           0));
  footnote_view_->set_border(
      views::Border::CreateSolidSidedBorder(1, 0, 0, 0, kSubtleBorderColor));
  footnote_view_->set_background(
      views::Background::CreateSolidBackground(kShadingColor));

  legal_document_view_ = new views::StyledLabel(base::string16(), this);
  views::StyledLabel::RangeStyleInfo default_style;
  default_style.color = kGreyTextColor;
  legal_document_view_->SetDefaultStyle(default_style);

  footnote_view_->AddChildView(legal_document_view_);
  footnote_view_->SetVisible(false);

  return footnote_view_;
}

views::View* AutofillDialogViews::CreateOverlayView() {
  return overlay_view_;
}

bool AutofillDialogViews::Cancel() {
  return delegate_->OnCancel();
}

bool AutofillDialogViews::Accept() {
  if (ValidateForm())
    return delegate_->OnAccept();

  if (!validity_map_.empty())
    validity_map_.begin()->first->RequestFocus();
  return false;
}

// TODO(wittman): Remove this override once we move to the new style frame view
// on all dialogs.
views::NonClientFrameView* AutofillDialogViews::CreateNonClientFrameView(
    views::Widget* widget) {
  return CreateConstrainedStyleNonClientFrameView(
      widget,
      delegate_->GetWebContents()->GetBrowserContext());
}

void AutofillDialogViews::ButtonPressed(views::Button* sender,
                                        const ui::Event& event) {
  if (sender->GetAncestorWithClassName(kOverlayViewClassName)) {
    delegate_->OverlayButtonPressed();
    return;
  }

  // TODO(estade): Should the menu be shown on mouse down?
  DetailsGroup* group = NULL;
  for (DetailGroupMap::iterator iter = detail_groups_.begin();
       iter != detail_groups_.end(); ++iter) {
    if (sender == iter->second.suggested_button) {
      group = &iter->second;
      break;
    }
  }
  DCHECK(group);

  if (!group->suggested_button->visible())
    return;

  menu_runner_.reset(new views::MenuRunner(
                         delegate_->MenuModelForSection(group->section)));

  group->container->SetActive(true);
  views::Button::ButtonState state = group->suggested_button->state();
  group->suggested_button->SetState(views::Button::STATE_PRESSED);
  // Ignore the result since we don't need to handle a deleted menu specially.
  gfx::Rect bounds = group->suggested_button->GetBoundsInScreen();
  bounds.Inset(group->suggested_button->GetInsets());
  ignore_result(
      menu_runner_->RunMenuAt(sender->GetWidget(),
                              NULL,
                              bounds,
                              views::MenuItemView::TOPRIGHT,
                              ui::GetMenuSourceTypeForEvent(event),
                              0));
  group->container->SetActive(false);
  group->suggested_button->SetState(state);
}

void AutofillDialogViews::ContentsChanged(views::Textfield* sender,
                                          const base::string16& new_contents) {
  TextfieldEditedOrActivated(sender, true);
}

bool AutofillDialogViews::HandleKeyEvent(views::Textfield* sender,
                                         const ui::KeyEvent& key_event) {
  scoped_ptr<ui::KeyEvent> copy(key_event.Copy());
#if defined(OS_WIN) && !defined(USE_AURA)
  content::NativeWebKeyboardEvent event(copy->native_event());
#else
  content::NativeWebKeyboardEvent event(copy.get());
#endif
  return delegate_->HandleKeyPressEventInInput(event);
}

bool AutofillDialogViews::HandleMouseEvent(views::Textfield* sender,
                                           const ui::MouseEvent& mouse_event) {
  if (mouse_event.IsLeftMouseButton() && sender->HasFocus()) {
    TextfieldEditedOrActivated(sender, false);
    // Show an error bubble if a user clicks on an input that's already focused
    // (and invalid).
    ShowErrorBubbleForViewIfNecessary(sender);
  }

  return false;
}

void AutofillDialogViews::OnWillChangeFocus(
    views::View* focused_before,
    views::View* focused_now) {
  delegate_->FocusMoved();
  error_bubble_.reset();
}

void AutofillDialogViews::OnDidChangeFocus(
    views::View* focused_before,
    views::View* focused_now) {
  // If user leaves an edit-field, revalidate the group it belongs to.
  if (focused_before) {
    DetailsGroup* group = GroupForView(focused_before);
    if (group && group->container->visible())
      ValidateGroup(*group, VALIDATE_EDIT);
  }

  // Show an error bubble when the user focuses the input.
  if (focused_now) {
    focused_now->ScrollRectToVisible(focused_now->GetLocalBounds());
    ShowErrorBubbleForViewIfNecessary(focused_now);
  }
}

void AutofillDialogViews::OnSelectedIndexChanged(views::Combobox* combobox) {
  DetailsGroup* group = GroupForView(combobox);
  ValidateGroup(*group, VALIDATE_EDIT);
}

void AutofillDialogViews::StyledLabelLinkClicked(const ui::Range& range,
                                                 int event_flags) {
  delegate_->LegalDocumentLinkClicked(range);
}

void AutofillDialogViews::InitChildViews() {
  button_strip_extra_view_ = new LayoutPropagationView();
  button_strip_extra_view_->SetLayoutManager(
      new views::BoxLayout(views::BoxLayout::kHorizontal, 0, 0, 0));

  save_in_chrome_checkbox_container_ = new views::View();
  save_in_chrome_checkbox_container_->SetLayoutManager(
      new views::BoxLayout(views::BoxLayout::kHorizontal, 0, 0, 7));
  button_strip_extra_view_->AddChildView(save_in_chrome_checkbox_container_);

  save_in_chrome_checkbox_ =
      new views::Checkbox(delegate_->SaveLocallyText());
  save_in_chrome_checkbox_->SetChecked(true);
  save_in_chrome_checkbox_container_->AddChildView(save_in_chrome_checkbox_);

  save_in_chrome_checkbox_container_->AddChildView(
      new TooltipIcon(delegate_->SaveLocallyTooltip()));

  button_strip_image_ = new views::ImageView();
  button_strip_extra_view_->AddChildView(button_strip_image_);

  autocheckout_progress_bar_view_ = new views::View();
  views::GridLayout* progress_bar_layout =
      new views::GridLayout(autocheckout_progress_bar_view_);
  autocheckout_progress_bar_view_->SetLayoutManager(progress_bar_layout);
  const int kColumnSetId = 0;
  views::ColumnSet* columns = progress_bar_layout->AddColumnSet(kColumnSetId);
  columns->AddColumn(views::GridLayout::LEADING,
                     views::GridLayout::CENTER,
                     0,
                     views::GridLayout::USE_PREF,
                     0,
                     0);
  progress_bar_layout->StartRow(1.0, kColumnSetId);

  autocheckout_progress_bar_ = new AutocheckoutProgressBar();
  progress_bar_layout->AddView(autocheckout_progress_bar_);
  button_strip_extra_view_->AddChildView(autocheckout_progress_bar_view_);

  account_chooser_ = new AccountChooser(delegate_);
  notification_area_ = new NotificationArea(delegate_);
  notification_area_->set_arrow_centering_anchor(account_chooser_->AsWeakPtr());
  AddChildView(notification_area_);

  scrollable_area_ = new views::ScrollView();
  scrollable_area_->set_hide_horizontal_scrollbar(true);
  scrollable_area_->SetContents(CreateDetailsContainer());
  AddChildView(scrollable_area_);

  autocheckout_steps_area_ = new AutocheckoutStepsArea();
  AddChildView(autocheckout_steps_area_);

  loading_shield_ = new views::Label();
  loading_shield_->SetVisible(false);
  loading_shield_->set_background(views::Background::CreateSolidBackground(
      GetNativeTheme()->GetSystemColor(
          ui::NativeTheme::kColorId_DialogBackground)));
  loading_shield_->SetFont(ui::ResourceBundle::GetSharedInstance().GetFont(
      ui::ResourceBundle::BaseFont).DeriveFont(15));
  AddChildView(loading_shield_);

  sign_in_webview_ = new views::WebView(delegate_->profile());
  sign_in_webview_->SetVisible(false);
  AddChildView(sign_in_webview_);
  sign_in_delegate_.reset(
      new AutofillDialogSignInDelegate(this,
                                       sign_in_webview_->GetWebContents()));

  overlay_view_ = new OverlayView(this);
  overlay_view_->SetVisible(false);
}

views::View* AutofillDialogViews::CreateDetailsContainer() {
  details_container_ = new DetailsContainerView(
      base::Bind(&AutofillDialogViews::DetailsContainerBoundsChanged,
                 base::Unretained(this)));
  // A box layout is used because it respects widget visibility.
  details_container_->SetLayoutManager(
      new views::BoxLayout(views::BoxLayout::kVertical, 0, 0, 0));
  for (DetailGroupMap::iterator iter = detail_groups_.begin();
       iter != detail_groups_.end(); ++iter) {
    CreateDetailsSection(iter->second.section);
    details_container_->AddChildView(iter->second.container);
  }

  return details_container_;
}

void AutofillDialogViews::CreateDetailsSection(DialogSection section) {
  // Inputs container (manual inputs + combobox).
  views::View* inputs_container = CreateInputsContainer(section);

  DetailsGroup* group = GroupForSection(section);
  // Container (holds label + inputs).
  group->container = new SectionContainer(
      delegate_->LabelForSection(section),
      inputs_container,
      group->suggested_button);
  DCHECK(group->suggested_button->parent());
  UpdateDetailsGroupState(*group);
}

views::View* AutofillDialogViews::CreateInputsContainer(DialogSection section) {
  // The |info_view| holds |manual_inputs| and |suggested_info|, allowing the
  // dialog to toggle which is shown.
  views::View* info_view = new views::View();
  info_view->SetLayoutManager(
      new views::BoxLayout(views::BoxLayout::kVertical, 0, 0, 0));

  views::View* manual_inputs = InitInputsView(section);
  info_view->AddChildView(manual_inputs);
  SuggestionView* suggested_info = new SuggestionView(this);
  info_view->AddChildView(suggested_info);

  // TODO(estade): It might be slightly more OO if this button were created
  // and listened to by the section container.
  views::ImageButton* menu_button = new views::ImageButton(this);
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  menu_button->SetImage(views::Button::STATE_NORMAL,
      rb.GetImageSkiaNamed(IDR_AUTOFILL_DIALOG_MENU_BUTTON));
  menu_button->SetImage(views::Button::STATE_PRESSED,
      rb.GetImageSkiaNamed(IDR_AUTOFILL_DIALOG_MENU_BUTTON_P));
  menu_button->SetImage(views::Button::STATE_HOVERED,
      rb.GetImageSkiaNamed(IDR_AUTOFILL_DIALOG_MENU_BUTTON_H));
  menu_button->SetImage(views::Button::STATE_DISABLED,
      rb.GetImageSkiaNamed(IDR_AUTOFILL_DIALOG_MENU_BUTTON_D));
  menu_button->set_border(views::Border::CreateEmptyBorder(
      kMenuButtonTopInset,
      kDialogEdgePadding,
      kMenuButtonBottomInset,
      0));

  DetailsGroup* group = GroupForSection(section);
  group->suggested_button = menu_button;
  group->manual_input = manual_inputs;
  group->suggested_info = suggested_info;
  return info_view;
}

// TODO(estade): we should be using Chrome-style constrained window padding
// values.
views::View* AutofillDialogViews::InitInputsView(DialogSection section) {
  const DetailInputs& inputs = delegate_->RequestedFieldsForSection(section);
  TextfieldMap* textfields = &GroupForSection(section)->textfields;
  ComboboxMap* comboboxes = &GroupForSection(section)->comboboxes;

  views::View* view = new views::View();
  views::GridLayout* layout = new views::GridLayout(view);
  view->SetLayoutManager(layout);

  for (DetailInputs::const_iterator it = inputs.begin();
       it != inputs.end(); ++it) {
    const DetailInput& input = *it;
    ui::ComboboxModel* input_model =
        delegate_->ComboboxModelForAutofillType(input.type);
    scoped_ptr<views::View> view_to_add;
    if (input_model) {
      views::Combobox* combobox = new views::Combobox(input_model);
      combobox->set_listener(this);
      comboboxes->insert(std::make_pair(&input, combobox));

      for (int i = 0; i < input_model->GetItemCount(); ++i) {
        if (input.initial_value == input_model->GetItemAt(i)) {
          combobox->SetSelectedIndex(i);
          break;
        }
      }

      view_to_add.reset(combobox);
    } else {
      DecoratedTextfield* field = new DecoratedTextfield(
          input.initial_value,
          l10n_util::GetStringUTF16(input.placeholder_text_rid),
          this);

      gfx::Image icon =
          delegate_->IconForField(input.type, input.initial_value);
      field->SetIcon(icon);

      textfields->insert(std::make_pair(&input, field));
      view_to_add.reset(field);
    }

    int kColumnSetId = input.row_id;
    if (kColumnSetId < 0) {
      other_owned_views_.push_back(view_to_add.release());
      continue;
    }

    views::ColumnSet* column_set = layout->GetColumnSet(kColumnSetId);
    if (!column_set) {
      // Create a new column set and row.
      column_set = layout->AddColumnSet(kColumnSetId);
      if (it != inputs.begin())
        layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
      layout->StartRow(0, kColumnSetId);
    } else {
      // Add a new column to existing row.
      column_set->AddPaddingColumn(0, views::kRelatedControlHorizontalSpacing);
      // Must explicitly skip the padding column since we've already started
      // adding views.
      layout->SkipColumns(1);
    }

    float expand = input.expand_weight;
    column_set->AddColumn(views::GridLayout::FILL,
                          views::GridLayout::FILL,
                          expand ? expand : 1.0,
                          views::GridLayout::USE_PREF,
                          0,
                          0);

    // This is the same as AddView(view_to_add), except that 1 is used for the
    // view's preferred width. Thus the width of the column completely depends
    // on |expand|.
    layout->AddView(view_to_add.release(), 1, 1,
                    views::GridLayout::FILL, views::GridLayout::FILL,
                    1, 0);
  }

  return view;
}

void AutofillDialogViews::UpdateSectionImpl(
    DialogSection section,
    bool clobber_inputs) {
  const DetailInputs& updated_inputs =
      delegate_->RequestedFieldsForSection(section);
  DetailsGroup* group = GroupForSection(section);

  for (DetailInputs::const_iterator iter = updated_inputs.begin();
       iter != updated_inputs.end(); ++iter) {
    const DetailInput& input = *iter;
    TextfieldMap::iterator text_mapping = group->textfields.find(&input);

    if (text_mapping != group->textfields.end()) {
      DecoratedTextfield* decorated = text_mapping->second;
      decorated->SetEnabled(input.editable);
      if (decorated->text().empty() || clobber_inputs) {
        decorated->SetText(iter->initial_value);
        decorated->SetIcon(
            delegate_->IconForField(input.type, decorated->text()));
      }
    }

    ComboboxMap::iterator combo_mapping = group->comboboxes.find(&input);
    if (combo_mapping != group->comboboxes.end()) {
      views::Combobox* combobox = combo_mapping->second;
      combobox->SetEnabled(input.editable);
      if (combobox->selected_index() == combobox->model()->GetDefaultIndex() ||
          clobber_inputs) {
        for (int i = 0; i < combobox->model()->GetItemCount(); ++i) {
          if (input.initial_value == combobox->model()->GetItemAt(i)) {
            combobox->SetSelectedIndex(i);
            break;
          }
        }
      }
    }
  }

  UpdateDetailsGroupState(*group);
}

void AutofillDialogViews::UpdateDetailsGroupState(const DetailsGroup& group) {
  const SuggestionState& suggestion_state =
      delegate_->SuggestionStateForSection(group.section);
  group.suggested_info->SetState(suggestion_state);
  group.manual_input->SetVisible(!suggestion_state.visible);

  UpdateButtonStripExtraView();

  const bool has_menu = !!delegate_->MenuModelForSection(group.section);

  if (group.suggested_button)
    group.suggested_button->SetVisible(has_menu);

  if (group.container) {
    group.container->SetForwardMouseEvents(
        has_menu && suggestion_state.visible);
    group.container->SetVisible(delegate_->SectionIsActive(group.section));
    if (group.container->visible())
      ValidateGroup(group, VALIDATE_EDIT);
  }

  ContentsPreferredSizeChanged();
}

template<class T>
void AutofillDialogViews::SetValidityForInput(
    T* input,
    const base::string16& message) {
  bool invalid = !message.empty();
  input->SetInvalid(invalid);

  if (invalid) {
    validity_map_[input] = message;
  } else {
    validity_map_.erase(input);

    if (error_bubble_ && error_bubble_->anchor() == input) {
      validity_map_.erase(input);
      error_bubble_.reset();
    }
  }
}

void AutofillDialogViews::ShowErrorBubbleForViewIfNecessary(views::View* view) {
  if (!view->GetWidget())
    return;

  std::map<views::View*, base::string16>::iterator error_message =
      validity_map_.find(view);
  if (error_message != validity_map_.end()) {
    view->ScrollRectToVisible(view->GetLocalBounds());

    if (!error_bubble_ || error_bubble_->anchor() != view)
      error_bubble_.reset(new ErrorBubble(view, error_message->second));
  }
}

void AutofillDialogViews::MarkInputsInvalid(DialogSection section,
                                            const ValidityData& validity_data) {
  DetailsGroup* group = GroupForSection(section);
  DCHECK(group->container->visible());

  typedef std::map<ServerFieldType,
      base::Callback<void(const base::string16&)> > FieldMap;
  FieldMap field_map;

  if (group->manual_input->visible()) {
    for (TextfieldMap::const_iterator iter = group->textfields.begin();
         iter != group->textfields.end(); ++iter) {
      field_map[iter->first->type] = base::Bind(
          &AutofillDialogViews::SetValidityForInput<DecoratedTextfield>,
          base::Unretained(this),
          iter->second);
    }
    for (ComboboxMap::const_iterator iter = group->comboboxes.begin();
         iter != group->comboboxes.end(); ++iter) {
      field_map[iter->first->type] = base::Bind(
          &AutofillDialogViews::SetValidityForInput<views::Combobox>,
          base::Unretained(this),
          iter->second);
    }
  } else {
    // Purge invisible views from |validity_map_|.
    std::map<views::View*, base::string16>::iterator it;
    for (it = validity_map_.begin(); it != validity_map_.end();) {
      DCHECK(GroupForView(it->first));
      if (GroupForView(it->first) == group)
        validity_map_.erase(it++);
      else
        ++it;
    }

    if (section == SECTION_CC) {
      // Special case CVC as it's not part of |group->manual_input|.
      field_map[CREDIT_CARD_VERIFICATION_CODE] = base::Bind(
          &AutofillDialogViews::SetValidityForInput<DecoratedTextfield>,
          base::Unretained(this),
          group->suggested_info->decorated_textfield());
    }
  }

  // Flag invalid fields, removing them from |field_map|.
  for (ValidityData::const_iterator iter = validity_data.begin();
       iter != validity_data.end(); ++iter) {
    const base::string16& message = iter->second;
    field_map[iter->first].Run(message);
    field_map.erase(iter->first);
  }

  // The remaining fields in |field_map| are valid. Mark them as such.
  for (FieldMap::iterator iter = field_map.begin(); iter != field_map.end();
       ++iter) {
    iter->second.Run(base::string16());
  }
}

bool AutofillDialogViews::ValidateGroup(const DetailsGroup& group,
                                        ValidationType validation_type) {
  DCHECK(group.container->visible());

  scoped_ptr<DetailInput> cvc_input;
  DetailOutputMap detail_outputs;

  if (group.manual_input->visible()) {
    for (TextfieldMap::const_iterator iter = group.textfields.begin();
         iter != group.textfields.end(); ++iter) {
      if (!iter->first->editable)
        continue;

      detail_outputs[iter->first] = iter->second->text();
    }
    for (ComboboxMap::const_iterator iter = group.comboboxes.begin();
         iter != group.comboboxes.end(); ++iter) {
      if (!iter->first->editable)
        continue;

      views::Combobox* combobox = iter->second;
      base::string16 item =
          combobox->model()->GetItemAt(combobox->selected_index());
      detail_outputs[iter->first] = item;
    }
  } else if (group.section == SECTION_CC) {
    DecoratedTextfield* decorated_cvc =
        group.suggested_info->decorated_textfield();
    cvc_input.reset(new DetailInput);
    cvc_input->type = CREDIT_CARD_VERIFICATION_CODE;
    detail_outputs[cvc_input.get()] = decorated_cvc->text();
  }

  ValidityData invalid_inputs = delegate_->InputsAreValid(
      group.section, detail_outputs, validation_type);
  MarkInputsInvalid(group.section, invalid_inputs);

  return invalid_inputs.empty();
}

bool AutofillDialogViews::ValidateForm() {
  bool all_valid = true;
  validity_map_.clear();

  for (DetailGroupMap::iterator iter = detail_groups_.begin();
       iter != detail_groups_.end(); ++iter) {
    const DetailsGroup& group = iter->second;
    if (!group.container->visible())
      continue;

    if (!ValidateGroup(group, VALIDATE_FINAL))
      all_valid = false;
  }

  return all_valid;
}

void AutofillDialogViews::TextfieldEditedOrActivated(
    views::Textfield* textfield,
    bool was_edit) {
  DetailsGroup* group = GroupForView(textfield);
  DCHECK(group);

  // Figure out the ServerFieldType this textfield represents.
  ServerFieldType type = UNKNOWN_TYPE;
  DecoratedTextfield* decorated = NULL;

  // Look for the input in the manual inputs.
  for (TextfieldMap::const_iterator iter = group->textfields.begin();
       iter != group->textfields.end();
       ++iter) {
    decorated = iter->second;
    if (decorated == textfield) {
      delegate_->UserEditedOrActivatedInput(group->section,
                                              iter->first,
                                              GetWidget()->GetNativeView(),
                                              textfield->GetBoundsInScreen(),
                                              textfield->text(),
                                              was_edit);
      type = iter->first->type;
      break;
    }
  }

  if (textfield == group->suggested_info->decorated_textfield()) {
    decorated = group->suggested_info->decorated_textfield();
    type = CREDIT_CARD_VERIFICATION_CODE;
  }
  DCHECK_NE(UNKNOWN_TYPE, type);

  // If the field is marked as invalid, check if the text is now valid.
  // Many fields (i.e. CC#) are invalid for most of the duration of editing,
  // so flagging them as invalid prematurely is not helpful. However,
  // correcting a minor mistake (i.e. a wrong CC digit) should immediately
  // result in validation - positive user feedback.
  if (decorated->invalid() && was_edit) {
    SetValidityForInput<DecoratedTextfield>(
        decorated,
        delegate_->InputValidityMessage(group->section, type,
                                          textfield->text()));

    // If the field transitioned from invalid to valid, re-validate the group,
    // since inter-field checks become meaningful with valid fields.
    if (!decorated->invalid())
      ValidateGroup(*group, VALIDATE_EDIT);
  }

  gfx::Image icon = delegate_->IconForField(type, textfield->text());
  decorated->SetIcon(icon);
}

void AutofillDialogViews::UpdateButtonStripExtraView() {
  save_in_chrome_checkbox_container_->SetVisible(
      delegate_->ShouldOfferToSaveInChrome());

  gfx::Image image = delegate_->ButtonStripImage();
  button_strip_image_->SetVisible(!image.IsEmpty());
  button_strip_image_->SetImage(image.AsImageSkia());

  autocheckout_progress_bar_view_->SetVisible(
      delegate_->ShouldShowProgressBar());
}

void AutofillDialogViews::ContentsPreferredSizeChanged() {
  if (GetWidget()) {
    GetWidget()->SetSize(GetWidget()->non_client_view()->GetPreferredSize());
    // If the above line does not cause the dialog's size to change, |contents_|
    // may not be laid out. This will trigger a layout only if it's needed.
    SetBoundsRect(bounds());
  }
}

AutofillDialogViews::DetailsGroup* AutofillDialogViews::GroupForSection(
    DialogSection section) {
  return &detail_groups_.find(section)->second;
}

AutofillDialogViews::DetailsGroup* AutofillDialogViews::GroupForView(
    views::View* view) {
  DCHECK(view);

  for (DetailGroupMap::iterator iter = detail_groups_.begin();
       iter != detail_groups_.end(); ++iter) {
    DetailsGroup* group = &iter->second;
    if (view->parent() == group->manual_input)
      return group;

    views::View* decorated =
        view->GetAncestorWithClassName(DecoratedTextfield::kViewClassName);

    // Textfields need to check a second case, since they can be
    // suggested inputs instead of directly editable inputs. Those are
    // accessed via |suggested_info|.
    if (decorated &&
        decorated == group->suggested_info->decorated_textfield()) {
      return group;
    }
  }
  return NULL;
}

views::Textfield* AutofillDialogViews::TextfieldForInput(
    const DetailInput& input) {
  for (DetailGroupMap::iterator iter = detail_groups_.begin();
       iter != detail_groups_.end(); ++iter) {
    const DetailsGroup& group = iter->second;
    TextfieldMap::const_iterator text_mapping = group.textfields.find(&input);
    if (text_mapping != group.textfields.end())
      return text_mapping->second;
  }

  return NULL;
}

views::Combobox* AutofillDialogViews::ComboboxForInput(
    const DetailInput& input) {
  for (DetailGroupMap::iterator iter = detail_groups_.begin();
       iter != detail_groups_.end(); ++iter) {
    const DetailsGroup& group = iter->second;
    ComboboxMap::const_iterator combo_mapping = group.comboboxes.find(&input);
    if (combo_mapping != group.comboboxes.end())
      return combo_mapping->second;
  }

  return NULL;
}

void AutofillDialogViews::DetailsContainerBoundsChanged() {
  if (error_bubble_)
    error_bubble_->UpdatePosition();
}

AutofillDialogViews::DetailsGroup::DetailsGroup(DialogSection section)
    : section(section),
      container(NULL),
      manual_input(NULL),
      suggested_info(NULL),
      suggested_button(NULL) {}

AutofillDialogViews::DetailsGroup::~DetailsGroup() {}

}  // namespace autofill
