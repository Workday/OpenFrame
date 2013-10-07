// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/apps/native_app_window_views.h"

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/threading/sequenced_worker_pool.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/favicon/favicon_tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/extensions/extension_keybinding_registry_views.h"
#include "chrome/browser/ui/views/extensions/shell_window_frame_view.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "extensions/common/draggable_region.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

#if defined(OS_WIN)
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/web_applications/web_app_ui.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/browser/web_applications/web_app_win.h"
#include "ui/base/win/shell.h"
#include "ui/views/win/hwnd_util.h"
#endif

#if defined(USE_ASH)
#include "ash/screen_ash.h"
#include "ash/shell.h"
#include "ash/wm/custom_frame_view_ash.h"
#include "ash/wm/panels/panel_frame_view.h"
#include "ash/wm/window_properties.h"
#include "chrome/browser/ui/ash/ash_util.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#endif

using apps::ShellWindow;

namespace {

const int kMinPanelWidth = 100;
const int kMinPanelHeight = 100;
const int kDefaultPanelWidth = 200;
const int kDefaultPanelHeight = 300;
const int kResizeInsideBoundsSize = 5;

struct AcceleratorMapping {
  ui::KeyboardCode keycode;
  int modifiers;
  int command_id;
};
const AcceleratorMapping kAppWindowAcceleratorMap[] = {
  { ui::VKEY_W, ui::EF_CONTROL_DOWN, IDC_CLOSE_WINDOW },
  { ui::VKEY_W, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN, IDC_CLOSE_WINDOW },
  { ui::VKEY_F4, ui::EF_ALT_DOWN, IDC_CLOSE_WINDOW },
};

const std::map<ui::Accelerator, int>& GetAcceleratorTable() {
  typedef std::map<ui::Accelerator, int> AcceleratorMap;
  CR_DEFINE_STATIC_LOCAL(AcceleratorMap, accelerators, ());
  if (accelerators.empty()) {
    for (size_t i = 0; i < arraysize(kAppWindowAcceleratorMap); ++i) {
      ui::Accelerator accelerator(kAppWindowAcceleratorMap[i].keycode,
                                  kAppWindowAcceleratorMap[i].modifiers);
      accelerators[accelerator] = kAppWindowAcceleratorMap[i].command_id;
    }
  }
  return accelerators;
}

#if defined(OS_WIN)
void CreateIconAndSetRelaunchDetails(
    const base::FilePath web_app_path,
    const base::FilePath icon_file,
    const ShellIntegration::ShortcutInfo& shortcut_info,
    const HWND hwnd) {
  DCHECK(content::BrowserThread::GetBlockingPool()->RunsTasksOnCurrentThread());

  // Set the relaunch data so "Pin this program to taskbar" has the app's
  // information.
  CommandLine command_line = ShellIntegration::CommandLineArgsForLauncher(
      shortcut_info.url,
      shortcut_info.extension_id,
      shortcut_info.profile_path);

  // TODO(benwells): Change this to use app_host.exe.
  base::FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
     NOTREACHED();
     return;
  }
  command_line.SetProgram(chrome_exe);
  ui::win::SetRelaunchDetailsForWindow(command_line.GetCommandLineString(),
      shortcut_info.title, hwnd);

  if (!base::PathExists(web_app_path) &&
      !file_util::CreateDirectory(web_app_path)) {
    return;
  }
  ui::win::SetAppIconForWindow(icon_file.value(), hwnd);
  web_app::internals::CheckAndSaveIcon(icon_file, shortcut_info.favicon);
}
#endif

}  // namespace

NativeAppWindowViews::NativeAppWindowViews(
    ShellWindow* shell_window,
    const ShellWindow::CreateParams& create_params)
    : shell_window_(shell_window),
      web_view_(NULL),
      window_(NULL),
      is_fullscreen_(false),
      frameless_(create_params.frame == ShellWindow::FRAME_NONE),
      transparent_background_(create_params.transparent_background),
      minimum_size_(create_params.minimum_size),
      maximum_size_(create_params.maximum_size),
      resizable_(create_params.resizable),
      weak_ptr_factory_(this) {
  Observe(web_contents());

  window_ = new views::Widget;
  if (create_params.window_type == ShellWindow::WINDOW_TYPE_PANEL ||
      create_params.window_type == ShellWindow::WINDOW_TYPE_V1_PANEL) {
    InitializePanelWindow(create_params);
  } else {
    InitializeDefaultWindow(create_params);
  }
  extension_keybinding_registry_.reset(
      new ExtensionKeybindingRegistryViews(
          profile(),
          window_->GetFocusManager(),
          extensions::ExtensionKeybindingRegistry::PLATFORM_APPS_ONLY,
          shell_window_));

  OnViewWasResized();
  window_->AddObserver(this);
}

