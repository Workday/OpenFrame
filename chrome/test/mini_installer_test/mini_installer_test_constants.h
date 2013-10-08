// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Constants related to the Chrome mini installer testing.

#ifndef CHROME_TEST_MINI_INSTALLER_TEST_MINI_INSTALLER_TEST_CONSTANTS_H__
#define CHROME_TEST_MINI_INSTALLER_TEST_MINI_INSTALLER_TEST_CONSTANTS_H__

namespace mini_installer_constants {

// Path and process names
extern const wchar_t kChromeAppDir[];
extern const wchar_t kChromeFrameAppDir[];
extern const wchar_t kChromeFrameProductName[];
extern const wchar_t kChromeMetaInstallerExecutable[];
extern const wchar_t kChromeProductName[];
extern const wchar_t kChromeMiniInstallerExecutable[];
extern const wchar_t kChromeSetupExecutable[];
extern const wchar_t kChromeUserDataBackupDir[];
extern const wchar_t kChromeUserDataDir[];
extern const wchar_t kDiffInstall[];
extern const wchar_t kDiffInstallerPattern[];
extern const wchar_t kFullInstall[];
extern const wchar_t kFullInstallerPattern[];
extern const wchar_t kGoogleUpdateExecutable[];
extern const wchar_t kIEExecutable[];
extern const wchar_t kWinFolder[];

// Window names.
extern const wchar_t kBrowserTabName[];
extern const wchar_t kChromeBuildType[];
extern const wchar_t kChromeFrameAppName[];
extern const wchar_t kChromeFirstRunUI[];
extern const wchar_t kChromeUninstallDialogName[];
extern const wchar_t kInstallerWindow[];

// Shortcut names
extern const wchar_t kChromeLaunchShortcut[];
extern const wchar_t kChromeUninstallShortcut[];

// Chrome install types
extern const wchar_t kStandaloneInstaller[];
extern const wchar_t kUntaggedInstallerPattern[];

// Channel types
extern const wchar_t kDevChannelBuild[];
extern const wchar_t kStableChannelBuild[];

extern const wchar_t kIELocation[];
extern const wchar_t kIEProcessName[];

// Google Chrome meta installer location.
extern const wchar_t kChromeApplyTagExe[];
extern const wchar_t kChromeApplyTagParameters[];
extern const wchar_t kChromeInstallersLocation[];
extern const wchar_t kChromeMetaInstallerExe[];
extern const wchar_t kChromeStandAloneInstallerLocation[];
}  // namespace mini_installer_constants

// Command line switches.
namespace switches {
extern const char kInstallerHelp[];
extern const char kInstallerTestBackup[];
extern const char kInstallerTestBuild[];
extern const char kInstallerTestForce[];
}  // namespace switches

#endif  // CHROME_TEST_MINI_INSTALLER_TEST_MINI_INSTALLER_TEST_CONSTANTS_H__
