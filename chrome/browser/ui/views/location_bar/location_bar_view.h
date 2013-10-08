// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_LOCATION_BAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_LOCATION_BAR_VIEW_H_

#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/prefs/pref_member.h"
#include "chrome/browser/extensions/extension_context_menu_model.h"
#include "chrome/browser/search_engines/template_url_service_observer.h"
#include "chrome/browser/ui/omnibox/location_bar.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_controller.h"
#include "chrome/browser/ui/search/search_model_observer.h"
#include "chrome/browser/ui/toolbar/toolbar_model.h"
#include "chrome/browser/ui/views/dropdown_bar_host.h"
#include "chrome/browser/ui/views/dropdown_bar_host_delegate.h"
#include "chrome/browser/ui/views/extensions/extension_popup.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/gfx/font.h"
#include "ui/gfx/rect.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/drag_controller.h"

#if defined(USE_AURA)
#include "ui/compositor/layer_animation_observer.h"
#endif

class ActionBoxButtonView;
class CommandUpdater;
class ContentSettingBubbleModelDelegate;
class ContentSettingImageView;
class EVBubbleView;
class ExtensionAction;
class GURL;
class GeneratedCreditCardView;
class InstantController;
class KeywordHintView;
class LocationIconView;
class OpenPDFInReaderView;
class PageActionWithBadgeView;
class PageActionImageView;
class Profile;
class ScriptBubbleIconView;
class SelectedKeywordView;
class StarView;
class TemplateURLService;
class ZoomView;

namespace views {
class BubbleDelegateView;
class ImageButton;
class Label;
class Widget;
}

