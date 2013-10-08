// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_AUTOFILL_AUTOFILL_POPUP_CONTROLLER_IMPL_H_
#define CHROME_BROWSER_UI_AUTOFILL_AUTOFILL_POPUP_CONTROLLER_IMPL_H_

#include "base/gtest_prod_util.h"
#include "base/i18n/rtl.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string16.h"
#include "chrome/browser/ui/autofill/autofill_popup_controller.h"
#include "content/public/browser/keyboard_listener.h"
#include "ui/gfx/font.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/rect_f.h"

namespace gfx {
class Display;
}

namespace ui {
class KeyEvent;
}

namespace autofill {

class AutofillPopupDelegate;
class AutofillPopupView;

// This class is a controller for an AutofillPopupView. It implements
// AutofillPopupController to allow calls from AutofillPopupView. The
// other, public functions are available to its instantiator.
class AutofillPopupControllerImpl : public AutofillPopupController,
                                    public content::KeyboardListener {
 public:
  // Creates a new |AutofillPopupControllerImpl|, or reuses |previous| if
  // the construction arguments are the same. |previous| may be invalidated by
  // this call.
  static base::WeakPtr<AutofillPopupControllerImpl> GetOrCreate(
      base::WeakPtr<AutofillPopupControllerImpl> previous,
      base::WeakPtr<AutofillPopupDelegate> delegate,
      gfx::NativeView container_view,
      const gfx::RectF& element_bounds,
      base::i18n::TextDirection text_direction);

  // Shows the popup, or updates the existing popup with the given values.
  void Show(const std::vector<string16>& names,
            const std::vector<string16>& subtexts,
            const std::vector<string16>& icons,
            const std::vector<int>& identifiers);

  // Updates the data list values currently shown with the popup.
  void UpdateDataListValues(const std::vector<base::string16>& values,
                            const std::vector<base::string16>& labels);

  // Hides the popup and destroys the controller. This also invalidates
  // |delegate_|.
  virtual void Hide() OVERRIDE;

  // Invoked when the view was destroyed by by someone other than this class.
  virtual void ViewDestroyed() OVERRIDE;

  // KeyboardListener implementation.
  virtual bool HandleKeyPressEvent(
      const content::NativeWebKeyboardEvent& event) OVERRIDE;

 protected:
  FRIEND_TEST_ALL_PREFIXES(AutofillExternalDelegateBrowserTest,
                           CloseWidgetAndNoLeaking);
  FRIEND_TEST_ALL_PREFIXES(AutofillPopupControllerUnitTest,
                           ProperlyResetController);

  AutofillPopupControllerImpl(base::WeakPtr<AutofillPopupDelegate> delegate,
                              gfx::NativeView container_view,
                              const gfx::RectF& element_bounds,
                              base::i18n::TextDirection text_direction);
  virtual ~AutofillPopupControllerImpl();

  // AutofillPopupController implementation.
  virtual void UpdateBoundsAndRedrawPopup() OVERRIDE;
  virtual void MouseHovered(int x, int y) OVERRIDE;
  virtual void MouseClicked(int x, int y) OVERRIDE;
  virtual void MouseExitedPopup() OVERRIDE;
  virtual void AcceptSuggestion(size_t index) OVERRIDE;
  virtual int GetIconResourceID(const string16& resource_name) OVERRIDE;
  virtual bool CanDelete(size_t index) const OVERRIDE;
  virtual bool IsWarning(size_t index) const OVERRIDE;
  virtual gfx::Rect GetRowBounds(size_t index) OVERRIDE;
  virtual void SetPopupBounds(const gfx::Rect& bounds) OVERRIDE;
  virtual const gfx::Rect& popup_bounds() const OVERRIDE;
  virtual gfx::NativeView container_view() const OVERRIDE;
  virtual const gfx::RectF& element_bounds() const OVERRIDE;
  virtual bool IsRTL() const OVERRIDE;

  virtual const std::vector<string16>& names() const OVERRIDE;
  virtual const std::vector<string16>& subtexts() const OVERRIDE;
  virtual const std::vector<string16>& icons() const OVERRIDE;
  virtual const std::vector<int>& identifiers() const OVERRIDE;
#if !defined(OS_ANDROID)
  virtual const gfx::Font& GetNameFontForRow(size_t index) const OVERRIDE;
  virtual const gfx::Font& subtext_font() const OVERRIDE;
#endif
  virtual int selected_line() const OVERRIDE;

  // Change which line is currently selected by the user.
  void SetSelectedLine(int selected_line);

  // Increase the selected line by 1, properly handling wrapping.
  void SelectNextLine();

  // Decrease the selected line by 1, properly handling wrapping.
  void SelectPreviousLine();

  // The user has choosen the selected line.
  bool AcceptSelectedLine();

  // The user has removed a suggestion.
  bool RemoveSelectedLine();

  // Convert a y-coordinate to the closest line.
  int LineFromY(int y);

  // Returns the height of a row depending on its type.
  int GetRowHeightFromId(int identifier) const;

  // Returns true if the given id refers to an element that can be accepted.
  bool CanAccept(int id);

  // Returns true if the popup still has non-options entries to show the user.
  bool HasSuggestions();

  // Set the Autofill entry values. Exposed to allow tests to set these values
  // without showing the popup.
  void SetValues(const std::vector<string16>& names,
                 const std::vector<string16>& subtexts,
                 const std::vector<string16>& icons,
                 const std::vector<int>& identifier);

  AutofillPopupView* view() { return view_; }

  // |view_| pass throughs (virtual for testing).
  virtual void ShowView();
  virtual void InvalidateRow(size_t row);

  // Protected so tests can access.
#if !defined(OS_ANDROID)
  // Calculates the desired width of the popup based on its contents.
  int GetDesiredPopupWidth() const;

  // Calculates the desired height of the popup based on its contents.
  int GetDesiredPopupHeight() const;

  // Calculate the width of the row, excluding all the text. This provides
  // the size of the row that won't be reducible (since all the text can be
  // elided if there isn't enough space).
  int RowWidthWithoutText(int row) const;
#endif

  base::WeakPtr<AutofillPopupControllerImpl> GetWeakPtr();

 private:
  // Clear the internal state of the controller. This is needed to ensure that
  // when the popup is reused it doesn't leak values between uses.
  void ClearState();

  const gfx::Rect RoundedElementBounds() const;
#if !defined(OS_ANDROID)
  // Calculates and sets the bounds of the popup, including placing it properly
  // to prevent it from going off the screen.
  void UpdatePopupBounds();
#endif

  // A helper function to get the display closest to the given point (virtual
  // for testing).
  virtual gfx::Display GetDisplayNearestPoint(const gfx::Point& point) const;

  // Calculates the width of the popup and the x position of it. These values
  // will stay on the screen.
  std::pair<int, int> CalculatePopupXAndWidth(
      const gfx::Display& left_display,
      const gfx::Display& right_display,
      int popup_required_width) const;

  // Calculates the height of the popup and the y position of it. These values
  // will stay on the screen.
  std::pair<int, int> CalculatePopupYAndHeight(
      const gfx::Display& top_display,
      const gfx::Display& bottom_display,
      int popup_required_height) const;

  AutofillPopupView* view_;  // Weak reference.
  base::WeakPtr<AutofillPopupDelegate> delegate_;
  gfx::NativeView container_view_;  // Weak reference.

  // The bounds of the text element that is the focus of the Autofill.
  // These coordinates are in screen space.
  const gfx::RectF element_bounds_;

  // The bounds of the Autofill popup.
  gfx::Rect popup_bounds_;

  // The text direction of the popup.
  base::i18n::TextDirection text_direction_;

  // The current Autofill query values.
  std::vector<string16> names_;
  std::vector<string16> subtexts_;
  std::vector<string16> icons_;
  std::vector<int> identifiers_;

  // Since names_ can be elided to ensure that it fits on the screen, we need to
  // keep an unelided copy of the names to be able to pass to the delegate.
  std::vector<string16> full_names_;

#if !defined(OS_ANDROID)
  // The fonts for the popup text.
  gfx::Font name_font_;
  gfx::Font subtext_font_;
  gfx::Font warning_font_;
#endif

  // The line that is currently selected by the user.
  // |kNoSelection| indicates that no line is currently selected.
  int selected_line_;

  base::WeakPtrFactory<AutofillPopupControllerImpl> weak_ptr_factory_;
};

}  // namespace autofill

#endif  // CHROME_BROWSER_UI_AUTOFILL_AUTOFILL_POPUP_CONTROLLER_IMPL_H_
