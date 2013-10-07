// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/test/ash_test_base.h"

#include <string>
#include <vector>

#include "ash/ash_switches.h"
#include "ash/display/display_controller.h"
#include "ash/screen_ash.h"
#include "ash/shell.h"
#include "ash/test/ash_test_helper.h"
#include "ash/test/display_manager_test_api.h"
#include "ash/test/test_session_state_delegate.h"
#include "ash/test/test_shell_delegate.h"
#include "ash/wm/coordinate_conversion.h"
#include "base/command_line.h"
#include "content/public/test/web_contents_tester.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/root_window.h"
#include "ui/aura/test/event_generator.h"
#include "ui/aura/test/test_window_delegate.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/base/ime/input_method_initializer.h"
#include "ui/gfx/display.h"
#include "ui/gfx/point.h"
#include "ui/gfx/screen.h"

#if defined(OS_CHROMEOS)
#include "ash/system/chromeos/tray_display.h"
#endif

#if defined(OS_WIN)
#include "ash/test/test_metro_viewer_process_host.h"
#include "base/test/test_process_killer_win.h"
#include "base/win/metro.h"
#include "base/win/windows_version.h"
#include "ui/aura/remote_root_window_host_win.h"
#include "ui/aura/root_window_host_win.h"
#include "win8/test/test_registrar_constants.h"
#endif

namespace ash {
namespace test {
namespace {

class AshEventGeneratorDelegate : public aura::test::EventGeneratorDelegate {
 public:
  AshEventGeneratorDelegate() {}
  virtual ~AshEventGeneratorDelegate() {}

  // aura::test::EventGeneratorDelegate overrides:
  virtual aura::RootWindow* GetRootWindowAt(
      const gfx::Point& point_in_screen) const OVERRIDE {
    gfx::Screen* screen = Shell::GetScreen();
    gfx::Display display = screen->GetDisplayNearestPoint(point_in_screen);
    return Shell::GetInstance()->display_controller()->
        GetRootWindowForDisplayId(display.id());
  }

