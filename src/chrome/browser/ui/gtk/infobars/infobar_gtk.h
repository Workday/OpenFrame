// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_INFOBARS_INFOBAR_GTK_H_
#define CHROME_BROWSER_UI_GTK_INFOBARS_INFOBAR_GTK_H_

#include <gtk/gtk.h>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/infobars/infobar.h"
#include "chrome/browser/infobars/infobar_delegate.h"
#include "chrome/browser/ui/gtk/menu_gtk.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/gtk/gtk_signal.h"
#include "ui/base/gtk/owned_widget_gtk.h"

class CustomDrawButton;
class GtkThemeService;

namespace ui {
class GtkSignalRegistrar;
class MenuModel;
}

class InfoBarGtk : public InfoBar,
                   public content::NotificationObserver {
 public:
  // Conversion from cairo colors to SkColor.
  typedef void (InfoBarGtk::*ColorGetter)(InfoBarDelegate::Type,
                                          double* r, double* g, double* b);

  InfoBarGtk(InfoBarService* owner, InfoBarDelegate* delegate);
  virtual ~InfoBarGtk();

  // Must be called before we try to show the infobar.  Inits any widgets and
  // related objects necessary.  This must be called only once during the
  // infobar's life.
  //
  // NOTE: Subclasses who need to init widgets should override this function and
  // explicitly call their parent's implementation first, then continue with
  // further work they need to do.  Failing to call the parent implementation
  // first (or at all), or setting up widgets in the constructor instead of
  // here, will lead to bad side effects like crashing or having this function
  // get called repeatedly.
  virtual void InitWidgets();

  // Get the top level native GTK widget for this infobar.
  GtkWidget* widget() { return widget_.get(); }

  GdkColor GetBorderColor() const;

  // Returns the target height of the infobar if the bar is animating,
  // otherwise 0. We care about this number since we want to prevent
  // unnecessary renderer repaints while animating.
  int AnimatingHeight() const;

  SkColor ConvertGetColor(ColorGetter getter);

  // Retrieves the component colors for the infobar's background
  // gradient. (This varies by infobars and can be animated to change).
  virtual void GetTopColor(InfoBarDelegate::Type type,
                           double* r, double* g, double* b);
  virtual void GetBottomColor(InfoBarDelegate::Type type,
                              double* r, double* g, double* b);

 protected:
  // Spacing after message (and before buttons).
  static const int kEndOfLabelSpacing;

  // InfoBar:
  virtual void PlatformSpecificShow(bool animate) OVERRIDE;
  virtual void PlatformSpecificOnCloseSoon() OVERRIDE;
  virtual void PlatformSpecificOnHeightsRecalculated() OVERRIDE;

  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Styles the close button as if we're doing Chrome-stlye widget rendering.
  void ForceCloseButtonToUseChromeTheme();

  GtkWidget* hbox() { return hbox_; }

  // Returns the signal registrar for this infobar. All signals representing
  // user actions on visible widgets must go through this registrar!
  ui::GtkSignalRegistrar* signals() { return signals_.get(); }

  // Creates a label with the appropriate font and color for the current
  // gtk-theme state. It is InfoBarGtk's responsibility to observe browser
  // theme changes and update the label's state.
  GtkWidget* CreateLabel(const std::string& text);

  // Creates a link button with the appropriate current gtk-theme state.
  GtkWidget* CreateLinkButton(const std::string& text);

  // Builds a button with an arrow in it to emulate the menu-button style from
  // the windows version.
  static GtkWidget* CreateMenuButton(const std::string& text);

  // Adds |display_text| to the infobar. If |link_text| is not empty, it is
  // rendered as a hyperlink and inserted into |display_text| at |link_offset|,
  // or right aligned in the infobar if |link_offset| is |npos|. If a link is
  // supplied, |link_callback| must not be null. It will be invoked on click.
  void AddLabelWithInlineLink(const string16& display_text,
                              const string16& link_text,
                              size_t link_offset,
                              GCallback callback);

  // Shows the menu with |model| with the context of |sender|.
  void ShowMenuWithModel(GtkWidget* sender,
                         MenuGtk::Delegate* delegate,
                         ui::MenuModel* model);

 private:
  void GetBackgroundColor(SkColor color, double* r, double* g, double* b);
  void UpdateBorderColor();

  CHROMEGTK_CALLBACK_0(InfoBarGtk, void, OnCloseButton);
  CHROMEGTK_CALLBACK_1(InfoBarGtk, gboolean, OnBackgroundExpose,
                       GdkEventExpose*);
  CHROMEGTK_CALLBACK_2(InfoBarGtk, void, OnChildSizeRequest, GtkWidget*,
                       GtkRequisition*);

  // A GtkExpandedContainer that contains |bg_box_| so we can vary the height of
  // the infobar.
  ui::OwnedWidgetGtk widget_;

  // The second highest widget in the hierarchy (after the |widget_|).
  GtkWidget* bg_box_;

  // The hbox that holds infobar elements (button, text, icon, etc.).
  GtkWidget* hbox_;

  // The x that closes the bar.
  scoped_ptr<CustomDrawButton> close_button_;

  // The theme provider, used for getting border colors.
  GtkThemeService* theme_service_;

  content::NotificationRegistrar registrar_;

  // A list of signals which we clear out once we're closing.
  scoped_ptr<ui::GtkSignalRegistrar> signals_;

  // The current menu displayed. Can be null. We own this on the base class so
  // we can cancel the menu while we're closing.
  scoped_ptr<MenuGtk> menu_;

  DISALLOW_COPY_AND_ASSIGN(InfoBarGtk);
};

#endif  // CHROME_BROWSER_UI_GTK_INFOBARS_INFOBAR_GTK_H_
