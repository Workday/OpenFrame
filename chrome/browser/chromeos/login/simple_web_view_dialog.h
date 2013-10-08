// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_SIMPLE_WEB_VIEW_DIALOG_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_SIMPLE_WEB_VIEW_DIALOG_H_

#include <string>
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/command_updater_delegate.h"
#include "chrome/browser/ui/chrome_web_modal_dialog_manager_delegate.h"
#include "chrome/browser/ui/toolbar/toolbar_model_delegate.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents_delegate.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"

class CommandUpdater;
class Profile;
class ReloadButton;
class ToolbarModel;

namespace views {
class WebView;
class Widget;
}

namespace chromeos {

class StubBubbleModelDelegate;

// View class which shows the light version of the toolbar and the web contents.
// Light version of the toolbar includes back, forward buttons and location
// bar. Location bar is shown in read only mode, because this view is designed
// to be used for sign in to captive portal on login screen (when Browser
// isn't running).
class SimpleWebViewDialog : public views::ButtonListener,
                            public views::WidgetDelegateView,
                            public LocationBarView::Delegate,
                            public ToolbarModelDelegate,
                            public CommandUpdaterDelegate,
                            public content::PageNavigator,
                            public content::WebContentsDelegate,
                            public ChromeWebModalDialogManagerDelegate,
                            public web_modal::WebContentsModalDialogHost {
 public:
  explicit SimpleWebViewDialog(Profile* profile);
  virtual ~SimpleWebViewDialog();

  // Starts loading.
  void StartLoad(const GURL& gurl);

  // Inits view. Should be attached to a Widget before call.
  void Init();

  // Overridden from views::View:
  virtual void Layout() OVERRIDE;

  // Overridden from views::WidgetDelegate:
  virtual views::View* GetContentsView() OVERRIDE;
  virtual views::View* GetInitiallyFocusedView() OVERRIDE;

  // Implements views::ButtonListener:
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE;

  // Implements content::PageNavigator:
  virtual content::WebContents* OpenURL(
      const content::OpenURLParams& params) OVERRIDE;

  // Implements content::WebContentsDelegate:
  virtual void LoadingStateChanged(content::WebContents* source) OVERRIDE;

  // Implements LocationBarView::Delegate:
  virtual void NavigationStateChanged(const content::WebContents* source,
                                      unsigned changed_flags) OVERRIDE;
  virtual content::WebContents* GetWebContents() const OVERRIDE;
  virtual InstantController* GetInstant() OVERRIDE;
  virtual views::Widget* CreateViewsBubble(
      views::BubbleDelegateView* bubble_delegate) OVERRIDE;
  virtual PageActionImageView* CreatePageActionImageView(
      LocationBarView* owner,
      ExtensionAction* action) OVERRIDE;
  virtual ContentSettingBubbleModelDelegate*
  GetContentSettingBubbleModelDelegate() OVERRIDE;
  virtual void ShowWebsiteSettings(content::WebContents* web_contents,
                                   const GURL& url,
                                   const content::SSLStatus& ssl) OVERRIDE;
  virtual void OnInputInProgress(bool in_progress) OVERRIDE;

  // Implements ToolbarModelDelegate:
  virtual content::WebContents* GetActiveWebContents() const OVERRIDE;

  // Implements CommandUpdaterDelegate:
  virtual void ExecuteCommandWithDisposition(
      int id,
      WindowOpenDisposition) OVERRIDE;

  // Implements ChromeWebModalDialogManagerDelegate:
  virtual web_modal::WebContentsModalDialogHost*
      GetWebContentsModalDialogHost() OVERRIDE;

  // Implements web_modal::WebContentsModalDialogHost:
  virtual gfx::NativeView GetHostView() const OVERRIDE;
  virtual gfx::Point GetDialogPosition(const gfx::Size& size) OVERRIDE;
  virtual void AddObserver(
      web_modal::WebContentsModalDialogHostObserver* observer) OVERRIDE;
  virtual void RemoveObserver(
      web_modal::WebContentsModalDialogHostObserver* observer) OVERRIDE;

 private:
  void LoadImages();
  void UpdateButtons();
  void UpdateReload(bool is_loading, bool force);

  Profile* profile_;
  scoped_ptr<ToolbarModel> toolbar_model_;
  scoped_ptr<CommandUpdater> command_updater_;

  // Controls
  views::ImageButton* back_;
  views::ImageButton* forward_;
  ReloadButton* reload_;
  LocationBarView* location_bar_;
  views::WebView* web_view_;

  // Contains |web_view_| while it isn't owned by the view.
  scoped_ptr<views::WebView> web_view_container_;

  scoped_ptr<StubBubbleModelDelegate> bubble_model_delegate_;

  ObserverList<web_modal::WebContentsModalDialogHostObserver> observer_list_;

  DISALLOW_COPY_AND_ASSIGN(SimpleWebViewDialog);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_SIMPLE_WEB_VIEW_DIALOG_H_