NativeAppWindowViews::~NativeAppWindowViews() {
  web_view_->SetWebContents(NULL);
}

void NativeAppWindowViews::InitializeDefaultWindow(
    const ShellWindow::CreateParams& create_params) {
  views::Widget::InitParams init_params(views::Widget::InitParams::TYPE_WINDOW);
  init_params.delegate = this;
  init_params.remove_standard_frame = ShouldUseChromeStyleFrame();
  init_params.use_system_default_icon = true;
  // TODO(erg): Conceptually, these are toplevel windows, but we theoretically
  // could plumb context through to here in some cases.
  init_params.top_level = true;
  gfx::Rect window_bounds = create_params.bounds;
  bool position_specified =
      window_bounds.x() != INT_MIN && window_bounds.y() != INT_MIN;
  if (position_specified && !window_bounds.IsEmpty())
    init_params.bounds = window_bounds;
  window_->Init(init_params);

  gfx::Rect adjusted_bounds = window_bounds;
  adjusted_bounds.Inset(-GetFrameInsets());
  // Center window if no position was specified.
  if (!position_specified)
    window_->CenterWindow(adjusted_bounds.size());
  else if (!adjusted_bounds.IsEmpty() && adjusted_bounds != window_bounds)
    window_->SetBounds(adjusted_bounds);

  // Register accelarators supported by app windows.
  // TODO(jeremya/stevenjb): should these be registered for panels too?
  views::FocusManager* focus_manager = GetFocusManager();
  const std::map<ui::Accelerator, int>& accelerator_table =
      GetAcceleratorTable();
  for (std::map<ui::Accelerator, int>::const_iterator iter =
           accelerator_table.begin();
       iter != accelerator_table.end(); ++iter) {
    focus_manager->RegisterAccelerator(
        iter->first, ui::AcceleratorManager::kNormalPriority, this);
  }

#if defined(OS_WIN)
  string16 app_name = UTF8ToWide(
      web_app::GenerateApplicationNameFromExtensionId(extension()->id()));
  HWND hwnd = GetNativeAppWindowHWND();
  ui::win::SetAppIdForWindow(ShellIntegration::GetAppModelIdForProfile(
      app_name, profile()->GetPath()), hwnd);

  web_app::UpdateShortcutInfoAndIconForApp(
      *extension(), profile(),
      base::Bind(&NativeAppWindowViews::OnShortcutInfoLoaded,
                 weak_ptr_factory_.GetWeakPtr()));
#endif
}

#if defined(OS_WIN)
void NativeAppWindowViews::OnShortcutInfoLoaded(
    const ShellIntegration::ShortcutInfo& shortcut_info) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  HWND hwnd = GetNativeAppWindowHWND();

  // Set window's icon to the one we're about to create/update in the web app
  // path. The icon cache will refresh on icon creation.
  base::FilePath web_app_path = web_app::GetWebAppDataDirectory(
      shortcut_info.profile_path, shortcut_info.extension_id,
      shortcut_info.url);
  base::FilePath icon_file = web_app_path
      .Append(web_app::internals::GetSanitizedFileName(shortcut_info.title))
      .ReplaceExtension(FILE_PATH_LITERAL(".ico"));

  content::BrowserThread::PostBlockingPoolTask(
      FROM_HERE,
      base::Bind(&CreateIconAndSetRelaunchDetails,
                 web_app_path, icon_file, shortcut_info, hwnd));
}

HWND NativeAppWindowViews::GetNativeAppWindowHWND() const {
  return views::HWNDForWidget(GetWidget()->GetTopLevelWidget());
}
#endif

void NativeAppWindowViews::InitializePanelWindow(
    const ShellWindow::CreateParams& create_params) {
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_PANEL);
  params.delegate = this;

  preferred_size_ = gfx::Size(create_params.bounds.width(),
                              create_params.bounds.height());
  if (preferred_size_.width() == 0)
    preferred_size_.set_width(kDefaultPanelWidth);
  else if (preferred_size_.width() < kMinPanelWidth)
    preferred_size_.set_width(kMinPanelWidth);

  if (preferred_size_.height() == 0)
    preferred_size_.set_height(kDefaultPanelHeight);
  else if (preferred_size_.height() < kMinPanelHeight)
    preferred_size_.set_height(kMinPanelHeight);
