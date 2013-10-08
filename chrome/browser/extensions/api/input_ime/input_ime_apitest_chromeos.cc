// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/api/test/test_api.h"
#include "chrome/common/chrome_switches.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_CHROMEOS)
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, InputImeApiBasic) {
  ASSERT_TRUE(RunExtensionTest("input_ime")) << message_;
}
#endif
