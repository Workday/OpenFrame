// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

GEN('#if !defined(OS_CHROMEOS)');

/**
 * Test fixture for sync setup WebUI testing.
 * @constructor
 * @extends {testing.Test}
 */
function SyncSetupWebUITest() {}

SyncSetupWebUITest.prototype = {
  __proto__: testing.Test.prototype,

  /**
   * Browse to the settings sub-frame.
   */
  browsePreload: 'chrome://settings-frame',

  /** @override */
  preLoad: function() {
    this.makeAndRegisterMockHandler(['SyncSetupConfigure',
                                     'SyncSetupShowSetupUI',
                                     'SyncSetupStartSignIn',
                                    ]);
  },

  /**
   * Verifies starting point is not synced.
   */
  verifyUnsynced: function() {
    assertFalse(BrowserOptions.getInstance().signedIn_);
  },

  /**
   * Clicks the "Sign in to Chrome" button.
   */
  startSyncing: function() {
    var startStopSyncButton = BrowserOptions.getStartStopSyncButton();
    assertNotEquals(null, startStopSyncButton);
    startStopSyncButton.click();
  },
};

/**
 * Async version of SyncSetupWebUITest.
 * @extends {SyncSetupWebUITest}
 * @constructor
 */
function SyncSetupWebUITestAsync() {}

SyncSetupWebUITestAsync.prototype = {
  __proto__: SyncSetupWebUITest.prototype,

  /** @override */
  isAsync: true,

  /**
   * Verifies that initial state is unsynced and simulates signing in.
   * @override
   */
  setUp: function() {
    SyncSetupWebUITest.prototype.setUp.call(this);

    // For testing, don't wait to execute timeouts.
    var oldSetTimeout = setTimeout;
    setTimeout = function(fn, timeout) {
      oldSetTimeout(fn, 0);
    };

    // Make sure the user is not starting off in the signed in or syncing state.
    this.verifyUnsynced();

    // Handle SyncSetupShowSetupUI by navigating to chrome://settings/syncSetup.
    this.mockHandler.expects(once()).SyncSetupShowSetupUI().will(callFunction(
        function() {
      PageManager.showPageByName('syncSetup');
    }));
  },
};

TEST_F('SyncSetupWebUITestAsync', 'VerifySignIn', function() {
  // Handle SyncSetupStartSignIn by displaying the sync setup dialog, verifying
  // that a confirmation dialog appears, and clicking OK to dismiss the dialog.
  // Note that this test doesn't actually do a gaia sign in.
  this.mockHandler.expects(once()).SyncSetupStartSignIn().will(callFunction(
      function() {
    SyncSetupOverlay.showSyncSetupPage('configure');
    var okButton = $('confirm-everything-ok');
    assertNotEquals(null, okButton);
    okButton.click();
  }));

  // The test completes after the sync config is sent out.
  this.mockHandler.expects(once()).SyncSetupConfigure(ANYTHING).
      will(callFunction(testDone));

  // Click the "Sign in to Chrome..." button.
  this.startSyncing();
});

// Test that switching to and from "Sync everything" saves and restores types.
TEST_F('SyncSetupWebUITestAsync', 'RestoreSyncDataTypes', function() {
  this.mockHandler.expects(once()).SyncSetupStartSignIn().will(callFunction(
      function() {
    SyncSetupOverlay.showSyncSetupPage('configure', {});

    $('sync-select-datatypes').selectedIndex = 1;
    cr.dispatchSimpleEvent($('sync-select-datatypes'), 'change', true);

    $('bookmarks-checkbox').checked = false;
    cr.dispatchSimpleEvent($('bookmarks-checkbox'), 'change', true);

    $('sync-select-datatypes').selectedIndex = 0;
    cr.dispatchSimpleEvent($('sync-select-datatypes'), 'change', true);
    assertTrue($('bookmarks-checkbox').checked);

    $('sync-select-datatypes').selectedIndex = 1;
    cr.dispatchSimpleEvent($('sync-select-datatypes'), 'change', true);
    assertFalse($('bookmarks-checkbox').checked);

    testDone();
  }));

  this.startSyncing();
});

GEN('#endif  // OS_CHROMEOS');
