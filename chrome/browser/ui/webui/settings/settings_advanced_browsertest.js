// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @extends {testing.Test}
 * @constructor
 */
function SettingsAdvancedBrowserTest() {}

SettingsAdvancedBrowserTest.prototype = {
  __proto__: testing.Test.prototype,

  /** @override */
  browsePreload: 'chrome://md-settings/advanced',

  /**
   * TODO(dpapad): Fix accessibility issues and enable checks.
   * @override
   */
  runAccessibilityChecks: false,
};

// Flaky timeout failures on Linux Tests (dbg) and Win7 Tests (dbg); see
// https://crbug.com/558434.
TEST_F('SettingsAdvancedBrowserTest', 'DISABLED_NoConsoleErrors', function() {
  assertEquals(this.browsePreload, document.location.href);
  // Nothing else to assert here. If there are errors in the console the test
  // will automatically fail.
});
