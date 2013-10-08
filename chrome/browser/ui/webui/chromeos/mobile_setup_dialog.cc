// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/chromeos/mobile_setup_dialog.h"

#include "base/bind.h"
#include "base/memory/singleton.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_shutdown.h"
#include "chrome/browser/chromeos/login/login_display_host_impl.h"
#include "chrome/browser/chromeos/login/webui_login_view.h"
#include "chrome/browser/chromeos/mobile/mobile_activator.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/simple_message_box.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/size.h"
#include "ui/views/widget/widget.h"
#include "ui/web_dialogs/web_dialog_delegate.h"

using chromeos::CellularNetwork;
using chromeos::MobileActivator;
using content::BrowserThread;
using content::WebContents;
using content::WebUIMessageHandler;
using ui::WebDialogDelegate;

class MobileSetupDialogDelegate : public WebDialogDelegate,
                                  public MobileActivator::Observer {
 public:
  static MobileSetupDialogDelegate* GetInstance();
  void ShowDialog(const std::string& service_path);

 protected:
  friend struct DefaultSingletonTraits<MobileSetupDialogDelegate>;

  MobileSetupDialogDelegate();
  virtual ~MobileSetupDialogDelegate();

  void OnCloseDialog();

  // WebDialogDelegate overrides.
  virtual ui::ModalType GetDialogModalType() const OVERRIDE;
  virtual string16 GetDialogTitle() const OVERRIDE;
  virtual GURL GetDialogContentURL() const OVERRIDE;
  virtual void GetWebUIMessageHandlers(
      std::vector<WebUIMessageHandler*>* handlers) const OVERRIDE;
  virtual void GetDialogSize(gfx::Size* size) const OVERRIDE;
  virtual std::string GetDialogArgs() const OVERRIDE;
  virtual void OnDialogShown(
      content::WebUI* webui,
      content::RenderViewHost* render_view_host) OVERRIDE;
  virtual void OnDialogClosed(const std::string& json_retval) OVERRIDE;
  virtual void OnCloseContents(WebContents* source,
                               bool* out_close_dialog) OVERRIDE;
  virtual bool ShouldShowDialogTitle() const OVERRIDE;
  virtual bool HandleContextMenu(
      const content::ContextMenuParams& params) OVERRIDE;

  // MobileActivator::Observer overrides.
  virtual void OnActivationStateChanged(
      CellularNetwork* network,
      MobileActivator::PlanActivationState state,
      const std::string& error_description) OVERRIDE;

 private:
  gfx::NativeWindow dialog_window_;
  // Cellular network service path.
  std::string service_path_;
  DISALLOW_COPY_AND_ASSIGN(MobileSetupDialogDelegate);
};

// static
void MobileSetupDialog::Show(const std::string& service_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  MobileSetupDialogDelegate::GetInstance()->ShowDialog(service_path);
}

// static
MobileSetupDialogDelegate* MobileSetupDialogDelegate::GetInstance() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return Singleton<MobileSetupDialogDelegate>::get();
}

MobileSetupDialogDelegate::MobileSetupDialogDelegate() : dialog_window_(NULL) {
}

MobileSetupDialogDelegate::~MobileSetupDialogDelegate() {
  MobileActivator::GetInstance()->RemoveObserver(this);
}

void MobileSetupDialogDelegate::ShowDialog(const std::string& service_path) {
  service_path_ = service_path;

  gfx::NativeWindow parent = NULL;
  // If we're on the login screen.
  if (chromeos::LoginDisplayHostImpl::default_host()) {
    chromeos::LoginDisplayHostImpl* webui_host =
        static_cast<chromeos::LoginDisplayHostImpl*>(
            chromeos::LoginDisplayHostImpl::default_host());
    chromeos::WebUILoginView* login_view = webui_host->GetWebUILoginView();
    if (login_view)
      parent = login_view->GetNativeWindow();
  }

  dialog_window_ = chrome::ShowWebDialog(
      parent,
      ProfileManager::GetDefaultProfileOrOffTheRecord(),
      this);
}

ui::ModalType MobileSetupDialogDelegate::GetDialogModalType() const {
  return ui::MODAL_TYPE_SYSTEM;
}

string16 MobileSetupDialogDelegate::GetDialogTitle() const {
  return l10n_util::GetStringUTF16(IDS_MOBILE_SETUP_TITLE);
}

GURL MobileSetupDialogDelegate::GetDialogContentURL() const {
  std::string url(chrome::kChromeUIMobileSetupURL);
  url.append(service_path_);
  return GURL(url);
}

void MobileSetupDialogDelegate::GetWebUIMessageHandlers(
    std::vector<WebUIMessageHandler*>* handlers) const {
}

void MobileSetupDialogDelegate::GetDialogSize(gfx::Size* size) const {
  size->SetSize(850, 650);
}

std::string MobileSetupDialogDelegate::GetDialogArgs() const {
  return std::string();
}

void MobileSetupDialogDelegate::OnDialogShown(
    content::WebUI* webui, content::RenderViewHost* render_view_host) {
  MobileActivator::GetInstance()->AddObserver(this);
}


void MobileSetupDialogDelegate::OnDialogClosed(const std::string& json_retval) {
  MobileActivator::GetInstance()->RemoveObserver(this);
  dialog_window_ = NULL;
}

void MobileSetupDialogDelegate::OnCloseContents(WebContents* source,
                                                bool* out_close_dialog) {
  // If we're exiting, popping up the confirmation dialog can cause a
  // crash. Note: IsTryingToQuit can be cancelled on other platforms by the
  // onbeforeunload handler, except on ChromeOS. So IsTryingToQuit is the
  // appropriate check to use here.
  if (!dialog_window_ ||
      !MobileActivator::GetInstance()->RunningActivation() ||
      browser_shutdown::IsTryingToQuit()) {
    *out_close_dialog = true;
    return;
  }

  *out_close_dialog = chrome::ShowMessageBox(dialog_window_,
      l10n_util::GetStringUTF16(IDS_MOBILE_SETUP_TITLE),
      l10n_util::GetStringUTF16(IDS_MOBILE_CANCEL_ACTIVATION),
      chrome::MESSAGE_BOX_TYPE_QUESTION);
}

bool MobileSetupDialogDelegate::ShouldShowDialogTitle() const {
  return true;
}

bool MobileSetupDialogDelegate::HandleContextMenu(
    const content::ContextMenuParams& params) {
  return true;
}

void MobileSetupDialogDelegate::OnActivationStateChanged(
    CellularNetwork* network,
    MobileActivator::PlanActivationState state,
    const std::string& error_description) {
}
