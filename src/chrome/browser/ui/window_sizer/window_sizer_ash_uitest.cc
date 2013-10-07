// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/launcher/launcher.h"
#include "ash/launcher/launcher_view.h"
#include "ash/shell.h"
#include "base/command_line.h"
#include "base/message_loop/message_loop.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "ui/aura/root_window.h"
#include "ui/aura/test/event_generator.h"
#include "ui/base/test/ui_controls.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/view.h"
#include "ui/views/view_model.h"

namespace {

class WindowSizerTest : public InProcessBrowserTest {
 public:
  WindowSizerTest() {}
  virtual ~WindowSizerTest() {}

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    // Make screens sufficiently wide to host 2 browsers side by side.
    command_line->AppendSwitchASCII("ash-host-window-bounds",
                                    "600x600,601+0-600x600");
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(WindowSizerTest);
};

void CloseBrowser(Browser* browser) {
  browser->window()->Close();
  base::MessageLoop::current()->RunUntilIdle();
}

gfx::Rect GetChromeIconBoundsForRootWindow(aura::RootWindow* root_window) {
  ash::Launcher* launcher = ash::Launcher::ForWindow(root_window);
  const ash::internal::LauncherView* launcher_view =
      launcher->GetLauncherViewForTest();
  const views::ViewModel* view_model = launcher_view->view_model_for_test();

  EXPECT_EQ(2, view_model->view_size());
  return view_model->view_at(0)->GetBoundsInScreen();
}

void OpenBrowserUsingShelfOnRootWindow(aura::RootWindow* root_window) {
  aura::test::EventGenerator generator(root_window);
  gfx::Point center =
      GetChromeIconBoundsForRootWindow(root_window).CenterPoint();
  gfx::Display display =
      ash::Shell::GetScreen()->GetDisplayNearestWindow(root_window);
  const gfx::Point& origin = display.bounds().origin();
  center.Offset(- origin.x(), - origin.y());
  generator.MoveMouseTo(center);
  generator.ClickLeftButton();
}

}  // namespace

#if defined(OS_WIN)
#define MAYBE_OpenBrowserUsingShelfOnOhterDisplay DISABLED_OpenBrowserUsingShelfOnOhterDisplay
#define MAYBE_OpenBrowserUsingContextMenuOnOhterDisplay DISABLED_OpenBrowserUsingContextMenuOnOhterDisplay
#else
#define MAYBE_OpenBrowserUsingShelfOnOhterDisplay OpenBrowserUsingShelfOnOhterDisplay
#define MAYBE_OpenBrowserUsingContextMenuOnOhterDisplay OpenBrowserUsingContextMenuOnOhterDisplay
#endif

IN_PROC_BROWSER_TEST_F(WindowSizerTest,
                       MAYBE_OpenBrowserUsingShelfOnOhterDisplay) {
  // Don't shutdown when closing the last browser window.
  chrome::StartKeepAlive();

  ash::Shell::RootWindowList root_windows = ash::Shell::GetAllRootWindows();

  BrowserList* browser_list =
      BrowserList::GetInstance(chrome::HOST_DESKTOP_TYPE_ASH);

  EXPECT_EQ(1u, browser_list->size());
  // Close the browser window so that clicking icon will create a new window.
  CloseBrowser(browser_list->get(0));
  EXPECT_EQ(0u, browser_list->size());
  EXPECT_EQ(root_windows[0], ash::Shell::GetActiveRootWindow());

  OpenBrowserUsingShelfOnRootWindow(root_windows[1]);

  // A new browser must be created on 2nd display.
  EXPECT_EQ(1u, browser_list->size());
  EXPECT_EQ(root_windows[1],
            browser_list->get(0)->window()->GetNativeWindow()->GetRootWindow());
  EXPECT_EQ(root_windows[1], ash::Shell::GetActiveRootWindow());

  // Close the browser window so that clicking icon will create a new window.
  CloseBrowser(browser_list->get(0));
  EXPECT_EQ(0u, browser_list->size());

  OpenBrowserUsingShelfOnRootWindow(root_windows[0]);

  // A new browser must be created on 1st display.
  EXPECT_EQ(1u, browser_list->size());
  EXPECT_EQ(root_windows[0],
            browser_list->get(0)->window()->GetNativeWindow()->GetRootWindow());
  EXPECT_EQ(root_windows[0], ash::Shell::GetActiveRootWindow());

  // Balanced with the chrome::StartKeepAlive above.
  chrome::EndKeepAlive();
}

