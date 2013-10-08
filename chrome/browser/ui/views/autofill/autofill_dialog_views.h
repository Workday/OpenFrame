// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AUTOFILL_AUTOFILL_DIALOG_VIEWS_H_
#define CHROME_BROWSER_UI_VIEWS_AUTOFILL_AUTOFILL_DIALOG_VIEWS_H_

#include <map>

#include "base/memory/scoped_vector.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observer.h"
#include "chrome/browser/ui/autofill/autofill_dialog_types.h"
#include "chrome/browser/ui/autofill/autofill_dialog_view.h"
#include "chrome/browser/ui/autofill/autofill_dialog_view_delegate.h"
#include "chrome/browser/ui/autofill/testable_autofill_dialog_view.h"
#include "ui/base/animation/animation_delegate.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/menu_button_listener.h"
#include "ui/views/controls/combobox/combobox_listener.h"
#include "ui/views/controls/link_listener.h"
#include "ui/views/controls/progress_bar.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/styled_label_listener.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/views/window/dialog_delegate.h"

namespace content {
class KeyboardListener;
}

namespace gfx {
class Image;
}

namespace views {
class BubbleBorder;
class Checkbox;
class Combobox;
class FocusManager;
class ImageButton;
class ImageView;
class Label;
class LabelButton;
class Link;
class MenuRunner;
class StyledLabel;
class WebView;
class Widget;
}  // namespace views

namespace ui {
class ComboboxModel;
class KeyEvent;
class MultiAnimation;
}