#if defined(USE_ASH)
  if (ash::Shell::HasInstance()) {
    // Open a new panel on the active root window where
    // a current active/focused window is on.
    aura::RootWindow* active = ash::Shell::GetActiveRootWindow();
    params.bounds = ash::ScreenAsh::ConvertRectToScreen(
        active, gfx::Rect(preferred_size_));
  } else {
    params.bounds = gfx::Rect(preferred_size_);
  }
#else
  params.bounds = gfx::Rect(preferred_size_);
#endif
  // TODO(erg): Conceptually, these are toplevel windows, but we theoretically
  // could plumb context through to here in some cases.
  params.top_level = true;
  window_->Init(params);
  window_->set_focus_on_creation(create_params.focused);

#if defined(USE_ASH)
  if (create_params.state == ui::SHOW_STATE_DETACHED) {
    gfx::Rect window_bounds(create_params.bounds.x(),
                            create_params.bounds.y(),
                            preferred_size_.width(),
                            preferred_size_.height());
    aura::Window* native_window = GetNativeWindow();
    native_window->SetProperty(ash::internal::kPanelAttachedKey, false);
    native_window->SetDefaultParentByRootWindow(
        native_window->GetRootWindow(), native_window->GetBoundsInScreen());
    window_->SetBounds(window_bounds);
  }
#else
  // TODO(stevenjb): NativeAppWindow panels need to be implemented for other
  // platforms.
#endif
}

// ui::BaseWindow implementation.

bool NativeAppWindowViews::IsActive() const {
  return window_->IsActive();
}

bool NativeAppWindowViews::IsMaximized() const {
  return window_->IsMaximized();
}

bool NativeAppWindowViews::IsMinimized() const {
  return window_->IsMinimized();
}

bool NativeAppWindowViews::IsFullscreen() const {
  return window_->IsFullscreen();
}

gfx::NativeWindow NativeAppWindowViews::GetNativeWindow() {
  return window_->GetNativeWindow();
}

gfx::Rect NativeAppWindowViews::GetRestoredBounds() const {
  return window_->GetRestoredBounds();
}

ui::WindowShowState NativeAppWindowViews::GetRestoredState() const {
  if (IsMaximized())
    return ui::SHOW_STATE_MAXIMIZED;
#if defined(USE_ASH)
  // On Ash, restore fullscreen.
  if (IsFullscreen())
    return ui::SHOW_STATE_FULLSCREEN;

  // Use kRestoreShowStateKey in case a window is minimized/hidden.
  ui::WindowShowState restore_state =
      window_->GetNativeWindow()->GetProperty(
          aura::client::kRestoreShowStateKey);
  // Whitelist states to return so that invalid and transient states
  // are not saved and used to restore windows when they are recreated.
  switch (restore_state) {
    case ui::SHOW_STATE_NORMAL:
    case ui::SHOW_STATE_MAXIMIZED:
    case ui::SHOW_STATE_FULLSCREEN:
    case ui::SHOW_STATE_DETACHED:
      return restore_state;

    case ui::SHOW_STATE_DEFAULT:
    case ui::SHOW_STATE_MINIMIZED:
    case ui::SHOW_STATE_INACTIVE:
    case ui::SHOW_STATE_END:
      return ui::SHOW_STATE_NORMAL;
  }
#endif
  return ui::SHOW_STATE_NORMAL;
}

gfx::Rect NativeAppWindowViews::GetBounds() const {
  return window_->GetWindowBoundsInScreen();
}

void NativeAppWindowViews::Show() {
  if (window_->IsVisible()) {
    window_->Activate();
    return;
  }

  window_->Show();
}

void NativeAppWindowViews::ShowInactive() {
  if (window_->IsVisible())
    return;
  window_->ShowInactive();
}

void NativeAppWindowViews::Hide() {
  window_->Hide();
}

void NativeAppWindowViews::Close() {
  window_->Close();
}

void NativeAppWindowViews::Activate() {
  window_->Activate();
}

void NativeAppWindowViews::Deactivate() {
  window_->Deactivate();
}

void NativeAppWindowViews::Maximize() {
  window_->Maximize();
}

