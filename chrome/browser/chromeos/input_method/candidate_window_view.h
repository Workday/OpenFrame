// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_INPUT_METHOD_CANDIDATE_WINDOW_VIEW_H_
#define CHROME_BROWSER_CHROMEOS_INPUT_METHOD_CANDIDATE_WINDOW_VIEW_H_

#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "chromeos/dbus/ibus/ibus_lookup_table.h"
#include "ui/views/view.h"

namespace gfx {
class Font;
}

namespace chromeos {
class IBusLookupTable;
namespace input_method {

class CandidateView;
class HidableArea;
class InformationTextArea;

// CandidateWindowView is the main container of the candidate window UI.
class CandidateWindowView : public views::View {
 public:
  // The object can be monitored by the observer.
  class Observer {
   public:
    virtual ~Observer() {}
    // The function is called when a candidate is committed.
    // See comments at NotifyCandidateClicked() in chromeos_input_method_ui.h
    // for details about the parameters.
    virtual void OnCandidateCommitted(int index, int button, int flag) = 0;

    virtual void OnCandidateWindowOpened() = 0;
    virtual void OnCandidateWindowClosed() = 0;
  };

  explicit CandidateWindowView(views::Widget* parent_frame);
  virtual ~CandidateWindowView();
  void Init();

  // Adds the given observer. The ownership is not transferred.
  void AddObserver(Observer* observer) {
    observers_.AddObserver(observer);
  }

  // Removes the given observer.
  void RemoveObserver(Observer* observer) {
    observers_.RemoveObserver(observer);
  }

  // Selects the candidate specified by the index in the current page
  // (zero-origin).  Changes the appearance of the selected candidate,
  // updates the information in the candidate window as needed.
  void SelectCandidateAt(int index_in_page);

  // The function is called when a candidate is being dragged. From the
  // given point, locates the candidate under the mouse cursor, and
  // selects it.
  void OnCandidatePressed(const gfx::Point& point);

  // Commits the candidate currently being selected.
  void CommitCandidate();

  // Hides the lookup table.
  void HideLookupTable();

  // Hides the auxiliary text.
  void HideAuxiliaryText();

  // Hides the preedit text.
  void HidePreeditText();

  // Hides whole the candidate window.
  void HideAll();

  // Shows the lookup table.
  void ShowLookupTable();

  // Shows the auxiliary text.
  void ShowAuxiliaryText();

  // Shows the preedit text.
  void ShowPreeditText();

  // Updates the auxiliary text.
  void UpdateAuxiliaryText(const std::string& utf8_text);

  // Updates the preedit text.
  void UpdatePreeditText(const std::string& utf8_text);

  // Returns true if we should update candidate views in the window.  For
  // instance, if we are going to show the same candidates as before, we
  // don't have to update candidate views. This happens when the user just
  // moves the cursor in the same page in the candidate window.
  static bool ShouldUpdateCandidateViews(
      const IBusLookupTable& old_table,
      const IBusLookupTable& new_table);

  // Updates candidates of the candidate window from |lookup_table|.
  // Candidates are arranged per |orientation|.
  void UpdateCandidates(const IBusLookupTable& lookup_table);

  // Resizes and moves the parent frame. The two actions should be
  // performed consecutively as resizing may require the candidate window
  // to move. For instance, we may need to move the candidate window from
  // below the cursor to above the cursor, if the candidate window becomes
  // too big to be shown near the bottom of the screen.  This function
  // needs to be called when the visible contents of the candidate window
  // are modified.
  void ResizeAndMoveParentFrame();

  // Returns the horizontal offset used for placing the vertical candidate
  // window so that the first candidate is aligned with the the text being
  // converted like:
  //
  //      XXX           <- The user is converting XXX
  //   +-----+
  //   |1 XXX|
  //   |2 YYY|
  //   |3 ZZZ|
  //
  // Returns 0 if no candidate is present.
  int GetHorizontalOffset();