namespace autofill {

class AutofillDialogSignInDelegate;
class DecoratedTextfield;

// Views toolkit implementation of the Autofill dialog that handles the
// imperative autocomplete API call.
class AutofillDialogViews : public AutofillDialogView,
                            public TestableAutofillDialogView,
                            public views::DialogDelegateView,
                            public views::WidgetObserver,
                            public views::ButtonListener,
                            public views::TextfieldController,
                            public views::FocusChangeListener,
                            public views::ComboboxListener,
                            public views::StyledLabelListener {
 public:
  explicit AutofillDialogViews(AutofillDialogViewDelegate* delegate);
  virtual ~AutofillDialogViews();

  // AutofillDialogView implementation:
  virtual void Show() OVERRIDE;
  virtual void Hide() OVERRIDE;
  virtual void UpdateAccountChooser() OVERRIDE;
  virtual void UpdateAutocheckoutStepsArea() OVERRIDE;
  virtual void UpdateButtonStrip() OVERRIDE;
  virtual void UpdateDetailArea() OVERRIDE;
  virtual void UpdateForErrors() OVERRIDE;
  virtual void UpdateNotificationArea() OVERRIDE;
  virtual void UpdateSection(DialogSection section) OVERRIDE;
  virtual void FillSection(DialogSection section,
                           const DetailInput& originating_input) OVERRIDE;
  virtual void GetUserInput(DialogSection section,
                            DetailOutputMap* output) OVERRIDE;
  virtual base::string16 GetCvc() OVERRIDE;
  virtual bool SaveDetailsLocally() OVERRIDE;
  virtual const content::NavigationController* ShowSignIn() OVERRIDE;
  virtual void HideSignIn() OVERRIDE;
  virtual void UpdateProgressBar(double value) OVERRIDE;
  virtual void ModelChanged() OVERRIDE;
  virtual TestableAutofillDialogView* GetTestableView() OVERRIDE;
  virtual void OnSignInResize(const gfx::Size& pref_size) OVERRIDE;

  // TestableAutofillDialogView implementation:
  virtual void SubmitForTesting() OVERRIDE;
  virtual void CancelForTesting() OVERRIDE;
  virtual base::string16 GetTextContentsOfInput(
      const DetailInput& input) OVERRIDE;
  virtual void SetTextContentsOfInput(const DetailInput& input,
                                      const base::string16& contents) OVERRIDE;
  virtual void SetTextContentsOfSuggestionInput(
      DialogSection section,
      const base::string16& text) OVERRIDE;
  virtual void ActivateInput(const DetailInput& input) OVERRIDE;
  virtual gfx::Size GetSize() const OVERRIDE;

  // views::View implementation.
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual void Layout() OVERRIDE;
  virtual void OnBoundsChanged(const gfx::Rect& previous_bounds) OVERRIDE;

  // views::DialogDelegate implementation:
  virtual base::string16 GetWindowTitle() const OVERRIDE;
  virtual void WindowClosing() OVERRIDE;
  virtual void DeleteDelegate() OVERRIDE;
  virtual views::View* CreateOverlayView() OVERRIDE;
  virtual int GetDialogButtons() const OVERRIDE;
  virtual base::string16 GetDialogButtonLabel(ui::DialogButton button) const
      OVERRIDE;
  virtual bool ShouldDefaultButtonBeBlue() const OVERRIDE;
  virtual bool IsDialogButtonEnabled(ui::DialogButton button) const OVERRIDE;
  virtual views::View* CreateExtraView() OVERRIDE;
  virtual views::View* CreateTitlebarExtraView() OVERRIDE;
  virtual views::View* CreateFootnoteView() OVERRIDE;
  virtual bool Cancel() OVERRIDE;
  virtual bool Accept() OVERRIDE;
  virtual views::NonClientFrameView* CreateNonClientFrameView(
      views::Widget* widget) OVERRIDE;

  // views::WidgetObserver implementation:
  virtual void OnWidgetClosing(views::Widget* widget) OVERRIDE;
  virtual void OnWidgetBoundsChanged(views::Widget* widget,
                                     const gfx::Rect& new_bounds) OVERRIDE;

  // views::ButtonListener implementation:
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE;

  // views::TextfieldController implementation:
  virtual void ContentsChanged(views::Textfield* sender,
                               const base::string16& new_contents) OVERRIDE;
  virtual bool HandleKeyEvent(views::Textfield* sender,
                              const ui::KeyEvent& key_event) OVERRIDE;
  virtual bool HandleMouseEvent(views::Textfield* sender,
                                const ui::MouseEvent& key_event) OVERRIDE;

  // views::FocusChangeListener implementation.
  virtual void OnWillChangeFocus(views::View* focused_before,
                                 views::View* focused_now) OVERRIDE;
  virtual void OnDidChangeFocus(views::View* focused_before,
                                views::View* focused_now) OVERRIDE;

  // views::ComboboxListener implementation:
  virtual void OnSelectedIndexChanged(views::Combobox* combobox) OVERRIDE;

  // views::StyledLabelListener implementation:
  virtual void StyledLabelLinkClicked(const ui::Range& range, int event_flags)
      OVERRIDE;

 private:
  // A class that creates and manages a widget for error messages.
  class ErrorBubble : public views::WidgetObserver {
   public:
    ErrorBubble(views::View* anchor, const base::string16& message);
    virtual ~ErrorBubble();

    bool IsShowing();

    // Re-positions the bubble over |anchor_|. If |anchor_| is not visible,
    // the bubble will hide.
    void UpdatePosition();

    virtual void OnWidgetClosing(views::Widget* widget) OVERRIDE;

    views::View* anchor() { return anchor_; }

   private:
    // Calculates and returns the bounds of |widget_|, depending on |anchor_|
    // and |contents_|.
    gfx::Rect GetBoundsForWidget();

    views::Widget* widget_;  // Weak, may be NULL.
    views::View* anchor_;  // Weak.
    // The contents view of |widget_|.
    views::View* contents_;  // Weak.
    ScopedObserver<views::Widget, ErrorBubble> observer_;

    DISALLOW_COPY_AND_ASSIGN(ErrorBubble);
  };

  // A View which displays the currently selected account and lets the user
  // switch accounts.
  class AccountChooser : public views::View,
                         public views::LinkListener,
                         public base::SupportsWeakPtr<AccountChooser> {
   public:
    explicit AccountChooser(AutofillDialogViewDelegate* delegate);
    virtual ~AccountChooser();

    // Updates the view based on the state that |delegate_| reports.
    void Update();

    // views::View implementation.
    virtual bool OnMousePressed(const ui::MouseEvent& event) OVERRIDE;
    virtual void OnMouseReleased(const ui::MouseEvent& event) OVERRIDE;

    // views::LinkListener implementation.
    virtual void LinkClicked(views::Link* source, int event_flags) OVERRIDE;

   private:
    // The icon for the currently in-use account.
    views::ImageView* image_;

    // The label for the currently in-use account.
    views::Label* label_;

    // The drop arrow.
    views::ImageView* arrow_;

    // The sign in link.
    views::Link* link_;

    // The delegate |this| queries for logic and state.
    AutofillDialogViewDelegate* delegate_;

    // Runs the suggestion menu (triggered by each section's |suggested_button|.
    scoped_ptr<views::MenuRunner> menu_runner_;

    DISALLOW_COPY_AND_ASSIGN(AccountChooser);
  };

  // A view which displays an image, optionally some messages and a button. Used
  // for the splash page as well as the Wallet interstitial.
  class OverlayView : public views::View,
                      public ui::AnimationDelegate {
   public:
    // The listener is informed when |button_| is pressed.
    explicit OverlayView(views::ButtonListener* listener);
    virtual ~OverlayView();

    // Returns a height which should be used when the contents view has width
    // |w|. Note that the value returned should be used as the height of the
    // dialog's contents.
    int GetHeightForContentsForWidth(int width);

    // Sets properties that should be displayed.
    void SetState(const DialogOverlayState& state,
                  views::ButtonListener* listener);

    // Fades the view out after a delay.
    void BeginFadeOut();

    // ui::AnimationDelegate implementation:
    virtual void AnimationProgressed(const ui::Animation* animation) OVERRIDE;
    virtual void AnimationEnded(const ui::Animation* animation) OVERRIDE;

    // views::View implementation:
    virtual gfx::Insets GetInsets() const OVERRIDE;
    virtual void Layout() OVERRIDE;
    virtual const char* GetClassName() const OVERRIDE;
    virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;
    virtual void PaintChildren(gfx::Canvas* canvas) OVERRIDE;

   private:
    // Gets the border of the non-client frame view as a BubbleBorder.
    views::BubbleBorder* GetBubbleBorder();

    // Gets the bounds of this view without the frame view's bubble border.
    gfx::Rect ContentBoundsSansBubbleBorder();

    // Child View. Front and center.
    views::ImageView* image_view_;
    // Child View. When visible, below |image_view_|.
    views::View* message_stack_;
    // Child View. When visible, below |message_stack_|.
    views::LabelButton* button_;

    // This MultiAnimation is used to first fade out the contents of the
    // overlay, then fade out the background of the overlay (revealing the
    // dialog behind the overlay). This avoids cross-fade.
    scoped_ptr<ui::MultiAnimation> fade_out_;

    DISALLOW_COPY_AND_ASSIGN(OverlayView);
  };

  // An area for notifications. Some notifications point at the account chooser.
  class NotificationArea : public views::View,
                           public views::ButtonListener {
   public:
    explicit NotificationArea(AutofillDialogViewDelegate* delegate);
    virtual ~NotificationArea();

    // Displays the given notifications.
    void SetNotifications(const std::vector<DialogNotification>& notifications);

    // views::View implementation.
    virtual gfx::Size GetPreferredSize() OVERRIDE;
    virtual const char* GetClassName() const OVERRIDE;
    virtual void PaintChildren(gfx::Canvas* canvas) OVERRIDE;
    virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;

    // views::ButtonListener implementation:
    virtual void ButtonPressed(views::Button* sender,
                               const ui::Event& event) OVERRIDE;

    void set_arrow_centering_anchor(
        const base::WeakPtr<views::View>& arrow_centering_anchor) {
      arrow_centering_anchor_ = arrow_centering_anchor;
    }

   private:
    // Utility function for determining whether an arrow should be drawn
    // pointing at |arrow_centering_anchor_|.
    bool HasArrow();

    // A reference to the delegate/controller than owns this view.
    // Used to report when checkboxes change their values.
    AutofillDialogViewDelegate* delegate_;  // weak

    // The currently showing checkbox, or NULL if none exists.
    views::Checkbox* checkbox_;  // weak

    // If HasArrow() is true, the arrow should point at this.
    base::WeakPtr<views::View> arrow_centering_anchor_;

    std::vector<DialogNotification> notifications_;

    DISALLOW_COPY_AND_ASSIGN(NotificationArea);
  };

  typedef std::map<const DetailInput*, DecoratedTextfield*> TextfieldMap;
  typedef std::map<const DetailInput*, views::Combobox*> ComboboxMap;

  // A view that packs a label on the left and some related controls
  // on the right.
  class SectionContainer : public views::View {
   public:
    SectionContainer(const base::string16& label,
                     views::View* controls,
                     views::Button* proxy_button);
    virtual ~SectionContainer();

    // Sets the visual appearance of the section to active (considered active
    // when showing the menu or hovered by the mouse cursor).
    void SetActive(bool active);

    // Sets whether mouse events should be forwarded to |proxy_button_|.
    void SetForwardMouseEvents(bool forward);

    // views::View implementation.
    virtual void OnMouseMoved(const ui::MouseEvent& event) OVERRIDE;
    virtual void OnMouseEntered(const ui::MouseEvent& event) OVERRIDE;
    virtual void OnMouseExited(const ui::MouseEvent& event) OVERRIDE;
    virtual bool OnMousePressed(const ui::MouseEvent& event) OVERRIDE;
    virtual void OnMouseReleased(const ui::MouseEvent& event) OVERRIDE;

   private:
    // Converts |event| to one suitable for |proxy_button_|.
    static ui::MouseEvent ProxyEvent(const ui::MouseEvent& event);

    // Mouse events on |this| are sent to this button.
    views::Button* proxy_button_;  // Weak reference.

    // When true, mouse events will be forwarded to |proxy_button_|.
    bool forward_mouse_events_;

    DISALLOW_COPY_AND_ASSIGN(SectionContainer);
  };

  // A view that contains a suggestion (such as a known address) and a link to
  // edit the suggestion.
  class SuggestionView : public views::View {
   public:
    explicit SuggestionView(AutofillDialogViews* autofill_dialog);
    virtual ~SuggestionView();

    void SetState(const SuggestionState& state);

    // views::View implementation.
    virtual gfx::Size GetPreferredSize() OVERRIDE;
    virtual int GetHeightForWidth(int width) OVERRIDE;
    virtual void OnBoundsChanged(const gfx::Rect& previous_bounds) OVERRIDE;

    DecoratedTextfield* decorated_textfield() { return decorated_; }

   private:
    // Returns whether there's room to display |state_.vertically_compact_text|
    // without resorting to an ellipsis for a pixel width of |available_width|.
    // Fills in |resulting_height| with the amount of space required to display
    // |vertically_compact_text| or |horizontally_compact_text| as the case may
    // be.
    bool CanUseVerticallyCompactText(int available_width,
                                     int* resulting_height);

    // Sets the display text of the suggestion.
    void SetLabelText(const base::string16& text);

    // Sets the icon which should be displayed ahead of the text.
    void SetIcon(const gfx::Image& image);

    // Shows an auxiliary textfield to the right of the suggestion icon and
    // text. This is currently only used to show a CVC field for the CC section.
    void SetTextfield(const base::string16& placeholder_text,
                      const gfx::Image& icon);

    // The state of |this|.
    SuggestionState state_;

    // This caches preferred heights for given widths. The key is a preferred
    // width, the value is a cached result of CanUseVerticallyCompactText.
    std::map<int, std::pair<bool, int> > calculated_heights_;

    // The label that holds the suggestion description text.
    views::Label* label_;
    // The second (and greater) line of text that describes the suggestion.
    views::Label* label_line_2_;
    // The icon that comes just before |label_|.
    views::ImageView* icon_;
    // The input set by ShowTextfield.
    DecoratedTextfield* decorated_;
    // An "Edit" link that flips to editable inputs rather than suggestion text.
    views::Link* edit_link_;

    DISALLOW_COPY_AND_ASSIGN(SuggestionView);
  };

  // A convenience struct for holding pointers to views within each detail
  // section. None of the member pointers are owned.
  struct DetailsGroup {
    explicit DetailsGroup(DialogSection section);
    ~DetailsGroup();

    // The section this group is associated with.
    const DialogSection section;
    // The view that contains the entire section (label + input).
    SectionContainer* container;
    // The view that allows manual input.
    views::View* manual_input;
    // The textfields in |manual_input|, tracked by their DetailInput.
    TextfieldMap textfields;
    // The comboboxes in |manual_input|, tracked by their DetailInput.
    ComboboxMap comboboxes;
    // The view that holds the text of the suggested data. This will be
    // visible IFF |manual_input| is not visible.
    SuggestionView* suggested_info;
    // The view that allows selecting other data suggestions.
    views::ImageButton* suggested_button;
  };

  // Area for displaying that status of various steps in an Autocheckout flow.
  class AutocheckoutStepsArea : public views::View {
   public:
    AutocheckoutStepsArea();
    virtual ~AutocheckoutStepsArea() {}

    // Display the given steps.
    void SetSteps(const std::vector<DialogAutocheckoutStep>& steps);

   private:
    DISALLOW_COPY_AND_ASSIGN(AutocheckoutStepsArea);
  };

  class AutocheckoutProgressBar : public views::ProgressBar {
   public:
    AutocheckoutProgressBar();
    virtual ~AutocheckoutProgressBar();

   private:
    // Overriden from View
    virtual gfx::Size GetPreferredSize() OVERRIDE;

    DISALLOW_COPY_AND_ASSIGN(AutocheckoutProgressBar);
  };

  typedef std::map<DialogSection, DetailsGroup> DetailGroupMap;

  void InitChildViews();

  // Creates and returns a view that holds all detail sections.
  views::View* CreateDetailsContainer();

  // Creates and returns a view that holds the requesting host and intro text.
  views::View* CreateNotificationArea();

  // Creates and returns a view that holds the main controls of this dialog.
  views::View* CreateMainContainer();

  // Creates a detail section (Shipping, Email, etc.) with the given label,
  // inputs View, and suggestion model. Relevant pointers are stored in |group|.
  void CreateDetailsSection(DialogSection section);

  // Like CreateDetailsSection, but creates the combined billing/cc section,
  // which is somewhat more complicated than the others.
  void CreateBillingSection();

  // Creates the view that holds controls for inputing or selecting data for
  // a given section.
  views::View* CreateInputsContainer(DialogSection section);

  // Creates a grid of textfield views for the given section, and stores them
  // in the appropriate DetailsGroup. The top level View in the hierarchy is
  // returned.
  views::View* InitInputsView(DialogSection section);

  // Updates the given section to match the state provided by |delegate_|. If
  // |clobber_inputs| is true, the current state of the textfields will be
  // ignored, otherwise their contents will be preserved.
  void UpdateSectionImpl(DialogSection section, bool clobber_inputs);

  // Updates the visual state of the given group as per the model.
  void UpdateDetailsGroupState(const DetailsGroup& group);

  // Gets a pointer to the DetailsGroup that's associated with the given section
  // of the dialog.
  DetailsGroup* GroupForSection(DialogSection section);

  // Gets a pointer to the DetailsGroup that's associated with a given |view|.
  // Returns NULL if no DetailsGroup was found.
  DetailsGroup* GroupForView(views::View* view);

  // Sets the visual state for an input to be either valid or invalid. This
  // should work on Comboboxes or DecoratedTextfields. If |message| is empty,
  // the input is valid.
  template<class T>
  void SetValidityForInput(T* input, const base::string16& message);

  // Shows an error bubble pointing at |view| if |view| has a message in
  // |validity_map_|.
  void ShowErrorBubbleForViewIfNecessary(views::View* view);

  // Updates validity of the inputs in |section| with the new validity data.
  void MarkInputsInvalid(DialogSection section,
                         const ValidityData& validity_data);

  // Checks all manual inputs in |group| for validity. Decorates the invalid
  // ones and returns true if all were valid.
  bool ValidateGroup(const DetailsGroup& group, ValidationType type);

  // Checks all manual inputs in the form for validity. Decorates the invalid
  // ones and returns true if all were valid.
  bool ValidateForm();

  // When an input textfield is edited (its contents change) or activated
  // (clicked while focused), this function will inform the delegate that it's
  // time to show a suggestion popup and possibly reset the validity state of
  // the input.
  void TextfieldEditedOrActivated(views::Textfield* textfield, bool was_edit);

  // Updates the views in the button strip.
  void UpdateButtonStripExtraView();

  // Call this when the size of anything in |contents_| might've changed.
  void ContentsPreferredSizeChanged();

  // Gets the textfield view that is shown for the given DetailInput model, or
  // NULL.
  views::Textfield* TextfieldForInput(const DetailInput& input);

  // Gets the combobox view that is shown for the given DetailInput model, or
  // NULL.
  views::Combobox* ComboboxForInput(const DetailInput& input);

  // Called when the details container changes in size or position.
  void DetailsContainerBoundsChanged();

  // The delegate that drives this view. Weak pointer, always non-NULL.
  AutofillDialogViewDelegate* const delegate_;

  // The window that displays |contents_|. Weak pointer; may be NULL when the
  // dialog is closing.
  views::Widget* window_;

  // A DialogSection-keyed map of the DetailGroup structs.
  DetailGroupMap detail_groups_;

  // Somewhere to show notification messages about errors, warnings, or promos.
  NotificationArea* notification_area_;

  // Runs the suggestion menu (triggered by each section's |suggested_button|.
  scoped_ptr<views::MenuRunner> menu_runner_;

  // The view that allows the user to toggle the data source.
  AccountChooser* account_chooser_;

  // A WebView to that navigates to a Google sign-in page to allow the user to
  // sign-in.
  views::WebView* sign_in_webview_;

  // View that wraps |details_container_| and makes it scroll vertically.
  views::ScrollView* scrollable_area_;

  // View to host details sections.
  views::View* details_container_;

  // A view that overlays |this| (for "loading..." messages).
  views::Label* loading_shield_;

  // The view that completely overlays the dialog (used for the splash page).
  OverlayView* overlay_view_;

  // The "Extra view" is on the same row as the dialog buttons.
  views::View* button_strip_extra_view_;

  // This checkbox controls whether new details are saved to the Autofill
  // database. It lives in |extra_view_|.
  views::Checkbox* save_in_chrome_checkbox_;

  // Holds the above checkbox and an associated tooltip icon.
  views::View* save_in_chrome_checkbox_container_;

  // Used to display an image in the button strip extra view.
  views::ImageView* button_strip_image_;

  // View that aren't in the hierarchy but are owned by |this|. Currently
  // just holds the (hidden) country comboboxes.
  ScopedVector<views::View> other_owned_views_;

  // View to host Autocheckout steps.
  AutocheckoutStepsArea* autocheckout_steps_area_;

  // View to host |autocheckout_progress_bar_| and its label.
  views::View* autocheckout_progress_bar_view_;

  // Progress bar for displaying Autocheckout progress.
  AutocheckoutProgressBar* autocheckout_progress_bar_;

  // The view that is appended to the bottom of the dialog, below the button
  // strip. Used to display legal document links.
  views::View* footnote_view_;

  // The legal document text and links.
  views::StyledLabel* legal_document_view_;

  // The focus manager for |window_|.
  views::FocusManager* focus_manager_;

  // The object that manages the error bubble widget.
  scoped_ptr<ErrorBubble> error_bubble_;

  // Map from input view (textfield or combobox) to error string.
  std::map<views::View*, base::string16> validity_map_;

  ScopedObserver<views::Widget, AutofillDialogViews> observer_;

  // Delegate for the sign-in dialog's webview.
  scoped_ptr<AutofillDialogSignInDelegate> sign_in_delegate_;

  DISALLOW_COPY_AND_ASSIGN(AutofillDialogViews);
};

}  // namespace autofill

#endif  // CHROME_BROWSER_UI_VIEWS_AUTOFILL_AUTOFILL_DIALOG_VIEWS_H_