/////////////////////////////////////////////////////////////////////////////
//
// LocationBarView class
//
//   The LocationBarView class is a View subclass that paints the background
//   of the URL bar strip and contains its content.
//
/////////////////////////////////////////////////////////////////////////////
class LocationBarView : public LocationBar,
                        public LocationBarTesting,
                        public views::View,
                        public views::ButtonListener,
                        public views::DragController,
                        public OmniboxEditController,
                        public DropdownBarHostDelegate,
                        public TemplateURLServiceObserver,
                        public content::NotificationObserver,
                        public SearchModelObserver {
 public:
  // The location bar view's class name.
  static const char kViewClassName[];

  // DropdownBarHostDelegate:
  virtual void SetFocusAndSelection(bool select_all) OVERRIDE;
  virtual void SetAnimationOffset(int offset) OVERRIDE;

  // Returns the offset used while animating.
  int animation_offset() const { return animation_offset_; }

  class Delegate {
   public:
    // Should return the current web contents.
    virtual content::WebContents* GetWebContents() const = 0;

    // Returns the InstantController, or NULL if there isn't one.
    virtual InstantController* GetInstant() = 0;

    // Creates Widget for the given delegate.
    virtual views::Widget* CreateViewsBubble(
        views::BubbleDelegateView* bubble_delegate) = 0;

    // Creates PageActionImageView. Caller gets an ownership.
    virtual PageActionImageView* CreatePageActionImageView(
        LocationBarView* owner,
        ExtensionAction* action) = 0;

    // Returns ContentSettingBubbleModelDelegate.
    virtual ContentSettingBubbleModelDelegate*
        GetContentSettingBubbleModelDelegate() = 0;

    // Shows permissions and settings for the given web contents.
    virtual void ShowWebsiteSettings(content::WebContents* web_contents,
                                     const GURL& url,
                                     const content::SSLStatus& ssl) = 0;

    // Called by the location bar view when the user starts typing in the edit.
    // This forces our security style to be UNKNOWN for the duration of the
    // editing.
    virtual void OnInputInProgress(bool in_progress) = 0;

   protected:
    virtual ~Delegate() {}
  };

  enum ColorKind {
    BACKGROUND = 0,
    TEXT,
    SELECTED_TEXT,
    DEEMPHASIZED_TEXT,
    SECURITY_TEXT,
  };

  LocationBarView(Browser* browser,
                  Profile* profile,
                  CommandUpdater* command_updater,
                  ToolbarModel* model,
                  Delegate* delegate,
                  bool is_popup_mode);

  virtual ~LocationBarView();

  // Uses GetBuiltInHorizontalPaddingForChildViews() to optionally add
  // additional padding (via an empty border) to |view|. This should be called
  // during creation on all child views which are potentially touchable so that
  // when touch is enabled they will have sufficient padding.
  static void InitTouchableLocationBarChildView(views::View* view);

  // Initializes the LocationBarView.
  void Init();

  // True if this instance has been initialized by calling Init, which can only
  // be called when the receiving instance is attached to a view container.
  bool IsInitialized() const;

  // Returns the appropriate color for the desired kind, based on the user's
  // system theme.
  SkColor GetColor(ToolbarModel::SecurityLevel security_level,
                   ColorKind kind) const;

  // Updates the location bar.  We also reset the bar's permanent text and
  // security style, and, if |tab_for_state_restoring| is non-NULL, also restore
  // saved state that the tab holds.
  void Update(const content::WebContents* tab_for_state_restoring);

  // Returns corresponding profile.
  Profile* profile() const { return profile_; }

  // Returns the delegate.
  Delegate* delegate() const { return delegate_; }

  // See comment in browser_window.h for more info.
  void ZoomChangedForActiveTab(bool can_show_bubble);

  // The zoom icon view. It may not be visible.
  ZoomView* zoom_view() { return zoom_view_; }

  // Sets |preview_enabled| for the PageAction View associated with this
  // |page_action|. If |preview_enabled| is true, the view will display the
  // PageActions icon even though it has not been activated by the extension.
  // This is used by the ExtensionInstalledBubble to preview what the icon
  // will look like for the user upon installation of the extension.
  void SetPreviewEnabledPageAction(ExtensionAction* page_action,
                                   bool preview_enabled);

  // Retrieves the PageAction View which is associated with |page_action|.
  views::View* GetPageActionView(ExtensionAction* page_action);

  // Toggles the star on or off.
  void SetStarToggled(bool on);

  // Returns the star view. It may not be visible.
  StarView* star_view() { return star_view_; }

  // Shows the bookmark prompt.
  void ShowBookmarkPrompt();

  // Returns the screen coordinates of the location entry (where the URL text
  // appears, not where the icons are shown).
  gfx::Point GetLocationEntryOrigin() const;

  // Shows |text| as an inline autocompletion.  This is useful for IMEs, where
  // we can't show the autocompletion inside the actual OmniboxView.  See
  // comments on |ime_inline_autocomplete_view_|.
  void SetImeInlineAutocompletion(const string16& text);

  // Invoked from OmniboxViewWin to show gray text autocompletion.
  void SetGrayTextAutocompletion(const string16& text);

  // Returns the current gray text autocompletion.
  string16 GetGrayTextAutocompletion() const;

  // Sets whether the location entry can accept focus.
  void SetLocationEntryFocusable(bool focusable);

  // Returns true if the location entry is focusable and visible in
  // the root view.
  bool IsLocationEntryFocusableInRootView() const;

  // Sizing functions
  virtual gfx::Size GetPreferredSize() OVERRIDE;

  // Layout and Painting functions
  virtual void Layout() OVERRIDE;
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;

  // No focus border for the location bar, the caret is enough.
  virtual void OnPaintFocusBorder(gfx::Canvas* canvas) OVERRIDE { }

  // Set if we should show a focus rect while the location entry field is
  // focused. Used when the toolbar is in full keyboard accessibility mode.
  // Repaints if necessary.
  virtual void SetShowFocusRect(bool show);

  // Select all of the text. Needed when the user tabs through controls
  // in the toolbar in full keyboard accessibility mode.
  virtual void SelectAll();

#if defined(OS_WIN) && !defined(USE_AURA)
  // Event Handlers
  virtual bool OnMousePressed(const ui::MouseEvent& event) OVERRIDE;
  virtual bool OnMouseDragged(const ui::MouseEvent& event) OVERRIDE;
  virtual void OnMouseReleased(const ui::MouseEvent& event) OVERRIDE;
  virtual void OnMouseCaptureLost() OVERRIDE;
#endif

  LocationIconView* location_icon_view() { return location_icon_view_; }
  const LocationIconView* location_icon_view() const {
    return location_icon_view_;
  }

  views::View* location_entry_view() const { return location_entry_view_; }

  views::View* generated_credit_card_view();

  // OmniboxEditController:
  virtual void OnAutocompleteAccept(const GURL& url,
                                    WindowOpenDisposition disposition,
                                    content::PageTransition transition,
                                    const GURL& alternate_nav_url) OVERRIDE;
  virtual void OnChanged() OVERRIDE;
  virtual void OnSelectionBoundsChanged() OVERRIDE;
  virtual void OnInputInProgress(bool in_progress) OVERRIDE;
  virtual void OnKillFocus() OVERRIDE;
  virtual void OnSetFocus() OVERRIDE;
  virtual gfx::Image GetFavicon() const OVERRIDE;
  virtual string16 GetTitle() const OVERRIDE;
  virtual InstantController* GetInstant() OVERRIDE;
  virtual content::WebContents* GetWebContents() const OVERRIDE;

  // views::View:
  virtual const char* GetClassName() const OVERRIDE;
  virtual bool SkipDefaultKeyEventProcessing(
      const ui::KeyEvent& event) OVERRIDE;
  virtual void GetAccessibleState(ui::AccessibleViewState* state) OVERRIDE;
  virtual bool HasFocus() const OVERRIDE;
  virtual void OnBoundsChanged(const gfx::Rect& previous_bounds) OVERRIDE;

  // views::ButtonListener:
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE;

  // views::DragController:
  virtual void WriteDragDataForView(View* sender,
                                    const gfx::Point& press_pt,
                                    OSExchangeData* data) OVERRIDE;
  virtual int GetDragOperationsForView(View* sender,
                                       const gfx::Point& p) OVERRIDE;
  virtual bool CanStartDragForView(View* sender,
                                   const gfx::Point& press_pt,
                                   const gfx::Point& p) OVERRIDE;

  // LocationBar:
  virtual void ShowFirstRunBubble() OVERRIDE;
  virtual string16 GetInputString() const OVERRIDE;
  virtual WindowOpenDisposition GetWindowOpenDisposition() const OVERRIDE;
  virtual content::PageTransition GetPageTransition() const OVERRIDE;
  virtual void AcceptInput() OVERRIDE;
  virtual void FocusLocation(bool select_all) OVERRIDE;
  virtual void FocusSearch() OVERRIDE;
  virtual void UpdateContentSettingsIcons() OVERRIDE;
  virtual void UpdatePageActions() OVERRIDE;
  virtual void InvalidatePageActions() OVERRIDE;
  virtual void UpdateOpenPDFInReaderPrompt() OVERRIDE;
  virtual void UpdateGeneratedCreditCardView() OVERRIDE;
  virtual void SaveStateToContents(content::WebContents* contents) OVERRIDE;
  virtual void Revert() OVERRIDE;
  virtual const OmniboxView* GetLocationEntry() const OVERRIDE;
  virtual OmniboxView* GetLocationEntry() OVERRIDE;
  virtual LocationBarTesting* GetLocationBarForTesting() OVERRIDE;

  // LocationBarTesting:
  virtual int PageActionCount() OVERRIDE;
  virtual int PageActionVisibleCount() OVERRIDE;
  virtual ExtensionAction* GetPageAction(size_t index) OVERRIDE;
  virtual ExtensionAction* GetVisiblePageAction(size_t index) OVERRIDE;
  virtual void TestPageActionPressed(size_t index) OVERRIDE;
  virtual bool GetBookmarkStarVisibility() OVERRIDE;

  // TemplateURLServiceObserver:
  virtual void OnTemplateURLServiceChanged() OVERRIDE;

  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // SearchModelObserver:
  virtual void ModelChanged(const SearchModel::State& old_state,
                            const SearchModel::State& new_state) OVERRIDE;

  // Returns the height of the control without the top and bottom
  // edges(i.e.  the height of the edit control inside).  If
  // |use_preferred_size| is true this will be the preferred height,
  // otherwise it will be the current height.
  int GetInternalHeight(bool use_preferred_size);

  // Returns the position and width that the popup should be, and also the left
  // edge that the results should align themselves to (which will leave some
  // border on the left of the popup).
  void GetOmniboxPopupPositioningInfo(gfx::Point* top_left_screen_coord,
                                      int* popup_width,
                                      int* left_margin,
                                      int* right_margin);

  // Space between items in the location bar, as well as between items and the
  // edges.
  static int GetItemPadding();

  // Thickness of the edges of the omnibox background images, in normal mode.
  static const int kNormalEdgeThickness;
  // The same, but for popup mode.
  static const int kPopupEdgeThickness;
  // Amount of padding built into the standard omnibox icons.
  static const int kIconInternalPadding;
  // Space between the edge and a bubble.
  static const int kBubblePadding;

 protected:
  virtual void OnFocus() OVERRIDE;

 private:
  typedef std::vector<ContentSettingImageView*> ContentSettingViews;

  friend class PageActionImageView;
  friend class PageActionWithBadgeView;
  typedef std::vector<PageActionWithBadgeView*> PageActionViews;

  // Returns the number of pixels of built-in padding to the left and right for
  // child views. This is nonzero when touch UI is enabled so as to space out
  // child views for easier targeting. See InitTouchableLocationBarChildView().
  static int GetBuiltInHorizontalPaddingForChildViews();

  // Returns the thickness of any visible left and right edge, in pixels.
  int GetHorizontalEdgeThickness() const;

  // The same, but for the top and bottom edges.
  int vertical_edge_thickness() const {
    return is_popup_mode_ ? kPopupEdgeThickness : kNormalEdgeThickness;
  }

  // Update the visibility state of the Content Blocked icons to reflect what is
  // actually blocked on the current page.
  void RefreshContentSettingViews();

  // Delete all page action views that we have created.
  void DeletePageActionViews();

  // Update the views for the Page Actions, to reflect state changes for
  // PageActions.
  void RefreshPageActionViews();

  // Returns the number of scripts currently running on the page.
  size_t ScriptBubbleScriptsRunning();

  // Update the Script Bubble Icon, to reflect the number of content scripts
  // running on the page.
  void RefreshScriptBubble();

  // Update the view for the zoom icon based on the current tab's zoom.
  void RefreshZoomView();

  // Sets the visibility of view to new_vis.
  void ToggleVisibility(bool new_vis, views::View* view);

#if !defined(USE_AURA)
  // Helper for the Mouse event handlers that does all the real work.
  void OnMouseEvent(const ui::MouseEvent& event, UINT msg);
#endif

  // Returns true if the suggest text is valid.
  bool HasValidSuggestText() const;

  // Helper to show the first run info bubble.
  void ShowFirstRunBubbleInternal();

  // Draw backgrounds and borders for page actions.  Must be called
  // after layout, so the |page_action_views_| have their bounds.
  void PaintPageActionBackgrounds(gfx::Canvas* canvas);

  // The Browser this LocationBarView is in.  Note that at least
  // chromeos::SimpleWebViewDialog uses a LocationBarView outside any browser
  // window, so this may be NULL.
  Browser* browser_;

  // The Autocomplete Edit field.
  scoped_ptr<OmniboxView> location_entry_;

  // The profile which corresponds to this View.
  Profile* profile_;

  // Command updater which corresponds to this View.
  CommandUpdater* command_updater_;

  // The model.
  ToolbarModel* model_;

  // Our delegate.
  Delegate* delegate_;

  // This is the string of text from the autocompletion session that the user
  // entered or selected.
  string16 location_input_;

  // The user's desired disposition for how their input should be opened
  WindowOpenDisposition disposition_;

  // The transition type to use for the navigation
  content::PageTransition transition_;

  // An object used to paint the normal-mode background.
  scoped_ptr<views::Painter> background_border_painter_;
  scoped_ptr<views::Painter> background_filling_painter_;

  // An icon to the left of the edit field.
  LocationIconView* location_icon_view_;

  // A bubble displayed for EV HTTPS sites.
  EVBubbleView* ev_bubble_view_;

  // Location_entry view
  views::View* location_entry_view_;

  // A view to show inline autocompletion when an IME is active.  In this case,
  // we shouldn't change the text or selection inside the OmniboxView itself,
  // since this will conflict with the IME's control over the text.  So instead
  // we show any autocompletion in a separate field after the OmniboxView.
  views::Label* ime_inline_autocomplete_view_;

  // The following views are used to provide hints and remind the user as to
  // what is going in the edit. They are all added a children of the
  // LocationBarView. At most one is visible at a time. Preference is
  // given to the keyword_view_, then hint_view_.
  // These autocollapse when the edit needs the room.

  // Shown if the user has selected a keyword.
  SelectedKeywordView* selected_keyword_view_;

  // View responsible for showing suggested text. This is NULL when there is no
  // suggested text.
  views::Label* suggested_text_view_;

  // Shown if the selected url has a corresponding keyword.
  KeywordHintView* keyword_hint_view_;

  // The voice search icon.
  views::ImageButton* mic_search_view_;

  // The content setting views.
  ContentSettingViews content_setting_views_;

  // The zoom icon.
  ZoomView* zoom_view_;

  // A bubble that shows after successfully generating a new credit card number.
  GeneratedCreditCardView* generated_credit_card_view_;

  // The icon to open a PDF in Reader.
  OpenPDFInReaderView* open_pdf_in_reader_view_;

  // The current page actions.
  std::vector<ExtensionAction*> page_actions_;

  // The page action icon views.
  PageActionViews page_action_views_;

  // The script bubble.
  ScriptBubbleIconView* script_bubble_icon_view_;

  // The star.
  StarView* star_view_;

  // Whether we're in popup mode.
  const bool is_popup_mode_;

  // True if we should show a focus rect while the location entry field is
  // focused. Used when the toolbar is in full keyboard accessibility mode.
  bool show_focus_rect_;

  // This is in case we're destroyed before the model loads. We need to make
  // Add/RemoveObserver calls.
  TemplateURLService* template_url_service_;

  // Tracks this preference to determine whether bookmark editing is allowed.
  BooleanPrefMember edit_bookmarks_enabled_;

  // While animating, the host clips the widget and draws only the bottom
  // part of it. The view needs to know the pixel offset at which we are drawing
  // the widget so that we can draw the curved edges that attach to the toolbar
  // in the right location.
  int animation_offset_;

  // Used to register for notifications received by NotificationObserver.
  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(LocationBarView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_LOCATION_BAR_VIEW_H_
