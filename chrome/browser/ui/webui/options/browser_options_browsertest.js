// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * TestFixture for browser options WebUI testing.
 * @extends {testing.Test}
 * @constructor
 **/
function BrowserOptionsWebUITest() {}

BrowserOptionsWebUITest.prototype = {
  __proto__: testing.Test.prototype,

  /**
   * Browse to browser options.
   **/
  browsePreload: 'chrome://chrome/settings/',
};

// Test opening the browser options has correct location.
// Times out on Mac debug only. See http://crbug.com/121030
GEN('#if defined(OS_MACOSX) && !defined(NDEBUG)');
GEN('#define MAYBE_testOpenBrowserOptions ' +
    'DISABLED_testOpenBrowserOptions');
GEN('#else');
GEN('#define MAYBE_testOpenBrowserOptions testOpenBrowserOptions');
GEN('#endif  // defined(OS_MACOSX)');
TEST_F('BrowserOptionsWebUITest', 'MAYBE_testOpenBrowserOptions', function() {
  assertEquals(this.browsePreload, document.location.href);
});
