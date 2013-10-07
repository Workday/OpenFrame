// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chrome_browser_main_android.h"

#include "base/command_line.h"
#include "base/debug/trace_event.h"
#include "base/path_service.h"
#include "cc/base/switches.h"
#include "chrome/app/breakpad_linux.h"
#include "chrome/browser/android/crash_dump_manager.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/browser/android/compositor.h"
#include "content/public/common/main_function_params.h"
#include "net/android/network_change_notifier_factory_android.h"
#include "net/base/network_change_notifier.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"

ChromeBrowserMainPartsAndroid::ChromeBrowserMainPartsAndroid(
    const content::MainFunctionParams& parameters)
    : ChromeBrowserMainParts(parameters) {
}

ChromeBrowserMainPartsAndroid::~ChromeBrowserMainPartsAndroid() {
}

void ChromeBrowserMainPartsAndroid::PreProfileInit() {
  TRACE_EVENT0("startup", "ChromeBrowserMainPartsAndroid::PreProfileInit")
#if defined(GOOGLE_CHROME_BUILD)
  // TODO(jcivelli): we should not initialize the crash-reporter when it was not
  // enabled. Right now if it is disabled we still generate the minidumps but we
  // do not upload them.
  bool breakpad_enabled = true;
#else
  bool breakpad_enabled = false;
#endif

  // Allow Breakpad to be enabled in Chromium builds for testing purposes.
  if (!breakpad_enabled)
    breakpad_enabled = CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableCrashReporterForTesting);

  if (breakpad_enabled) {
    InitCrashReporter();
    crash_dump_manager_.reset(new CrashDumpManager());
  }

  ChromeBrowserMainParts::PreProfileInit();
}

void ChromeBrowserMainPartsAndroid::PreEarlyInitialization() {
  TRACE_EVENT0("startup",
    "ChromeBrowserMainPartsAndroid::PreEarlyInitialization")
  net::NetworkChangeNotifier::SetFactory(
      new net::NetworkChangeNotifierFactoryAndroid());

  content::Compositor::Initialize();

  // Chrome on Android does not use default MessageLoop. It has its own
  // Android specific MessageLoop.
  DCHECK(!main_message_loop_.get());

  // Create and start the MessageLoop.
  // This is a critical point in the startup process.
  {
    TRACE_EVENT0("startup",
      "ChromeBrowserMainPartsAndroid::PreEarlyInitialization:CreateUiMsgLoop");
    main_message_loop_.reset(new base::MessageLoop(base::MessageLoop::TYPE_UI));
  }

  {
    TRACE_EVENT0("startup",
      "ChromeBrowserMainPartsAndroid::PreEarlyInitialization:StartUiMsgLoop");
    base::MessageLoopForUI::current()->Start();
  }

  CommandLine::ForCurrentProcess()->AppendSwitch(
      cc::switches::kCompositeToMailbox);

  ChromeBrowserMainParts::PreEarlyInitialization();
}

void ChromeBrowserMainPartsAndroid::ShowMissingLocaleMessageBox() {
  NOTREACHED();
}