  virtual aura::client::ScreenPositionClient* GetScreenPositionClient(
      const aura::Window* window) const OVERRIDE {
    return aura::client::GetScreenPositionClient(window->GetRootWindow());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(AshEventGeneratorDelegate);
};

}  // namespace

content::WebContents* AshTestViewsDelegate::CreateWebContents(
    content::BrowserContext* browser_context,
    content::SiteInstance* site_instance) {
  return content::WebContentsTester::CreateTestWebContents(browser_context,
                                                           site_instance);
}

/////////////////////////////////////////////////////////////////////////////

AshTestBase::AshTestBase()
    : setup_called_(false),
      teardown_called_(false),
      start_session_(true) {
  // Must initialize |ash_test_helper_| here because some tests rely on
  // AshTestBase methods before they call AshTestBase::SetUp().
  ash_test_helper_.reset(new AshTestHelper(base::MessageLoopForUI::current()));
}

AshTestBase::~AshTestBase() {
  CHECK(setup_called_)
      << "You have overridden SetUp but never called AshTestBase::SetUp";
  CHECK(teardown_called_)
      << "You have overridden TearDown but never called AshTestBase::TearDown";
}

void AshTestBase::SetUp() {
  setup_called_ = true;
  // TODO(jamescook): Can we do this without changing command line?
  // Use the origin (1,1) so that it doesn't over
  // lap with the native mouse cursor.
  CommandLine::ForCurrentProcess()->AppendSwitchASCII(
      switches::kAshHostWindowBounds, "1+1-800x600");
#if defined(OS_WIN)
  aura::test::SetUsePopupAsRootWindowForTest(true);
#endif
  ui::InitializeInputMethodForTesting();

  ash_test_helper_->SetUp(start_session_);

  Shell::GetPrimaryRootWindow()->Show();
  Shell::GetPrimaryRootWindow()->ShowRootWindow();
  // Move the mouse cursor to far away so that native events doesn't
  // interfere test expectations.
  Shell::GetPrimaryRootWindow()->MoveCursorTo(gfx::Point(-1000, -1000));
  ash::Shell::GetInstance()->cursor_manager()->EnableMouseEvents();

#if defined(OS_WIN)
  if (base::win::GetVersion() >= base::win::VERSION_WIN8 &&
      !CommandLine::ForCurrentProcess()->HasSwitch(
          ash::switches::kForceAshToDesktop)) {
    ipc_thread_.reset(new base::Thread("test_metro_viewer_ipc_thread"));
    base::Thread::Options options;
    options.message_loop_type = base::MessageLoop::TYPE_IO;
    ipc_thread_->StartWithOptions(options);

    metro_viewer_host_.reset(
        new TestMetroViewerProcessHost(ipc_thread_->message_loop_proxy()));
    CHECK(metro_viewer_host_->LaunchViewerAndWaitForConnection(
        win8::test::kDefaultTestAppUserModelId));
    aura::RemoteRootWindowHostWin* root_window_host =
        aura::RemoteRootWindowHostWin::Instance();
    CHECK(root_window_host != NULL);
  }
#endif
}

void AshTestBase::TearDown() {
  teardown_called_ = true;
  // Flush the message loop to finish pending release tasks.
  RunAllPendingInMessageLoop();

#if defined(OS_WIN)
  if (base::win::GetVersion() >= base::win::VERSION_WIN8 &&
      !CommandLine::ForCurrentProcess()->HasSwitch(
          ash::switches::kForceAshToDesktop)) {
    // Check that our viewer connection is still established.
    CHECK(!metro_viewer_host_->closed_unexpectedly());
  }
#endif

  ash_test_helper_->TearDown();

  ui::ShutdownInputMethodForTesting();
#if defined(OS_WIN)
  aura::test::SetUsePopupAsRootWindowForTest(false);
  // Kill the viewer process if we spun one up.
  metro_viewer_host_.reset();

  // Clean up any dangling viewer processes as the metro APIs sometimes leave
  // zombies behind. A default browser process in metro will have the
  // following command line arg so use that to avoid killing all processes named
  // win8::test::kDefaultTestExePath.
  const wchar_t kViewerProcessArgument[] = L"DefaultBrowserServer";
  base::KillAllNamedProcessesWithArgument(win8::test::kDefaultTestExePath,
                                          kViewerProcessArgument);
#endif

  event_generator_.reset();
  // Some tests set an internal display id,
  // reset it here, so other tests will continue in a clean environment.
  gfx::Display::SetInternalDisplayId(gfx::Display::kInvalidDisplayID);
}

aura::test::EventGenerator& AshTestBase::GetEventGenerator() {
  if (!event_generator_) {
    event_generator_.reset(
        new aura::test::EventGenerator(new AshEventGeneratorDelegate()));
  }
  return *event_generator_.get();
}

// static
bool AshTestBase::SupportsMultipleDisplays() {
#if defined(OS_WIN)
  return base::win::GetVersion() < base::win::VERSION_WIN8;
#else
  return true;
#endif
}

// static
bool AshTestBase::SupportsHostWindowResize() {
#if defined(OS_WIN)
  return base::win::GetVersion() < base::win::VERSION_WIN8;
#else
  return true;
#endif
}

void AshTestBase::UpdateDisplay(const std::string& display_specs) {
  DisplayManagerTestApi display_manager_test_api(
      Shell::GetInstance()->display_manager());
  display_manager_test_api.UpdateDisplay(display_specs);
}

aura::RootWindow* AshTestBase::CurrentContext() {
  return ash_test_helper_->CurrentContext();
}

aura::Window* AshTestBase::CreateTestWindowInShellWithId(int id) {
  return CreateTestWindowInShellWithDelegate(NULL, id, gfx::Rect());
}

aura::Window* AshTestBase::CreateTestWindowInShellWithBounds(
    const gfx::Rect& bounds) {
  return CreateTestWindowInShellWithDelegate(NULL, 0, bounds);
}

aura::Window* AshTestBase::CreateTestWindowInShell(SkColor color,
                                                   int id,
                                                   const gfx::Rect& bounds) {
  return CreateTestWindowInShellWithDelegate(
      new aura::test::ColorTestWindowDelegate(color), id, bounds);
}

aura::Window* AshTestBase::CreateTestWindowInShellWithDelegate(
    aura::WindowDelegate* delegate,
    int id,
    const gfx::Rect& bounds) {
  return CreateTestWindowInShellWithDelegateAndType(
      delegate,
      aura::client::WINDOW_TYPE_NORMAL,
      id,
      bounds);
}

aura::Window* AshTestBase::CreateTestWindowInShellWithDelegateAndType(
    aura::WindowDelegate* delegate,
    aura::client::WindowType type,
    int id,
    const gfx::Rect& bounds) {
  aura::Window* window = new aura::Window(delegate);
  window->set_id(id);
  window->SetType(type);
  window->Init(ui::LAYER_TEXTURED);
  window->Show();

  if (bounds.IsEmpty()) {
    SetDefaultParentByPrimaryRootWindow(window);
  } else {
    gfx::Display display =
        Shell::GetScreen()->GetDisplayMatching(bounds);
    aura::RootWindow* root = ash::Shell::GetInstance()->display_controller()->
        GetRootWindowForDisplayId(display.id());
    gfx::Point origin = bounds.origin();
    wm::ConvertPointFromScreen(root, &origin);
    window->SetBounds(gfx::Rect(origin, bounds.size()));
    window->SetDefaultParentByRootWindow(root, bounds);
  }
  window->SetProperty(aura::client::kCanMaximizeKey, true);
  return window;
}

void AshTestBase::SetDefaultParentByPrimaryRootWindow(aura::Window* window) {
  window->SetDefaultParentByRootWindow(
      Shell::GetPrimaryRootWindow(), gfx::Rect());
}

void AshTestBase::RunAllPendingInMessageLoop() {
  ash_test_helper_->RunAllPendingInMessageLoop();
}

void AshTestBase::SetSessionStarted(bool session_started) {
  ash_test_helper_->test_shell_delegate()->test_session_state_delegate()->
      SetActiveUserSessionStarted(session_started);
}

void AshTestBase::SetUserLoggedIn(bool user_logged_in) {
  ash_test_helper_->test_shell_delegate()->test_session_state_delegate()->
      SetHasActiveUser(user_logged_in);
}

void AshTestBase::SetCanLockScreen(bool can_lock_screen) {
  ash_test_helper_->test_shell_delegate()->test_session_state_delegate()->
      SetCanLockScreen(can_lock_screen);
}

void AshTestBase::SetUserAddingScreenRunning(bool user_adding_screen_running) {
  ash_test_helper_->test_shell_delegate()->test_session_state_delegate()->
      SetUserAddingScreenRunning(user_adding_screen_running);
}

void AshTestBase::BlockUserSession(UserSessionBlockReason block_reason) {
  switch (block_reason) {
    case BLOCKED_BY_LOCK_SCREEN:
      SetSessionStarted(true);
      SetUserAddingScreenRunning(false);
      Shell::GetInstance()->session_state_delegate()->LockScreen();
      break;
    case BLOCKED_BY_LOGIN_SCREEN:
      SetUserAddingScreenRunning(false);
      SetSessionStarted(false);
      break;
    case BLOCKED_BY_USER_ADDING_SCREEN:
      SetUserAddingScreenRunning(true);
      SetSessionStarted(true);
      break;
    default:
      NOTREACHED();
      break;
  }
}

void AshTestBase::UnblockUserSession() {
  Shell::GetInstance()->session_state_delegate()->UnlockScreen();
  SetSessionStarted(true);
  SetUserAddingScreenRunning(false);
}


}  // namespace test
}  // namespace ash
