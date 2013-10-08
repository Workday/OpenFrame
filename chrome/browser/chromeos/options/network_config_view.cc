// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/options/network_config_view.h"

#include <algorithm>

#include "ash/shell.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chromeos/cros/network_property_ui_data.h"
#include "chrome/browser/chromeos/login/login_display_host_impl.h"
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/chromeos/options/vpn_config_view.h"
#include "chrome/browser/chromeos/options/wifi_config_view.h"
#include "chrome/browser/chromeos/options/wimax_config_view.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/host_desktop.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "ui/aura/root_window.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/rect.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/layout/layout_constants.h"
#include "ui/views/widget/widget.h"

using views::Widget;

namespace {

gfx::NativeWindow GetDialogParent() {
  if (chromeos::LoginDisplayHostImpl::default_host()) {
    return chromeos::LoginDisplayHostImpl::default_host()->GetNativeWindow();
  } else {
    Browser* browser = chrome::FindTabbedBrowser(
        ProfileManager::GetDefaultProfileOrOffTheRecord(),
        true,
        chrome::HOST_DESKTOP_TYPE_ASH);
    if (browser)
      return browser->window()->GetNativeWindow();
  }
  return NULL;
}

// Avoid global static initializer.
chromeos::NetworkConfigView** GetActiveDialogPointer() {
  static chromeos::NetworkConfigView* active_dialog = NULL;
  return &active_dialog;
}

chromeos::NetworkConfigView* GetActiveDialog() {
  return *(GetActiveDialogPointer());
}

void SetActiveDialog(chromeos::NetworkConfigView* dialog) {
  *(GetActiveDialogPointer()) = dialog;
}

}  // namespace

namespace chromeos {

// static
const int ChildNetworkConfigView::kInputFieldMinWidth = 270;

NetworkConfigView::NetworkConfigView()
    : child_config_view_(NULL),
      delegate_(NULL),
      advanced_button_(NULL) {
  DCHECK(GetActiveDialog() == NULL);
  SetActiveDialog(this);
}

void NetworkConfigView::InitWithNetworkState(const NetworkState* network) {
  DCHECK(network);
  std::string service_path = network->path();
  if (network->type() == flimflam::kTypeWifi)
    child_config_view_ = new WifiConfigView(this, service_path, false);
  else if (network->type() == flimflam::kTypeWimax)
    child_config_view_ = new WimaxConfigView(this, service_path);
  else if (network->type() == flimflam::kTypeVPN)
    child_config_view_ = new VPNConfigView(this, service_path);
  else
    NOTREACHED();
}

void NetworkConfigView::InitWithType(const std::string& type) {
  if (type == flimflam::kTypeWifi) {
    child_config_view_ = new WifiConfigView(this,
                                            "" /* service_path */,
                                            false /* show_8021x */);
    advanced_button_ = new views::LabelButton(this, l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_ADVANCED_BUTTON));
    advanced_button_->SetStyle(views::Button::STYLE_NATIVE_TEXTBUTTON);
  } else if (type == flimflam::kTypeVPN) {
    child_config_view_ = new VPNConfigView(this,
                                           "" /* service_path */);
  } else {
    NOTREACHED();
  }
}

NetworkConfigView::~NetworkConfigView() {
  DCHECK(GetActiveDialog() == this);
  SetActiveDialog(NULL);
}

// static
void NetworkConfigView::Show(const std::string& service_path,
                             gfx::NativeWindow parent) {
  if (GetActiveDialog() != NULL)
    return;
  NetworkConfigView* view = new NetworkConfigView();
  const NetworkState* network = NetworkHandler::Get()->network_state_handler()->
      GetNetworkState(service_path);
  if (!network) {
    LOG(ERROR) << "NetworkConfigView::Show called with invalid service_path";
    return;
  }
  view->InitWithNetworkState(network);
  view->ShowDialog(parent);
}

// static
void NetworkConfigView::ShowForType(const std::string& type,
                                    gfx::NativeWindow parent) {
  if (GetActiveDialog() != NULL)
    return;
  NetworkConfigView* view = new NetworkConfigView();
  view->InitWithType(type);
  view->ShowDialog(parent);
}

gfx::NativeWindow NetworkConfigView::GetNativeWindow() const {
  return GetWidget()->GetNativeWindow();
}

string16 NetworkConfigView::GetDialogButtonLabel(
    ui::DialogButton button) const {
  if (button == ui::DIALOG_BUTTON_OK)
    return l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_CONNECT);
  return views::DialogDelegateView::GetDialogButtonLabel(button);
}

bool NetworkConfigView::IsDialogButtonEnabled(ui::DialogButton button) const {
  // Disable connect button if cannot login.
  if (button == ui::DIALOG_BUTTON_OK)
    return child_config_view_->CanLogin();
  return true;
}

bool NetworkConfigView::Cancel() {
  if (delegate_)
    delegate_->OnDialogCancelled();
  child_config_view_->Cancel();
  return true;
}

bool NetworkConfigView::Accept() {
  // Do not attempt login if it is guaranteed to fail, keep the dialog open.
  if (!child_config_view_->CanLogin())
    return false;
  bool result = child_config_view_->Login();
  if (result && delegate_)
    delegate_->OnDialogAccepted();
  return result;
}

views::View* NetworkConfigView::CreateExtraView() {
  return advanced_button_;
}

views::View* NetworkConfigView::GetInitiallyFocusedView() {
  return child_config_view_->GetInitiallyFocusedView();
}

string16 NetworkConfigView::GetWindowTitle() const {
  DCHECK(!child_config_view_->GetTitle().empty());
  return child_config_view_->GetTitle();
}