void NativeAppWindowViews::Minimize() {
  window_->Minimize();
}

void NativeAppWindowViews::Restore() {
  window_->Restore();
}

void NativeAppWindowViews::SetBounds(const gfx::Rect& bounds) {
  GetWidget()->SetBounds(bounds);
}

void NativeAppWindowViews::FlashFrame(bool flash) {
  window_->FlashFrame(flash);
}

bool NativeAppWindowViews::IsAlwaysOnTop() const {
  if (!shell_window_->window_type_is_panel())
    return false;
#if defined(USE_ASH)
  return window_->GetNativeWindow()->GetProperty(
      ash::internal::kPanelAttachedKey);
#else
  return true;
#endif
}

gfx::Insets NativeAppWindowViews::GetFrameInsets() const {
  if (frameless())
    return gfx::Insets();

  // The pretend client_bounds passed in need to be large enough to ensure that
  // GetWindowBoundsForClientBounds() doesn't decide that it needs more than
  // the specified amount of space to fit the window controls in, and return a
  // number larger than the real frame insets. Most window controls are smaller
  // than 1000x1000px, so this should be big enough.
  gfx::Rect client_bounds = gfx::Rect(1000, 1000);
  gfx::Rect window_bounds =
      window_->non_client_view()->GetWindowBoundsForClientBounds(
          client_bounds);
  return window_bounds.InsetsFrom(client_bounds);
}

gfx::NativeView NativeAppWindowViews::GetHostView() const {
  return window_->GetNativeView();
}

gfx::Point NativeAppWindowViews::GetDialogPosition(const gfx::Size& size) {
  gfx::Size shell_window_size = window_->GetWindowBoundsInScreen().size();
  return gfx::Point(shell_window_size.width() / 2 - size.width() / 2,
                    shell_window_size.height() / 2 - size.height() / 2);
}

void NativeAppWindowViews::AddObserver(
    web_modal::WebContentsModalDialogHostObserver* observer) {
  observer_list_.AddObserver(observer);
}
void NativeAppWindowViews::RemoveObserver(
    web_modal::WebContentsModalDialogHostObserver* observer) {
  observer_list_.RemoveObserver(observer);
}

// Private method. TODO(stevenjb): Move this below InitializePanelWindow()
// to match declaration order.
void NativeAppWindowViews::OnViewWasResized() {
  // TODO(jeremya): this doesn't seem like a terribly elegant way to keep the
  // window shape in sync.
#if defined(OS_WIN) && !defined(USE_AURA)
  DCHECK(window_);
  DCHECK(web_view_);
  gfx::Size sz = web_view_->size();
  int height = sz.height(), width = sz.width();
  if (ShouldUseChromeStyleFrame()) {
    // Set the window shape of the RWHV.
    const int kCornerRadius = 1;
    gfx::Path path;
    if (window_->IsMaximized() || window_->IsFullscreen()) {
      // Don't round the corners when the window is maximized or fullscreen.
      path.addRect(0, 0, width, height);
    } else {
      if (frameless_) {
        path.moveTo(0, kCornerRadius);
        path.lineTo(kCornerRadius, 0);
        path.lineTo(width - kCornerRadius, 0);
        path.lineTo(width, kCornerRadius);
      } else {
        // Don't round the top corners in chrome-style frame mode.
        path.moveTo(0, 0);
        path.lineTo(width, 0);
      }
      path.lineTo(width, height - kCornerRadius - 1);
      path.lineTo(width - kCornerRadius - 1, height);
      path.lineTo(kCornerRadius + 1, height);
      path.lineTo(0, height - kCornerRadius - 1);
      path.close();
    }
    SetWindowRgn(web_contents()->GetView()->GetNativeView(),
                 path.CreateNativeRegion(), 1);
  }

  SkRegion* rgn = new SkRegion;
  if (!window_->IsFullscreen()) {
    if (draggable_region())
      rgn->op(*draggable_region(), SkRegion::kUnion_Op);
    if (!window_->IsMaximized()) {
      if (frameless_)
        rgn->op(0, 0, width, kResizeInsideBoundsSize, SkRegion::kUnion_Op);
      rgn->op(0, 0, kResizeInsideBoundsSize, height, SkRegion::kUnion_Op);
      rgn->op(width - kResizeInsideBoundsSize, 0, width, height,
          SkRegion::kUnion_Op);
      rgn->op(0, height - kResizeInsideBoundsSize, width, height,
          SkRegion::kUnion_Op);
    }
  }
  if (web_contents()->GetRenderViewHost()->GetView())
    web_contents()->GetRenderViewHost()->GetView()->SetClickthroughRegion(rgn);
#endif

  FOR_EACH_OBSERVER(web_modal::WebContentsModalDialogHostObserver,
                    observer_list_,
                    OnPositionRequiresUpdate());
}

