// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FIND_BAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FIND_BAR_VIEW_H_

#include "base/compiler_specific.h"
#include "base/strings/string16.h"
#include "chrome/browser/ui/views/dropdown_bar_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"

class FindBarHost;
class FindNotificationDetails;

namespace views {
class ImageButton;
class Label;
class MouseEvent;
class View;
}

////////////////////////////////////////////////////////////////////////////////
//
// The FindBarView is responsible for drawing the UI controls of the
// FindBar, the find text box, the 'Find' button and the 'Close'
// button. It communicates the user search words to the FindBarHost.
//
////////////////////////////////////////////////////////////////////////////////
class FindBarView : public DropdownBarView,
                    public views::ButtonListener,
                    public views::TextfieldController {
 public:
  // A tag denoting which button the user pressed.
  enum ButtonTag {
    FIND_PREVIOUS_TAG = 0,  // The Find Previous button.
    FIND_NEXT_TAG,          // The Find Next button.
    CLOSE_TAG,              // The Close button (the 'X').
  };

  explicit FindBarView(FindBarHost* host);
  virtual ~FindBarView();

  // Gets/sets the text displayed in the text box.
  string16 GetFindText() const;
  void SetFindText(const string16& find_text);

  // Gets the selected text in the text box.
  string16 GetFindSelectedText() const;

  // Gets the match count text displayed in the text box.
  string16 GetMatchCountText() const;

  // Updates the label inside the Find text box that shows the ordinal of the
  // active item and how many matches were found.
  void UpdateForResult(const FindNotificationDetails& result,
                       const string16& find_text);

  // Clears the current Match Count value in the Find text box.
  void ClearMatchCount();

  // Claims focus for the text field and selects its contents.
  virtual void SetFocusAndSelection(bool select_all) OVERRIDE;

  // views::View:
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;
  virtual void Layout() OVERRIDE;
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual void ViewHierarchyChanged(
      const ViewHierarchyChangedDetails& details) OVERRIDE;

  // views::ButtonListener:
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE;

  // views::TextfieldController:
  virtual void ContentsChanged(views::Textfield* sender,
                               const string16& new_contents) OVERRIDE;
  virtual bool HandleKeyEvent(views::Textfield* sender,
                              const ui::KeyEvent& key_event) OVERRIDE;
  virtual void OnAfterUserAction(views::Textfield* sender) OVERRIDE;
  virtual void OnAfterPaste() OVERRIDE;

 private:
  // Starts finding |search_text|.  If the text is empty, stops finding.
  void Find(const string16& search_text);

  // Updates the appearance for the match count label.
  void UpdateMatchCountAppearance(bool no_match);

  // views::View:
  virtual void OnThemeChanged() OVERRIDE;

  // We use a hidden view to grab mouse clicks and bring focus to the find
  // text box. This is because although the find text box may look like it
  // extends all the way to the find button, it only goes as far as to the
  // match_count label. The user, however, expects being able to click anywhere
  // inside what looks like the find text box (including on or around the
  // match_count label) and have focus brought to the find box.
  class FocusForwarderView : public views::View {
   public:
    explicit FocusForwarderView(
        views::Textfield* view_to_focus_on_mousedown)
      : view_to_focus_on_mousedown_(view_to_focus_on_mousedown) {}

   private:
    virtual bool OnMousePressed(const ui::MouseEvent& event) OVERRIDE;

    views::Textfield* view_to_focus_on_mousedown_;

    DISALLOW_COPY_AND_ASSIGN(FocusForwarderView);
  };

  // A wrapper of views::TextField that allows us to select all text when we
  // get focus. Represents the text field where the user enters a search term.
  class SearchTextfieldView : public views::Textfield {
   public:
    SearchTextfieldView();
    virtual ~SearchTextfieldView();

    // views::View:
    virtual bool OnMousePressed(const ui::MouseEvent& event) OVERRIDE;
    virtual void OnMouseReleased(const ui::MouseEvent& event) OVERRIDE;

    // views::Textfield:
    virtual void OnFocus() OVERRIDE;

   private:
    bool select_all_on_focus_;

    DISALLOW_COPY_AND_ASSIGN(SearchTextfieldView);
  };

  // Returns the OS-specific view for the find bar that acts as an intermediary
  // between us and the WebContentsView.
  FindBarHost* find_bar_host() const;

  // Used to detect if the input text, not including the IME composition text,
  // has changed or not.
  string16 last_searched_text_;

  // The controls in the window.
  SearchTextfieldView* find_text_;
  views::Label* match_count_text_;
  FocusForwarderView* focus_forwarder_view_;
  views::ImageButton* find_previous_button_;
  views::ImageButton* find_next_button_;
  views::ImageButton* close_button_;

  // The preferred height of the find bar.
  int preferred_height_;

  // The background image for the Find text box, which we draw behind the Find
  // box to provide the Chrome look to the edge of the text box.
  const gfx::ImageSkia* text_box_background_;

  // The rounded edge on the left side of the Find text box.
  const gfx::ImageSkia* text_box_background_left_;

  DISALLOW_COPY_AND_ASSIGN(FindBarView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_FIND_BAR_VIEW_H_
