// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "chrome/common/extensions/manifest_handlers/content_scripts_handler.h"
#include "chrome/common/extensions/manifest_tests/extension_manifest_test.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/switches.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace errors = extension_manifest_errors;

namespace extensions {

class ContentScriptsManifestTest : public ExtensionManifestTest {
};

TEST_F(ContentScriptsManifestTest, MatchPattern) {
  Testcase testcases[] = {
    // chrome:// urls are not allowed.
    Testcase("content_script_chrome_url_invalid.json",
             ErrorUtils::FormatErrorMessage(
                 errors::kInvalidMatch,
                 base::IntToString(0),
                 base::IntToString(0),
                 URLPattern::GetParseResultString(
                     URLPattern::PARSE_ERROR_INVALID_SCHEME))),

    // Match paterns must be strings.
    Testcase("content_script_match_pattern_not_string.json",
             ErrorUtils::FormatErrorMessage(errors::kInvalidMatch,
                                            base::IntToString(0),
                                            base::IntToString(0),
                                            errors::kExpectString))
  };
  RunTestcases(testcases, arraysize(testcases),
               EXPECT_TYPE_ERROR);

  LoadAndExpectSuccess("ports_in_content_scripts.json");
}

TEST_F(ContentScriptsManifestTest, OnChromeUrlsWithFlag) {
  CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kExtensionsOnChromeURLs);
  scoped_refptr<Extension> extension =
    LoadAndExpectSuccess("content_script_chrome_url_invalid.json");
  const GURL newtab_url("chrome://newtab/");
  EXPECT_TRUE(
      ContentScriptsInfo::ExtensionHasScriptAtURL(extension.get(), newtab_url));
}

TEST_F(ContentScriptsManifestTest, ScriptableHosts) {
  // TODO(yoz): Test GetScriptableHosts.
  scoped_refptr<Extension> extension =
      LoadAndExpectSuccess("content_script_yahoo.json");
  URLPatternSet scriptable_hosts =
      ContentScriptsInfo::GetScriptableHosts(extension.get());

  URLPatternSet expected;
  expected.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://yahoo.com/*"));

  EXPECT_EQ(expected, scriptable_hosts);
}

}  // namespace extensions