bool NativeAppWindowViews::ShouldUseChromeStyleFrame() const {
  return !CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kAppsUseNativeFrame) || frameless_;
}

// WidgetDelegate implementation.

void NativeAppWindowViews::OnWidgetMove() {
  shell_window_->OnNativeWindowChanged();
}

views::View* NativeAppWindowViews::GetInitiallyFocusedView() {
  return web_view_;
}

bool NativeAppWindowViews::CanResize() const {
  return resizable_ &&
      (maximum_size_.IsEmpty() || minimum_size_ != maximum_size_);
}

bool NativeAppWindowViews::CanMaximize() const {
  return resizable_ && maximum_size_.IsEmpty();
}

string16 NativeAppWindowViews::GetWindowTitle() const {
  return shell_window_->GetTitle();
}

bool NativeAppWindowViews::ShouldShowWindowTitle() const {
  return shell_window_->window_type() == ShellWindow::WINDOW_TYPE_V1_PANEL;
}

gfx::ImageSkia NativeAppWindowViews::GetWindowAppIcon() {
  gfx::Image app_icon = shell_window_->app_icon();
  if (app_icon.IsEmpty())
    return GetWindowIcon();
  else
    return *app_icon.ToImageSkia();
}

gfx::ImageSkia NativeAppWindowViews::GetWindowIcon() {
  content::WebContents* web_contents = shell_window_->web_contents();
  if (web_contents) {
    FaviconTabHelper* favicon_tab_helper =
        FaviconTabHelper::FromWebContents(web_contents);
    gfx::Image app_icon = favicon_tab_helper->GetFavicon();
    if (!app_icon.IsEmpty())
      return *app_icon.ToImageSkia();
  }
  return gfx::ImageSkia();
}

bool NativeAppWindowViews::ShouldShowWindowIcon() const {
  return shell_window_->window_type() == ShellWindow::WINDOW_TYPE_V1_PANEL;
}

void NativeAppWindowViews::SaveWindowPlacement(const gfx::Rect& bounds,
                                               ui::WindowShowState show_state) {
  views::WidgetDelegate::SaveWindowPlacement(bounds, show_state);
  shell_window_->OnNativeWindowChanged();
}

void NativeAppWindowViews::DeleteDelegate() {
  window_->RemoveObserver(this);
  shell_window_->OnNativeClose();
}

views::Widget* NativeAppWindowViews::GetWidget() {
  return window_;
}

const views::Widget* NativeAppWindowViews::GetWidget() const {
  return window_;
}

views::NonClientFrameView* NativeAppWindowViews::CreateNonClientFrameView(
    views::Widget* widget) {
#if defined(USE_ASH)
  if (chrome::IsNativeViewInAsh(widget->GetNativeView())) {
    if (shell_window_->window_type_is_panel()) {
      ash::PanelFrameView::FrameType frame_type = frameless_ ?
          ash::PanelFrameView::FRAME_NONE : ash::PanelFrameView::FRAME_ASH;
      return new ash::PanelFrameView(widget, frame_type);
    }
    if (!frameless_) {
      ash::CustomFrameViewAsh* frame = new ash::CustomFrameViewAsh();
      frame->Init(widget);
      return frame;
    }
  }
#endif
  if (ShouldUseChromeStyleFrame()) {
    ShellWindowFrameView* frame_view = new ShellWindowFrameView(this);
    frame_view->Init(window_);
    return frame_view;
  }
  return views::WidgetDelegateView::CreateNonClientFrameView(widget);
}

bool NativeAppWindowViews::ShouldDescendIntoChildForEventHandling(
    gfx::NativeView child,
    const gfx::Point& location) {
#if defined(USE_AURA)
  if (child == web_view_->web_contents()->GetView()->GetNativeView()) {
    // Shell window should claim mouse events that fall within the draggable
    // region.
    return !draggable_region_.get() ||
           !draggable_region_->contains(location.x(), location.y());
  }
#endif

  return true;
}

