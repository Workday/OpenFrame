// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/test/test_suite.h"

#include "ash/ash_switches.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/gfx/gfx_paths.h"

#if defined(OS_MACOSX)
#include "ash/test/test_suite_init.h"
#endif

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#include "ui/base/win/atl_module.h"
#include "win8/test/metro_registration_helper.h"
#include "win8/test/test_registrar_constants.h"
#endif

namespace ash {
namespace test {

AuraShellTestSuite::AuraShellTestSuite(int argc, char** argv)
    : TestSuite(argc, argv) {}

void AuraShellTestSuite::Initialize() {
  base::TestSuite::Initialize();

#if defined(OS_WIN)
  if (base::win::GetVersion() >= base::win::VERSION_WIN8 &&
      !CommandLine::ForCurrentProcess()->HasSwitch(
          ash::switches::kForceAshToDesktop)) {
    com_initializer_.reset(new base::win::ScopedCOMInitializer());
    ui::win::CreateATLModuleIfNeeded();
    ASSERT_TRUE(win8::MakeTestDefaultBrowserSynchronously());
  }
#endif

  gfx::RegisterPathProvider();
  ui::RegisterPathProvider();

#if defined(OS_MACOSX)
  ash::test::OverrideFrameworkBundle();
#endif

  // Force unittests to run using en-US so if we test against string
  // output, it'll pass regardless of the system language.
  ui::ResourceBundle::InitSharedInstanceWithLocale("en-US", NULL);
}

void AuraShellTestSuite::Shutdown() {
  ui::ResourceBundle::CleanupSharedInstance();
#if defined(OS_WIN)
  com_initializer_.reset();
#endif
  base::TestSuite::Shutdown();
}

}  // namespace test
}  // namespace ash