  void set_cursor_location(const gfx::Rect& cursor_location) {
    cursor_location_ = cursor_location;
  }

  void set_composition_head_location(
      const gfx::Rect& composition_head_location) {
    composition_head_location_ = composition_head_location;
  }

  const gfx::Rect& cursor_location() const { return cursor_location_; }
  const gfx::Rect& composition_head_location() const {
    return composition_head_location_;
  }

 protected:
  // Override View::VisibilityChanged()
  virtual void VisibilityChanged(View* starting_from, bool is_visible) OVERRIDE;

  // Override View::OnBoundsChanged()
  virtual void OnBoundsChanged(const gfx::Rect& previous_bounds) OVERRIDE;

 private:
  FRIEND_TEST_ALL_PREFIXES(CandidateWindowViewTest,
                           UpdateCandidatesTest_CursorVisibility);
  FRIEND_TEST_ALL_PREFIXES(CandidateWindowViewTest, ShortcutSettingTest);
  FRIEND_TEST_ALL_PREFIXES(CandidateWindowViewTest,
                           DoNotChangeRowHeightWithLabelSwitchTest);

  // Initializes the candidate views if needed.
  void MaybeInitializeCandidateViews(const IBusLookupTable& lookup_table);

  // Returns the appropriate area (header or footer) to put auxiliary texts.
  InformationTextArea* GetAuxiliaryTextArea();

  // Returns true if the candidate window is open.  The suggestion window does
  // not count as the candidate window.
  bool IsCandidateWindowOpen() const;

  // Notifies observers if the candidate window's opened/closed state has
  // changed from the previous call to this function.
  void NotifyIfCandidateWindowOpenedOrClosed();

  // The lookup table (candidates).
  IBusLookupTable lookup_table_;

  // The index in the current page of the candidate currently being selected.
  int selected_candidate_index_in_page_;

  // The observers of the object.
  ObserverList<Observer> observers_;

  // The parent frame.
  views::Widget* parent_frame_;

  // Views created in the class will be part of tree of |this|, so these
  // child views will be deleted when |this| is deleted.

  // The preedit area is where the preedit text is shown, if it is needed
  // in cases such as the focus is on a plugin that doesn't support in-line
  // preedit drawing.
  InformationTextArea* preedit_area_;
  // The header area is where the auxiliary text is shown, if the
  // orientation is horizontal. If the auxiliary text is not provided, we
  // show nothing.  For instance, we show pinyin text like "zhong'guo".
  InformationTextArea* header_area_;
  // The candidate area is where candidates are rendered.
  HidableArea* candidate_area_;
  // The candidate views are used for rendering candidates.
  std::vector<CandidateView*> candidate_views_;
  // The footer area is where the auxiliary text is shown, if the
  // orientation is vertical. Usually the auxiliary text is used for
  // showing candidate number information like 2/19.
  InformationTextArea* footer_area_;

  // Current columns size in |candidate_area_|.
  gfx::Size previous_shortcut_column_size_;
  gfx::Size previous_candidate_column_size_;
  gfx::Size previous_annotation_column_size_;

  // The last cursor location.
  gfx::Rect cursor_location_;

  // The last compostion head location.
  gfx::Rect composition_head_location_;

  // True if the candidate window should be shown with aligning with composition
  // text as opposed to the cursor.
  bool should_show_at_composition_head_;

  // True if the candidate window should be shonw on the upper side of
  // composition text.
  bool should_show_upper_side_;

  // True if the candidate window was open.  This is used to determine when to
  // send OnCandidateWindowOpened and OnCandidateWindowClosed events.
  bool was_candidate_window_open_;

  // This function judge whether the candidate window should be shown or not,
  // if should be, shows parent_frame and if not, hides parent_frame.
  void UpdateParentArea();

  DISALLOW_COPY_AND_ASSIGN(CandidateWindowView);
};

}  // namespace input_method
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_INPUT_METHOD_CANDIDATE_WINDOW_VIEW_H_