namespace {

class WindowSizerContextMenuTest : public WindowSizerTest {
 public:
  WindowSizerContextMenuTest() {}
  virtual ~WindowSizerContextMenuTest() {}

  static void Step1(gfx::Point release_point) {
    ui_controls::SendMouseEventsNotifyWhenDone(
        ui_controls::RIGHT, ui_controls::DOWN,
        base::Bind(&WindowSizerContextMenuTest::Step2, release_point));
  }

  static void Step2(gfx::Point release_point) {
    ui_controls::SendMouseMoveNotifyWhenDone(
        release_point.x(), release_point.y(),
        base::Bind(&WindowSizerContextMenuTest::Step3));
  }

  static void Step3() {
    ui_controls::SendMouseEventsNotifyWhenDone(
        ui_controls::RIGHT, ui_controls::UP,
        base::Bind(&WindowSizerContextMenuTest::QuitLoop));
  }

  static void QuitLoop() {
    base::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::MessageLoop::QuitWhenIdleClosure());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(WindowSizerContextMenuTest);
};

void OpenBrowserUsingContextMenuOnRootWindow(aura::RootWindow* root_window) {
  gfx::Point chrome_icon =
      GetChromeIconBoundsForRootWindow(root_window).CenterPoint();
  gfx::Point release_point = chrome_icon;
  release_point.Offset(50, -120);
  ui_controls::SendMouseMoveNotifyWhenDone(
      chrome_icon.x(), chrome_icon.y(),
      base::Bind(&WindowSizerContextMenuTest::Step1, release_point));
  base::MessageLoop::current()->Run();
}

}  // namespace

IN_PROC_BROWSER_TEST_F(WindowSizerContextMenuTest,
                       MAYBE_OpenBrowserUsingContextMenuOnOhterDisplay) {
  // Don't shutdown when closing the last browser window.
  chrome::StartKeepAlive();

  views::MenuController::TurnOffContextMenuSelectionHoldForTest();

  ash::Shell::RootWindowList root_windows = ash::Shell::GetAllRootWindows();

  BrowserList* browser_list =
      BrowserList::GetInstance(chrome::HOST_DESKTOP_TYPE_ASH);

  EXPECT_EQ(1u, browser_list->size());
  EXPECT_EQ(root_windows[0], ash::Shell::GetActiveRootWindow());
  CloseBrowser(browser_list->get(0));

  OpenBrowserUsingContextMenuOnRootWindow(root_windows[1]);

  // A new browser must be created on 2nd display.
  EXPECT_EQ(1u, browser_list->size());
  EXPECT_EQ(root_windows[1],
            browser_list->get(0)->window()->GetNativeWindow()->GetRootWindow());
  EXPECT_EQ(root_windows[1], ash::Shell::GetActiveRootWindow());

  OpenBrowserUsingContextMenuOnRootWindow(root_windows[0]);

  // Next new browser must be created on 1st display.
  EXPECT_EQ(2u, browser_list->size());
  EXPECT_EQ(root_windows[0],
            browser_list->get(1)->window()->GetNativeWindow()->GetRootWindow());
  EXPECT_EQ(root_windows[0], ash::Shell::GetActiveRootWindow());

  // Balanced with the chrome::StartKeepAlive above.
  chrome::EndKeepAlive();
}
