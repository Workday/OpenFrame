// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TOOLBAR_TOOLBAR_ACTION_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_TOOLBAR_TOOLBAR_ACTION_VIEW_H_

#include "base/callback.h"
#include "chrome/browser/ui/views/toolbar/toolbar_action_view_delegate_views.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/button/menu_button_listener.h"
#include "ui/views/drag_controller.h"
#include "ui/views/view.h"

class ExtensionAction;
class Profile;

namespace extensions {
class Extension;
}

namespace gfx {
class Image;
}

namespace views {
class MenuItemView;
class MenuRunner;
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarActionView
// A wrapper around a ToolbarActionViewController to display a toolbar action
// action in the BrowserActionsContainer.
class ToolbarActionView : public views::MenuButton,
                          public ToolbarActionViewDelegateViews,
                          public views::MenuButtonListener,
                          public views::ContextMenuController,
                          public content::NotificationObserver,
                          public views::InkDropHost {
 public:
  // Need DragController here because ToolbarActionView could be
  // dragged/dropped.
  class Delegate : public views::DragController {
   public:
    // Returns the current web contents.
    virtual content::WebContents* GetCurrentWebContents() = 0;

    // Whether the container for this button is shown inside a menu.
    virtual bool ShownInsideMenu() const = 0;

    // Notifies that a drag completed.
    virtual void OnToolbarActionViewDragDone() = 0;

    // Returns the view of the toolbar actions overflow menu to use as a
    // reference point for a popup when this view isn't visible.
    virtual views::MenuButton* GetOverflowReferenceView() = 0;

    // Notifies the delegate that the mouse entered the view.
    virtual void OnMouseEnteredToolbarActionView() = 0;

   protected:
    ~Delegate() override {}
  };

  // Callback type used for testing.
  using ContextMenuCallback = base::Callback<void(ToolbarActionView*)>;

  ToolbarActionView(ToolbarActionViewController* view_controller,
                    Profile* profile,
                    Delegate* delegate);
  ~ToolbarActionView() override;

  // views::MenuButton:
  void GetAccessibleState(ui::AXViewState* state) override;
  scoped_ptr<views::LabelButtonBorder> CreateDefaultBorder() const override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  bool ShouldEnterPushedState(const ui::Event& event) override;

  // ToolbarActionViewDelegateViews:
  content::WebContents* GetCurrentWebContents() const override;
  void UpdateState() override;

  // views::MenuButtonListener:
  void OnMenuButtonClicked(views::View* sender,
                           const gfx::Point& point) override;

  // content::NotificationObserver:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  // views::InkDropHost:
  void AddInkDropLayer(ui::Layer* ink_drop_layer) override;
  void RemoveInkDropLayer(ui::Layer* ink_drop_layer) override;

  ToolbarActionViewController* view_controller() {
    return view_controller_;
  }

  // Returns button icon so it can be accessed during tests.
  gfx::ImageSkia GetIconForTest();

  bool wants_to_run_for_testing() const { return wants_to_run_; }

  // Set a callback to be called directly before the context menu is shown.
  // The toolbar action opening the menu will be passed in.
  static void set_context_menu_callback_for_testing(
      ContextMenuCallback* callback);

  views::MenuItemView* menu_for_testing() { return menu_; }

 private:
  // views::MenuButton:
  gfx::Size GetPreferredSize() const override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnGestureEvent(ui::GestureEvent* event) override;
  void OnDragDone() override;
  void ViewHierarchyChanged(
      const ViewHierarchyChangedDetails& details) override;

  // ToolbarActionViewDelegateViews:
  views::View* GetAsView() override;
  views::FocusManager* GetFocusManagerForAccelerator() override;
  views::View* GetReferenceViewForPopup() override;
  bool IsMenuRunning() const override;
  void OnPopupShown(bool by_user) override;
  void OnPopupClosed() override;

  // views::ContextMenuController:
  void ShowContextMenuForView(views::View* source,
                              const gfx::Point& point,
                              ui::MenuSourceType source_type) override;

  // views::InkDropHost:
  gfx::Point CalculateInkDropCenter() const override;

  // Shows the context menu (if one exists) for the toolbar action.
  void DoShowContextMenu(ui::MenuSourceType source_type);

  // Closes the currently-active menu, if needed. This is the case when there
  // is an active menu that wouldn't close automatically when a new one is
  // opened.
  // Returns true if a menu was closed, false otherwise.
  bool CloseActiveMenuIfNeeded();

  // A lock to keep the MenuButton pressed when a menu or popup is visible.
  scoped_ptr<views::MenuButton::PressedLock> pressed_lock_;

  // The controller for this toolbar action view.
  ToolbarActionViewController* view_controller_;

  // The associated profile.
  Profile* profile_;

  // Delegate that usually represents a container for ToolbarActionView.
  Delegate* delegate_;

  // Used to make sure we only register the command once.
  bool called_register_command_;

  // The cached value of whether or not the action wants to run on the current
  // tab.
  bool wants_to_run_;

  // Responsible for running the menu.
  scoped_ptr<views::MenuRunner> menu_runner_;

  // The root MenuItemView for the context menu, or null if no menu is being
  // shown.
  views::MenuItemView* menu_;

  // If non-null, this is the next toolbar action context menu that wants to run
  // once the current owner (this one) is done.
  base::Closure followup_context_menu_task_;

  // The time the popup was last closed.
  base::TimeTicks popup_closed_time_;

  content::NotificationRegistrar registrar_;

  base::WeakPtrFactory<ToolbarActionView> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ToolbarActionView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_TOOLBAR_TOOLBAR_ACTION_VIEW_H_
