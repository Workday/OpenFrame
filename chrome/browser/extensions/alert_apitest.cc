// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_modal_dialogs/app_modal_dialog.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_view_host.h"

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, AlertBasic) {
  ASSERT_TRUE(RunExtensionTest("alert")) << message_;

  const extensions::Extension* extension = GetSingleLoadedExtension();
  extensions::ExtensionHost* host =
      extensions::ExtensionSystem::Get(browser()->profile())->
          process_manager()->GetBackgroundHostForExtension(extension->id());
  ASSERT_TRUE(host);
  host->render_view_host()->ExecuteJavascriptInWebFrame(string16(),
      ASCIIToUTF16("alert('This should not crash.');"));

  AppModalDialog* alert = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(alert);
  alert->CloseModalDialog();
}
