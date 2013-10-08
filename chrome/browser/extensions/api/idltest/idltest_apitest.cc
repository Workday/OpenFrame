// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_apitest.h"
#include "extensions/common/switches.h"

class ExtensionIdltestApiTest : public ExtensionApiTest {
 public:
  ExtensionIdltestApiTest() {}
  virtual ~ExtensionIdltestApiTest() {}

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    ExtensionApiTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(
        extensions::switches::kEnableExperimentalExtensionApis);
  }
};

IN_PROC_BROWSER_TEST_F(ExtensionIdltestApiTest, IdlCompiler) {
  EXPECT_TRUE(RunExtensionSubtest("idltest/binary_data", "binary.html"));
  EXPECT_TRUE(RunExtensionSubtest("idltest/nocompile", "nocompile.html"));
};