ui::ModalType NetworkConfigView::GetModalType() const {
  return ui::MODAL_TYPE_SYSTEM;
}

void NetworkConfigView::GetAccessibleState(ui::AccessibleViewState* state) {
  state->name =
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_OTHER_WIFI_NETWORKS);
  state->role = ui::AccessibilityTypes::ROLE_DIALOG;
}

void NetworkConfigView::ButtonPressed(views::Button* sender,
                                      const ui::Event& event) {
  if (advanced_button_ && sender == advanced_button_) {
    advanced_button_->SetVisible(false);
    ShowAdvancedView();
  }
}

void NetworkConfigView::ShowAdvancedView() {
  // Clear out the old widgets and build new ones.
  RemoveChildView(child_config_view_);
  delete child_config_view_;
  // For now, there is only an advanced view for Wi-Fi 802.1X.
  child_config_view_ = new WifiConfigView(this,
                                          "" /* service_path */,
                                          true /* show_8021x */);
  AddChildView(child_config_view_);
  // Resize the window to be able to hold the new widgets.
  gfx::Size size = views::Widget::GetLocalizedContentsSize(
      IDS_JOIN_WIFI_NETWORK_DIALOG_ADVANCED_WIDTH_CHARS,
      IDS_JOIN_WIFI_NETWORK_DIALOG_ADVANCED_MINIMUM_HEIGHT_LINES);
  // Get the new bounds with desired size at the same center point.
  gfx::Rect bounds = GetWidget()->GetWindowBoundsInScreen();
  int horiz_padding = bounds.width() - size.width();
  int vert_padding = bounds.height() - size.height();
  bounds.Inset(horiz_padding / 2, vert_padding / 2,
               horiz_padding / 2, vert_padding / 2);
  GetWidget()->SetBoundsConstrained(bounds);
  Layout();
  child_config_view_->InitFocus();
}

void NetworkConfigView::Layout() {
  child_config_view_->SetBounds(0, 0, width(), height());
}

gfx::Size NetworkConfigView::GetPreferredSize() {
  gfx::Size result(views::Widget::GetLocalizedContentsSize(
      IDS_JOIN_WIFI_NETWORK_DIALOG_WIDTH_CHARS,
      IDS_JOIN_WIFI_NETWORK_DIALOG_MINIMUM_HEIGHT_LINES));
  gfx::Size size = child_config_view_->GetPreferredSize();
  result.set_height(size.height());
  if (size.width() > result.width())
    result.set_width(size.width());
  return result;
}

void NetworkConfigView::ViewHierarchyChanged(
    const ViewHierarchyChangedDetails& details) {
  // Can't init before we're inserted into a Container, because we require
  // a HWND to parent native child controls to.
  if (details.is_add && details.child == this) {
    AddChildView(child_config_view_);
  }
}

void NetworkConfigView::ShowDialog(gfx::NativeWindow parent) {
  if (parent == NULL)
    parent = GetDialogParent();
  // Failed connections may result in a pop-up with no natural parent window,
  // so provide a fallback context on the active display.
  gfx::NativeWindow context = parent ? NULL : ash::Shell::GetActiveRootWindow();
  Widget* window = DialogDelegate::CreateDialogWidget(this, context, parent);
  window->SetAlwaysOnTop(true);
  window->Show();
}

// ChildNetworkConfigView

ChildNetworkConfigView::ChildNetworkConfigView(
    NetworkConfigView* parent,
    const std::string& service_path)
    : parent_(parent),
      service_path_(service_path) {
}

ChildNetworkConfigView::~ChildNetworkConfigView() {
}

// ControlledSettingIndicatorView

ControlledSettingIndicatorView::ControlledSettingIndicatorView()
    : managed_(false),
      image_view_(NULL) {
  Init();
}

ControlledSettingIndicatorView::ControlledSettingIndicatorView(
    const NetworkPropertyUIData& ui_data)
    : managed_(false),
      image_view_(NULL) {
  Init();
  Update(ui_data);
}

ControlledSettingIndicatorView::~ControlledSettingIndicatorView() {}

void ControlledSettingIndicatorView::Update(
    const NetworkPropertyUIData& ui_data) {
  if (managed_ == ui_data.IsManaged())
    return;

  managed_ = ui_data.IsManaged();
  PreferredSizeChanged();
}

gfx::Size ControlledSettingIndicatorView::GetPreferredSize() {
  return (managed_ && visible()) ? image_view_->GetPreferredSize()
                                 : gfx::Size();
}

void ControlledSettingIndicatorView::Layout() {
  image_view_->SetBounds(0, 0, width(), height());
}

void ControlledSettingIndicatorView::OnMouseEntered(
    const ui::MouseEvent& event) {
  image_view_->SetImage(color_image_);
}

void ControlledSettingIndicatorView::OnMouseExited(
    const ui::MouseEvent& event) {
  image_view_->SetImage(gray_image_);
}

void ControlledSettingIndicatorView::Init() {
  color_image_ = ResourceBundle::GetSharedInstance().GetImageNamed(
      IDR_CONTROLLED_SETTING_MANDATORY).ToImageSkia();
  gray_image_ = ResourceBundle::GetSharedInstance().GetImageNamed(
      IDR_CONTROLLED_SETTING_MANDATORY_GRAY).ToImageSkia();
  image_view_ = new views::ImageView();
  // Disable |image_view_| so mouse events propagate to the parent.
  image_view_->SetEnabled(false);
  image_view_->SetImage(gray_image_);
  image_view_->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_OPTIONS_CONTROLLED_SETTING_POLICY));
  AddChildView(image_view_);
}

}  // namespace chromeos
