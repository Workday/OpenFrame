// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/autofill/autofill_popup_controller_impl.h"

#include <algorithm>
#include <utility>

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/autofill/autofill_popup_view.h"
#include "components/autofill/core/browser/autofill_popup_delegate.h"
#include "content/public/browser/native_web_keyboard_event.h"
#include "grit/webkit_resources.h"
#include "third_party/WebKit/public/web/WebAutofillClient.h"
#include "ui/base/events/event.h"
#include "ui/base/text/text_elider.h"
#include "ui/gfx/display.h"
#include "ui/gfx/rect_conversions.h"
#include "ui/gfx/screen.h"
#include "ui/gfx/vector2d.h"

using base::WeakPtr;
using WebKit::WebAutofillClient;

namespace autofill {
namespace {

// Used to indicate that no line is currently selected by the user.
const int kNoSelection = -1;

// Size difference between name and subtext in pixels.
const int kLabelFontSizeDelta = -2;

// The vertical height of each row in pixels.
const size_t kRowHeight = 24;

// The vertical height of a separator in pixels.
const size_t kSeparatorHeight = 1;

// The maximum amount of characters to display from either the name or subtext.
const size_t kMaxTextLength = 15;

#if !defined(OS_ANDROID)
const size_t kNamePadding = AutofillPopupView::kNamePadding;
const size_t kIconPadding = AutofillPopupView::kIconPadding;
const size_t kEndPadding = AutofillPopupView::kEndPadding;
const size_t kAutofillIconWidth = AutofillPopupView::kAutofillIconWidth;
#endif

struct DataResource {
  const char* name;
  int id;
};

const DataResource kDataResources[] = {
  { "americanExpressCC", IDR_AUTOFILL_CC_AMEX },
  { "dinersCC", IDR_AUTOFILL_CC_DINERS },
  { "discoverCC", IDR_AUTOFILL_CC_DISCOVER },
  { "genericCC", IDR_AUTOFILL_CC_GENERIC },
  { "jcbCC", IDR_AUTOFILL_CC_JCB },
  { "masterCardCC", IDR_AUTOFILL_CC_MASTERCARD },
  { "visaCC", IDR_AUTOFILL_CC_VISA },
};

}  // namespace

// static
WeakPtr<AutofillPopupControllerImpl> AutofillPopupControllerImpl::GetOrCreate(
    WeakPtr<AutofillPopupControllerImpl> previous,
    WeakPtr<AutofillPopupDelegate> delegate,
    gfx::NativeView container_view,
    const gfx::RectF& element_bounds,
    base::i18n::TextDirection text_direction) {
  DCHECK(!previous.get() || previous->delegate_.get() == delegate.get());

  if (previous.get() && previous->container_view() == container_view &&
      previous->element_bounds() == element_bounds) {
    previous->ClearState();
    return previous;
  }

  if (previous.get())
    previous->Hide();

  AutofillPopupControllerImpl* controller =
      new AutofillPopupControllerImpl(
          delegate, container_view, element_bounds, text_direction);
  return controller->GetWeakPtr();
}

AutofillPopupControllerImpl::AutofillPopupControllerImpl(
    base::WeakPtr<AutofillPopupDelegate> delegate,
    gfx::NativeView container_view,
    const gfx::RectF& element_bounds,
    base::i18n::TextDirection text_direction)
    : view_(NULL),
      delegate_(delegate),
      container_view_(container_view),
      element_bounds_(element_bounds),
      text_direction_(text_direction),
      weak_ptr_factory_(this) {
  ClearState();
#if !defined(OS_ANDROID)
  subtext_font_ = name_font_.DeriveFont(kLabelFontSizeDelta);
  warning_font_ = name_font_.DeriveFont(0, gfx::Font::ITALIC);
#endif
}

AutofillPopupControllerImpl::~AutofillPopupControllerImpl() {}

void AutofillPopupControllerImpl::Show(
    const std::vector<string16>& names,
    const std::vector<string16>& subtexts,
    const std::vector<string16>& icons,
    const std::vector<int>& identifiers) {
  SetValues(names, subtexts, icons, identifiers);

#if !defined(OS_ANDROID)
  // Android displays the long text with ellipsis using the view attributes.

  UpdatePopupBounds();
  int popup_width = popup_bounds().width();

  // Elide the name and subtext strings so that the popup fits in the available
  // space.
  for (size_t i = 0; i < names_.size(); ++i) {
    int name_width = GetNameFontForRow(i).GetStringWidth(names_[i]);
    int subtext_width = subtext_font().GetStringWidth(subtexts_[i]);
    int total_text_length = name_width + subtext_width;

    // The line can have no strings if it represents a UI element, such as
    // a separator line.
    if (total_text_length == 0)
      continue;

    int available_width = popup_width - RowWidthWithoutText(i);

    // Each field recieves space in proportion to its length.
    int name_size = available_width * name_width / total_text_length;
    names_[i] = ui::ElideText(names_[i],
                              GetNameFontForRow(i),
                              name_size,
                              ui::ELIDE_AT_END);

    int subtext_size = available_width * subtext_width / total_text_length;
    subtexts_[i] = ui::ElideText(subtexts_[i],
                                 subtext_font(),
                                 subtext_size,
                                 ui::ELIDE_AT_END);
  }
#endif

  if (!view_) {
    view_ = AutofillPopupView::Create(this);

    // It is possible to fail to create the popup, in this case
    // treat the popup as hiding right away.
    if (!view_) {
      Hide();
      return;
    }

    ShowView();
  } else {
    UpdateBoundsAndRedrawPopup();
  }

  delegate_->OnPopupShown(this);
}

void AutofillPopupControllerImpl::UpdateDataListValues(
    const std::vector<base::string16>& values,
    const std::vector<base::string16>& labels) {
  // Remove all the old data list values, which should always be at the top of
  // the list if they are present.
  while (!identifiers_.empty() &&
         identifiers_[0] == WebAutofillClient::MenuItemIDDataListEntry) {
    names_.erase(names_.begin());
    subtexts_.erase(subtexts_.begin());
    icons_.erase(icons_.begin());
    identifiers_.erase(identifiers_.begin());
  }

  // If there are no new data list values, exit (clearing the separator if there
  // is one).
  if (values.empty()) {
    if (!identifiers_.empty() &&
        identifiers_[0] == WebAutofillClient::MenuItemIDSeparator) {
      names_.erase(names_.begin());
      subtexts_.erase(subtexts_.begin());
      icons_.erase(icons_.begin());
      identifiers_.erase(identifiers_.begin());
    }

     // The popup contents have changed, so either update the bounds or hide it.
    if (HasSuggestions())
      UpdateBoundsAndRedrawPopup();
    else
      Hide();

    return;
  }

  // Add a separator if there are any other values.
  if (!identifiers_.empty() &&
      identifiers_[0] != WebAutofillClient::MenuItemIDSeparator) {
    names_.insert(names_.begin(), string16());
    subtexts_.insert(subtexts_.begin(), string16());
    icons_.insert(icons_.begin(), string16());
    identifiers_.insert(identifiers_.begin(),
                        WebAutofillClient::MenuItemIDSeparator);
  }


  names_.insert(names_.begin(), values.begin(), values.end());
  subtexts_.insert(subtexts_.begin(), labels.begin(), labels.end());

  // Add the values that are the same for all data list elements.
  icons_.insert(icons_.begin(), values.size(), base::string16());
  identifiers_.insert(identifiers_.begin(),
                      values.size(),
                      WebAutofillClient::MenuItemIDDataListEntry);

  UpdateBoundsAndRedrawPopup();
}

void AutofillPopupControllerImpl::Hide() {
  if (delegate_.get())
    delegate_->OnPopupHidden(this);

  if (view_)
    view_->Hide();

  delete this;
}

void AutofillPopupControllerImpl::ViewDestroyed() {
  // The view has already been destroyed so clear the reference to it.
  view_ = NULL;

  Hide();
}

bool AutofillPopupControllerImpl::HandleKeyPressEvent(
    const content::NativeWebKeyboardEvent& event) {
  switch (event.windowsKeyCode) {
    case ui::VKEY_UP:
      SelectPreviousLine();
      return true;
    case ui::VKEY_DOWN:
      SelectNextLine();
      return true;
    case ui::VKEY_PRIOR:  // Page up.
      SetSelectedLine(0);
      return true;
    case ui::VKEY_NEXT:  // Page down.
      SetSelectedLine(names().size() - 1);
      return true;
    case ui::VKEY_ESCAPE:
      Hide();
      return true;
    case ui::VKEY_DELETE:
      return (event.modifiers & content::NativeWebKeyboardEvent::ShiftKey) &&
             RemoveSelectedLine();
    case ui::VKEY_TAB:
      // A tab press should cause the highlighted line to be selected, but still
      // return false so the tab key press propagates and changes the cursor
      // location.
      AcceptSelectedLine();
      return false;
    case ui::VKEY_RETURN:
      return AcceptSelectedLine();
    default:
      return false;
  }
}

void AutofillPopupControllerImpl::UpdateBoundsAndRedrawPopup() {
#if !defined(OS_ANDROID)
  // TODO(csharp): Since UpdatePopupBounds can change the position of the popup,
  // the popup could end up jumping from above the element to below it.
  // It is unclear if it is better to keep the popup where it was, or if it
  // should try and move to its desired position.
  UpdatePopupBounds();
#endif

  view_->UpdateBoundsAndRedrawPopup();
}

void AutofillPopupControllerImpl::MouseHovered(int x, int y) {
  SetSelectedLine(LineFromY(y));
}

void AutofillPopupControllerImpl::MouseClicked(int x, int y) {
  MouseHovered(x, y);
  AcceptSelectedLine();
}

void AutofillPopupControllerImpl::MouseExitedPopup() {
  SetSelectedLine(kNoSelection);
}

void AutofillPopupControllerImpl::AcceptSuggestion(size_t index) {
  delegate_->DidAcceptSuggestion(full_names_[index], identifiers_[index]);
}

int AutofillPopupControllerImpl::GetIconResourceID(
    const string16& resource_name) {
  for (size_t i = 0; i < arraysize(kDataResources); ++i) {
    if (resource_name == ASCIIToUTF16(kDataResources[i].name))
      return kDataResources[i].id;
  }

  return -1;
}

bool AutofillPopupControllerImpl::CanDelete(size_t index) const {
  // TODO(isherman): Native AddressBook suggestions on Mac and Android should
  // not be considered to be deleteable.
  int id = identifiers_[index];
  return id > 0 ||
      id == WebAutofillClient::MenuItemIDAutocompleteEntry ||
      id == WebAutofillClient::MenuItemIDPasswordEntry;
}

bool AutofillPopupControllerImpl::IsWarning(size_t index) const {
  return identifiers_[index] == WebAutofillClient::MenuItemIDWarningMessage;
}

gfx::Rect AutofillPopupControllerImpl::GetRowBounds(size_t index) {
  int top = 0;
  for (size_t i = 0; i < index; ++i) {
    top += GetRowHeightFromId(identifiers()[i]);
  }

  return gfx::Rect(
      0,
      top,
      popup_bounds_.width(),
      GetRowHeightFromId(identifiers()[index]));
}

void AutofillPopupControllerImpl::SetPopupBounds(const gfx::Rect& bounds) {
  popup_bounds_ = bounds;
  UpdateBoundsAndRedrawPopup();
}

const gfx::Rect& AutofillPopupControllerImpl::popup_bounds() const {
  return popup_bounds_;
}

gfx::NativeView AutofillPopupControllerImpl::container_view() const {
  return container_view_;
}

const gfx::RectF& AutofillPopupControllerImpl::element_bounds() const {
  return element_bounds_;
}

bool AutofillPopupControllerImpl::IsRTL() const {
  return text_direction_ == base::i18n::RIGHT_TO_LEFT;
}

const std::vector<string16>& AutofillPopupControllerImpl::names() const {
  return names_;
}

const std::vector<string16>& AutofillPopupControllerImpl::subtexts() const {
  return subtexts_;
}

const std::vector<string16>& AutofillPopupControllerImpl::icons() const {
  return icons_;
}

const std::vector<int>& AutofillPopupControllerImpl::identifiers() const {
  return identifiers_;
}

#if !defined(OS_ANDROID)
const gfx::Font& AutofillPopupControllerImpl::GetNameFontForRow(size_t index)
    const {
  if (identifiers_[index] == WebAutofillClient::MenuItemIDWarningMessage)
    return warning_font_;

  return name_font_;
}

const gfx::Font& AutofillPopupControllerImpl::subtext_font() const {
  return subtext_font_;
}
#endif

int AutofillPopupControllerImpl::selected_line() const {
  return selected_line_;
}

void AutofillPopupControllerImpl::SetSelectedLine(int selected_line) {
  if (selected_line_ == selected_line)
    return;

  if (selected_line_ != kNoSelection &&
      static_cast<size_t>(selected_line_) < identifiers_.size())
    InvalidateRow(selected_line_);

  if (selected_line != kNoSelection)
    InvalidateRow(selected_line);

  selected_line_ = selected_line;

  if (selected_line_ != kNoSelection)
    delegate_->DidSelectSuggestion(identifiers_[selected_line_]);
  else
    delegate_->ClearPreviewedForm();
}

void AutofillPopupControllerImpl::SelectNextLine() {
  int new_selected_line = selected_line_ + 1;

  // Skip over any lines that can't be selected.
  while (static_cast<size_t>(new_selected_line) < names_.size() &&
         !CanAccept(identifiers()[new_selected_line])) {
    ++new_selected_line;
  }

  if (new_selected_line >= static_cast<int>(names_.size()))
    new_selected_line = 0;

  SetSelectedLine(new_selected_line);
}

void AutofillPopupControllerImpl::SelectPreviousLine() {
  int new_selected_line = selected_line_ - 1;

  // Skip over any lines that can't be selected.
  while (new_selected_line > kNoSelection &&
         !CanAccept(identifiers()[new_selected_line])) {
    --new_selected_line;
  }

  if (new_selected_line <= kNoSelection)
    new_selected_line = names_.size() - 1;

  SetSelectedLine(new_selected_line);
}

bool AutofillPopupControllerImpl::AcceptSelectedLine() {
  if (selected_line_ == kNoSelection)
    return false;

  DCHECK_GE(selected_line_, 0);
  DCHECK_LT(selected_line_, static_cast<int>(names_.size()));

  if (!CanAccept(identifiers_[selected_line_]))
    return false;

  AcceptSuggestion(selected_line_);
  return true;
}

bool AutofillPopupControllerImpl::RemoveSelectedLine() {
  if (selected_line_ == kNoSelection)
    return false;

  DCHECK_GE(selected_line_, 0);
  DCHECK_LT(selected_line_, static_cast<int>(names_.size()));

  if (!CanDelete(selected_line_))
    return false;

  delegate_->RemoveSuggestion(full_names_[selected_line_],
                              identifiers_[selected_line_]);

  // Remove the deleted element.
  names_.erase(names_.begin() + selected_line_);
  full_names_.erase(full_names_.begin() + selected_line_);
  subtexts_.erase(subtexts_.begin() + selected_line_);
  icons_.erase(icons_.begin() + selected_line_);
  identifiers_.erase(identifiers_.begin() + selected_line_);

  SetSelectedLine(kNoSelection);

  if (HasSuggestions()) {
    delegate_->ClearPreviewedForm();
    UpdateBoundsAndRedrawPopup();
  } else {
    Hide();
  }

  return true;
}

int AutofillPopupControllerImpl::LineFromY(int y) {
  int current_height = 0;

  for (size_t i = 0; i < identifiers().size(); ++i) {
    current_height += GetRowHeightFromId(identifiers()[i]);

    if (y <= current_height)
      return i;
  }

  // The y value goes beyond the popup so stop the selection at the last line.
  return identifiers().size() - 1;
}

int AutofillPopupControllerImpl::GetRowHeightFromId(int identifier) const {
  if (identifier == WebAutofillClient::MenuItemIDSeparator)
    return kSeparatorHeight;

  return kRowHeight;
}

bool AutofillPopupControllerImpl::CanAccept(int id) {
  return id != WebAutofillClient::MenuItemIDSeparator &&
      id != WebAutofillClient::MenuItemIDWarningMessage;
}

bool AutofillPopupControllerImpl::HasSuggestions() {
  return identifiers_.size() != 0 &&
      (identifiers_[0] > 0 ||
       identifiers_[0] ==
           WebAutofillClient::MenuItemIDAutocompleteEntry ||
       identifiers_[0] == WebAutofillClient::MenuItemIDPasswordEntry ||
       identifiers_[0] == WebAutofillClient::MenuItemIDDataListEntry);
}

void AutofillPopupControllerImpl::SetValues(
    const std::vector<string16>& names,
    const std::vector<string16>& subtexts,
    const std::vector<string16>& icons,
    const std::vector<int>& identifiers) {
  names_ = names;
  full_names_ = names;
  subtexts_ = subtexts;
  icons_ = icons;
  identifiers_ = identifiers;
}

void AutofillPopupControllerImpl::ShowView() {
  view_->Show();
}

void AutofillPopupControllerImpl::InvalidateRow(size_t row) {
  DCHECK(0 <= row);
  DCHECK(row < identifiers_.size());
  view_->InvalidateRow(row);
}

#if !defined(OS_ANDROID)
int AutofillPopupControllerImpl::GetDesiredPopupWidth() const {
  if (!name_font_.platform_font() || !subtext_font_.platform_font()) {
    // We can't calculate the size of the popup if the fonts
    // aren't present.
    return 0;
  }

  int popup_width = RoundedElementBounds().width();
  DCHECK_EQ(names().size(), subtexts().size());
  for (size_t i = 0; i < names().size(); ++i) {
    int row_size = name_font_.GetStringWidth(names()[i]) +
        subtext_font_.GetStringWidth(subtexts()[i]) +
        RowWidthWithoutText(i);

    popup_width = std::max(popup_width, row_size);
  }

  return popup_width;
}

int AutofillPopupControllerImpl::GetDesiredPopupHeight() const {
  int popup_height = 0;

  for (size_t i = 0; i < identifiers().size(); ++i) {
    popup_height += GetRowHeightFromId(identifiers()[i]);
  }

  return popup_height;
}

int AutofillPopupControllerImpl::RowWidthWithoutText(int row) const {
  int row_size = kEndPadding;

  if (!subtexts_[row].empty())
    row_size += kNamePadding;

  // Add the Autofill icon size, if required.
  if (!icons_[row].empty())
    row_size += kAutofillIconWidth + kIconPadding;

  // Add the padding at the end
  row_size += kEndPadding;

  return row_size;
}

void AutofillPopupControllerImpl::UpdatePopupBounds() {
  int popup_required_width = GetDesiredPopupWidth();
  int popup_height = GetDesiredPopupHeight();
  // This is the top left point of the popup if the popup is above the element
  // and grows to the left (since that is the highest and furthest left the
  // popup go could).
  gfx::Point top_left_corner_of_popup = RoundedElementBounds().origin() +
      gfx::Vector2d(RoundedElementBounds().width() - popup_required_width,
                    -popup_height);

  // This is the bottom right point of the popup if the popup is below the
  // element and grows to the right (since the is the lowest and furthest right
  // the popup could go).
  gfx::Point bottom_right_corner_of_popup = RoundedElementBounds().origin() +
      gfx::Vector2d(popup_required_width,
                    RoundedElementBounds().height() + popup_height);

  gfx::Display top_left_display = GetDisplayNearestPoint(
      top_left_corner_of_popup);
  gfx::Display bottom_right_display = GetDisplayNearestPoint(
      bottom_right_corner_of_popup);

  std::pair<int, int> popup_x_and_width = CalculatePopupXAndWidth(
      top_left_display, bottom_right_display, popup_required_width);
  std::pair<int, int> popup_y_and_height = CalculatePopupYAndHeight(
      top_left_display, bottom_right_display, popup_height);

  popup_bounds_ = gfx::Rect(popup_x_and_width.first,
                            popup_y_and_height.first,
                            popup_x_and_width.second,
                            popup_y_and_height.second);
}
#endif  // !defined(OS_ANDROID)

WeakPtr<AutofillPopupControllerImpl> AutofillPopupControllerImpl::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void AutofillPopupControllerImpl::ClearState() {
  // Don't clear view_, because otherwise the popup will have to get regenerated
  // and this will cause flickering.

  popup_bounds_ = gfx::Rect();

  names_.clear();
  subtexts_.clear();
  icons_.clear();
  identifiers_.clear();
  full_names_.clear();

  selected_line_ = kNoSelection;
}

const gfx::Rect AutofillPopupControllerImpl::RoundedElementBounds() const {
  return gfx::ToEnclosingRect(element_bounds_);
}

gfx::Display AutofillPopupControllerImpl::GetDisplayNearestPoint(
    const gfx::Point& point) const {
  return gfx::Screen::GetScreenFor(container_view())->GetDisplayNearestPoint(
      point);
}

std::pair<int, int> AutofillPopupControllerImpl::CalculatePopupXAndWidth(
    const gfx::Display& left_display,
    const gfx::Display& right_display,
    int popup_required_width) const {
  int leftmost_display_x = left_display.bounds().x();
  int rightmost_display_x =
      right_display.GetSizeInPixel().width() + right_display.bounds().x();

  // Calculate the start coordinates for the popup if it is growing right or
  // the end position if it is growing to the left, capped to screen space.
  int right_growth_start = std::max(leftmost_display_x,
                                    std::min(rightmost_display_x,
                                             RoundedElementBounds().x()));
  int left_growth_end = std::max(leftmost_display_x,
                                   std::min(rightmost_display_x,
                                            RoundedElementBounds().right()));

  int right_available = rightmost_display_x - right_growth_start;
  int left_available = left_growth_end - leftmost_display_x;

  int popup_width = std::min(popup_required_width,
                             std::max(right_available, left_available));

  // If there is enough space for the popup on the right, show it there,
  // otherwise choose the larger size.
  if (right_available >= popup_width || right_available >= left_available)
    return std::make_pair(right_growth_start, popup_width);
  else
    return std::make_pair(left_growth_end - popup_width, popup_width);
}

std::pair<int,int> AutofillPopupControllerImpl::CalculatePopupYAndHeight(
    const gfx::Display& top_display,
    const gfx::Display& bottom_display,
    int popup_required_height) const {
  int topmost_display_y = top_display.bounds().y();
  int bottommost_display_y =
      bottom_display.GetSizeInPixel().height() + bottom_display.bounds().y();

  // Calculate the start coordinates for the popup if it is growing down or
  // the end position if it is growing up, capped to screen space.
  int top_growth_end = std::max(topmost_display_y,
                                std::min(bottommost_display_y,
                                         RoundedElementBounds().y()));
  int bottom_growth_start = std::max(topmost_display_y,
      std::min(bottommost_display_y, RoundedElementBounds().bottom()));

  int top_available = bottom_growth_start - topmost_display_y;
  int bottom_available = bottommost_display_y - top_growth_end;

  // TODO(csharp): Restrict the popup height to what is available.
  if (bottom_available >= popup_required_height ||
      bottom_available >= top_available) {
    // The popup can appear below the field.
    return std::make_pair(bottom_growth_start, popup_required_height);
  } else {
    // The popup must appear above the field.
    return std::make_pair(top_growth_end - popup_required_height,
                          popup_required_height);
  }
}

}  // namespace autofill