// WidgetObserver implementation.

void NativeAppWindowViews::OnWidgetVisibilityChanged(views::Widget* widget,
                                                     bool visible) {
  shell_window_->OnNativeWindowChanged();
}

void NativeAppWindowViews::OnWidgetActivationChanged(views::Widget* widget,
                                                     bool active) {
  shell_window_->OnNativeWindowChanged();
  if (active)
    shell_window_->OnNativeWindowActivated();
}

// WebContentsObserver implementation.

void NativeAppWindowViews::RenderViewCreated(
    content::RenderViewHost* render_view_host) {
  if (transparent_background_) {
    // Use a background with transparency to trigger transparency in Webkit.
    SkBitmap background;
    background.setConfig(SkBitmap::kARGB_8888_Config, 1, 1);
    background.allocPixels();
    background.eraseARGB(0x00, 0x00, 0x00, 0x00);

    content::RenderWidgetHostView* view = render_view_host->GetView();
    DCHECK(view);
    view->SetBackground(background);
  }
}

// views::View implementation.

void NativeAppWindowViews::Layout() {
  DCHECK(web_view_);
  web_view_->SetBounds(0, 0, width(), height());
  OnViewWasResized();
}

void NativeAppWindowViews::ViewHierarchyChanged(
    const ViewHierarchyChangedDetails& details) {
  if (details.is_add && details.child == this) {
    web_view_ = new views::WebView(NULL);
    AddChildView(web_view_);
    web_view_->SetWebContents(web_contents());
  }
}

gfx::Size NativeAppWindowViews::GetPreferredSize() {
  if (!preferred_size_.IsEmpty())
    return preferred_size_;
  return views::View::GetPreferredSize();
}

gfx::Size NativeAppWindowViews::GetMinimumSize() {
  return minimum_size_;
}

gfx::Size NativeAppWindowViews::GetMaximumSize() {
  return maximum_size_;
}

void NativeAppWindowViews::OnFocus() {
  web_view_->RequestFocus();
}

bool NativeAppWindowViews::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  const std::map<ui::Accelerator, int>& accelerator_table =
      GetAcceleratorTable();
  std::map<ui::Accelerator, int>::const_iterator iter =
      accelerator_table.find(accelerator);
  DCHECK(iter != accelerator_table.end());
  int command_id = iter->second;
  switch (command_id) {
    case IDC_CLOSE_WINDOW:
      Close();
      return true;
    default:
      NOTREACHED() << "Unknown accelerator sent to app window.";
  }
  return false;
}

// NativeAppWindow implementation.

void NativeAppWindowViews::SetFullscreen(bool fullscreen) {
  // Fullscreen not supported by panels.
  if (shell_window_->window_type_is_panel())
    return;
  is_fullscreen_ = fullscreen;
  window_->SetFullscreen(fullscreen);
  // TODO(jeremya) we need to call RenderViewHost::ExitFullscreen() if we
  // ever drop the window out of fullscreen in response to something that
  // wasn't the app calling webkitCancelFullScreen().
}

bool NativeAppWindowViews::IsFullscreenOrPending() const {
  return is_fullscreen_;
}

bool NativeAppWindowViews::IsDetached() const {
  if (!shell_window_->window_type_is_panel())
    return false;
#if defined(USE_ASH)
  return !window_->GetNativeWindow()->GetProperty(
      ash::internal::kPanelAttachedKey);
#else
  return false;
#endif
}

views::View* NativeAppWindowViews::GetContentsView() {
  return this;
}

void NativeAppWindowViews::UpdateWindowIcon() {
  window_->UpdateWindowIcon();
}

void NativeAppWindowViews::UpdateWindowTitle() {
  window_->UpdateWindowTitle();
}

void NativeAppWindowViews::UpdateDraggableRegions(
    const std::vector<extensions::DraggableRegion>& regions) {
  // Draggable region is not supported for non-frameless window.
  if (!frameless_)
    return;

  draggable_region_.reset(ShellWindow::RawDraggableRegionsToSkRegion(regions));
  OnViewWasResized();
}

void NativeAppWindowViews::HandleKeyboardEvent(
    const content::NativeWebKeyboardEvent& event) {
  unhandled_keyboard_event_handler_.HandleKeyboardEvent(event,
                                                        GetFocusManager());
}

void NativeAppWindowViews::RenderViewHostChanged() {
  OnViewWasResized();
}
