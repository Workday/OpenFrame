// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_BROWSER_ACTIONS_TOOLBAR_GTK_H_
#define CHROME_BROWSER_UI_GTK_BROWSER_ACTIONS_TOOLBAR_GTK_H_

#include <map>
#include <string>

#include "base/compiler_specific.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/extensions/extension_toolbar_model.h"
#include "chrome/browser/ui/gtk/custom_button.h"
#include "chrome/browser/ui/gtk/menu_gtk.h"
#include "chrome/browser/ui/gtk/overflow_button.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/base/animation/animation_delegate.h"
#include "ui/base/animation/slide_animation.h"
#include "ui/base/gtk/gtk_signal.h"
#include "ui/base/gtk/gtk_signal_registrar.h"
#include "ui/base/gtk/owned_widget_gtk.h"
#include "ui/base/models/simple_menu_model.h"

class Browser;
class BrowserActionButton;
class GtkThemeService;
class Profile;

namespace extensions {
class Extension;
}

typedef struct _GdkDragContext GdkDragContext;
typedef struct _GtkWidget GtkWidget;

class BrowserActionsToolbarGtk : public ExtensionToolbarModel::Observer,
                                 public ui::AnimationDelegate,
                                 public MenuGtk::Delegate,
                                 public ui::SimpleMenuModel::Delegate,
                                 public content::NotificationObserver {
 public:
  explicit BrowserActionsToolbarGtk(Browser* browser);
  virtual ~BrowserActionsToolbarGtk();

  GtkWidget* widget() { return hbox_.get(); }
  GtkWidget* chevron() { return overflow_button_->widget(); }

  // Returns the widget in use by the BrowserActionButton corresponding to
  // |extension|. Used in positioning the ExtensionInstalledBubble for
  // BrowserActions.
  GtkWidget* GetBrowserActionWidget(const extensions::Extension* extension);

  int button_count() { return extension_button_map_.size(); }

  Browser* browser() { return browser_; }

  ExtensionToolbarModel* model() { return model_; }

  // Returns the currently selected tab ID, or -1 if there is none.
  int GetCurrentTabId() const;

  // Update the display of all buttons.
  void Update();

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  bool animating() {
    return resize_animation_.is_animating();
  }

 private:
  friend class BrowserActionButton;

  // Initialize drag and drop.
  void SetupDrags();

  // Query the extensions service for all extensions with browser actions,
  // and create the UI for them.
  void CreateAllButtons();

  // Sets the width of the container and overflow state according to the model.
  void SetContainerWidth();

  // Create the UI for a single browser action. This will stick the button
  // at the end of the toolbar.
  void CreateButtonForExtension(const extensions::Extension* extension,
                                int index);

  // Delete resources associated with UI for a browser action.
  void RemoveButtonForExtension(const extensions::Extension* extension);

  // Change the visibility of widget() based on whether we have any buttons
  // to show.
  void UpdateVisibility();

  // Hide the extension popup, if any.
  void HidePopup();

  // Animate the toolbar to show the given number of icons. This assumes the
  // visibility of the overflow button will not change.
  void AnimateToShowNIcons(int count);

  // Returns true if this extension should be shown in this toolbar. This can
  // return false if we are in an incognito window and the extension is disabled
  // for incognito.
  bool ShouldDisplayBrowserAction(const extensions::Extension* extension);

  // ExtensionToolbarModel::Observer implementation.
  virtual void BrowserActionAdded(const extensions::Extension* extension,
                                  int index) OVERRIDE;
  virtual void BrowserActionRemoved(
      const extensions::Extension* extension) OVERRIDE;
  virtual void BrowserActionMoved(const extensions::Extension* extension,
                                  int index) OVERRIDE;
  virtual void ModelLoaded() OVERRIDE;

  // ui::AnimationDelegate implementation.
  virtual void AnimationProgressed(const ui::Animation* animation) OVERRIDE;
  virtual void AnimationEnded(const ui::Animation* animation) OVERRIDE;

  // SimpleMenuModel::Delegate implementation.
  // In our case, |command_id| is be the index into the model's extension list.
  virtual bool IsCommandIdChecked(int command_id) const OVERRIDE;
  virtual bool IsCommandIdEnabled(int command_id) const OVERRIDE;
  virtual bool GetAcceleratorForCommandId(
      int command_id,
      ui::Accelerator* accelerator) OVERRIDE;
  virtual void ExecuteCommand(int command_id, int event_flags) OVERRIDE;

  // MenuGtk::Delegate implementation.
  virtual void StoppedShowing() OVERRIDE;
  virtual bool AlwaysShowIconForCmd(int command_id) const OVERRIDE;

  // Called by the BrowserActionButton in response to drag-begin.
  void DragStarted(BrowserActionButton* button, GdkDragContext* drag_context);

  // Sets the width of the button area of the toolbar to |new_width|, clamping
  // it to appropriate values.
  void SetButtonHBoxWidth(int new_width);

  // Shows or hides the chevron as appropriate.
  void UpdateChevronVisibility();

  CHROMEGTK_CALLBACK_4(BrowserActionsToolbarGtk, gboolean, OnDragMotion,
                       GdkDragContext*, gint, gint, guint);
  CHROMEGTK_CALLBACK_1(BrowserActionsToolbarGtk, void, OnDragEnd,
                       GdkDragContext*);
  CHROMEGTK_CALLBACK_2(BrowserActionsToolbarGtk, gboolean, OnDragFailed,
                       GdkDragContext*, GtkDragResult);
  CHROMEGTK_CALLBACK_1(BrowserActionsToolbarGtk, void, OnHierarchyChanged,
                       GtkWidget*);
  CHROMEGTK_CALLBACK_1(BrowserActionsToolbarGtk, void, OnSetFocus, GtkWidget*);
  CHROMEGTK_CALLBACK_1(BrowserActionsToolbarGtk, gboolean,
                       OnGripperMotionNotify, GdkEventMotion*);
  CHROMEGTK_CALLBACK_1(BrowserActionsToolbarGtk, gboolean, OnGripperExpose,
                       GdkEventExpose*);
  CHROMEGTK_CALLBACK_1(BrowserActionsToolbarGtk, gboolean,
                       OnGripperEnterNotify, GdkEventCrossing*);
  CHROMEGTK_CALLBACK_1(BrowserActionsToolbarGtk, gboolean,
                       OnGripperLeaveNotify, GdkEventCrossing*);
  CHROMEGTK_CALLBACK_1(BrowserActionsToolbarGtk, gboolean,
                       OnGripperButtonRelease, GdkEventButton*);
  CHROMEGTK_CALLBACK_1(BrowserActionsToolbarGtk, gboolean,
                       OnGripperButtonPress, GdkEventButton*);
  // The overflow button is pressed.
  CHROMEGTK_CALLBACK_1(BrowserActionsToolbarGtk, gboolean,
                       OnOverflowButtonPress, GdkEventButton*);
  // The user presses a mouse button over the popped up overflow menu.
  CHROMEGTK_CALLBACK_1(BrowserActionsToolbarGtk, gboolean,
                       OnOverflowMenuButtonPress, GdkEventButton*);
  CHROMEGTK_CALLBACK_0(BrowserActionsToolbarGtk, void, OnButtonShowOrHide);

  Browser* browser_;

  Profile* profile_;
  GtkThemeService* theme_service_;

  ExtensionToolbarModel* model_;

  // Contains the drag gripper, browser action buttons, and overflow chevron.
  ui::OwnedWidgetGtk hbox_;

  // Contains the browser action buttons.
  ui::OwnedWidgetGtk button_hbox_;

  // The overflow button for chrome theme mode.
  scoped_ptr<CustomDrawButton> overflow_button_;
  // The separator just next to the overflow button. Only shown in GTK+ theme
  // mode. In Chrome theme mode, the overflow button has a separator built in.
  ui::OwnedWidgetGtk separator_;
  scoped_ptr<MenuGtk> overflow_menu_;
  scoped_ptr<ui::SimpleMenuModel> overflow_menu_model_;
  ui::OwnedWidgetGtk overflow_area_;
  // A widget for adding extra padding to the left of the overflow button.
  ui::OwnedWidgetGtk overflow_alignment_;

  // The button that is currently being dragged, or NULL.
  BrowserActionButton* drag_button_;

  // The new position of the button in the drag, or -1.
  int drop_index_;

  // Map from extension ID to BrowserActionButton, which is a wrapper for
  // a chrome button and related functionality. There should be one entry
  // for every extension that has a browser action.
  typedef std::map<std::string, linked_ptr<BrowserActionButton> >
      ExtensionButtonMap;
  ExtensionButtonMap extension_button_map_;

  // We use this animation for the smart resizing of the toolbar.
  ui::SlideAnimation resize_animation_;
  // This is the final width we are animating towards.
  int desired_width_;
  // This is the width we were at when we started animating.
  int start_width_;

  ui::GtkSignalRegistrar signals_;

  content::NotificationRegistrar registrar_;

  base::WeakPtrFactory<BrowserActionsToolbarGtk> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(BrowserActionsToolbarGtk);
};

#endif  // CHROME_BROWSER_UI_GTK_BROWSER_ACTIONS_TOOLBAR_GTK_H_
