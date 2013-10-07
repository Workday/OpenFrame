// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/ash_init.h"

#include "ash/accelerators/accelerator_controller.h"
#include "ash/ash_switches.h"
#include "ash/high_contrast/high_contrast_controller.h"
#include "ash/magnifier/magnification_controller.h"
#include "ash/magnifier/partial_magnification_controller.h"
#include "ash/shell.h"
#include "ash/wm/event_rewriter_event_filter.h"
#include "base/command_line.h"
#include "chrome/browser/browser_shutdown.h"
#include "chrome/browser/chromeos/accessibility/accessibility_manager.h"
#include "chrome/browser/chromeos/accessibility/magnification_manager.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/ui/ash/chrome_shell_delegate.h"
#include "chrome/browser/ui/ash/event_rewriter.h"
#include "chrome/browser/ui/ash/screenshot_taker.h"
#include "chrome/common/chrome_switches.h"
#include "ui/aura/env.h"
#include "ui/aura/root_window.h"

#if defined(OS_CHROMEOS)
#include "base/chromeos/chromeos_version.h"
#include "chrome/browser/ui/ash/brightness_controller_chromeos.h"
#include "chrome/browser/ui/ash/ime_controller_chromeos.h"
#include "chrome/browser/ui/ash/volume_controller_chromeos.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/login/login_state.h"
#include "ui/base/x/x11_util.h"
#endif

namespace chrome {

bool ShouldOpenAshOnStartup() {
#if defined(OS_CHROMEOS)
  return true;
#endif
  // TODO(scottmg): http://crbug.com/133312, will need this for Win8 too.
  return CommandLine::ForCurrentProcess()->HasSwitch(switches::kOpenAsh);
}

void OpenAsh() {
#if defined(OS_CHROMEOS)
  if (base::chromeos::IsRunningOnChromeOS()) {
    // Hides the cursor outside of the Aura root window. The cursor will be
    // drawn within the Aura root window, and it'll remain hidden after the
    // Aura window is closed.
    ui::HideHostCursor();
  }

  // Hide the mouse cursor completely at boot.
  if (!chromeos::LoginState::Get()->IsUserLoggedIn())
    ash::Shell::set_initially_hide_cursor(true);
#endif

  // Shell takes ownership of ChromeShellDelegate.
  ash::Shell* shell = ash::Shell::CreateInstance(new ChromeShellDelegate);
  shell->event_rewriter_filter()->SetEventRewriterDelegate(
      scoped_ptr<ash::EventRewriterDelegate>(new EventRewriter).Pass());
  shell->accelerator_controller()->SetScreenshotDelegate(
      scoped_ptr<ash::ScreenshotDelegate>(new ScreenshotTaker).Pass());
#if defined(OS_CHROMEOS)
  shell->accelerator_controller()->SetBrightnessControlDelegate(
      scoped_ptr<ash::BrightnessControlDelegate>(
          new BrightnessController).Pass());
  shell->accelerator_controller()->SetImeControlDelegate(
      scoped_ptr<ash::ImeControlDelegate>(new ImeController).Pass());
  ash::Shell::GetInstance()->high_contrast_controller()->SetEnabled(
      chromeos::AccessibilityManager::Get()->IsHighContrastEnabled());

  DCHECK(chromeos::MagnificationManager::Get());
  bool magnifier_enabled =
      chromeos::MagnificationManager::Get()->IsMagnifierEnabled();
  ash::MagnifierType magnifier_type =
      chromeos::MagnificationManager::Get()->GetMagnifierType();
  ash::Shell::GetInstance()->magnification_controller()->
      SetEnabled(magnifier_enabled && magnifier_type == ash::MAGNIFIER_FULL);
  ash::Shell::GetInstance()->partial_magnification_controller()->
      SetEnabled(magnifier_enabled && magnifier_type == ash::MAGNIFIER_PARTIAL);

  if (!CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableZeroBrowsersOpenForTests)) {
    chrome::StartKeepAlive();
  }
#endif
  ash::Shell::GetPrimaryRootWindow()->ShowRootWindow();
}

void CloseAsh() {
  // If shutdown is initiated by |BrowserX11IOErrorHandler|, don't
  // try to cleanup resources.
  if (!browser_shutdown::ShuttingDownWithoutClosingBrowsers() &&
      ash::Shell::HasInstance()) {
    ash::Shell::DeleteInstance();
  }
}

}  // namespace chrome
