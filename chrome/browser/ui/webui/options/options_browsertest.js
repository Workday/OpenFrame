// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

GEN('#include "chrome/browser/ui/webui/options/options_browsertest.h"');

/**
 * Wait for the global window.onpopstate callback to be called (after a tab
 * history navigation), then execute |afterFunction|.
 */
function waitForPopstate(afterFunction) {
  var originalCallback = window.onpopstate;

  // Install a wrapper that temporarily replaces the original function.
  window.onpopstate = function() {
    window.onpopstate = originalCallback;
    originalCallback.apply(this, arguments);
    afterFunction();
  };
}

/**
 * TestFixture for OptionsPage WebUI testing.
 * @extends {testing.Test}
 * @constructor
 */
function OptionsWebUITest() {}

OptionsWebUITest.prototype = {
  __proto__: testing.Test.prototype,

  /**
   * Browse to the options page & call our preLoad().
   */
  browsePreload: 'chrome://settings-frame',

  isAsync: true,

  /**
   * Register a mock handler to ensure expectations are met and options pages
   * behave correctly.
   */
  preLoad: function() {
    this.makeAndRegisterMockHandler(
        ['defaultZoomFactorAction',
         'fetchPrefs',
         'observePrefs',
         'setBooleanPref',
         'setIntegerPref',
         'setDoublePref',
         'setStringPref',
         'setObjectPref',
         'clearPref',
         'coreOptionsUserMetricsAction',
        ]);

    // Register stubs for methods expected to be called before/during tests.
    // Specific expectations can be made in the tests themselves.
    this.mockHandler.stubs().fetchPrefs(ANYTHING);
    this.mockHandler.stubs().observePrefs(ANYTHING);
    this.mockHandler.stubs().coreOptionsUserMetricsAction(ANYTHING);
  },
};

// Crashes on Mac only. See http://crbug.com/79181
GEN('#if defined(OS_MACOSX)');
GEN('#define MAYBE_testSetBooleanPrefTriggers ' +
    'DISABLED_testSetBooleanPrefTriggers');
GEN('#else');
GEN('#define MAYBE_testSetBooleanPrefTriggers testSetBooleanPrefTriggers');
GEN('#endif  // defined(OS_MACOSX)');

TEST_F('OptionsWebUITest', 'MAYBE_testSetBooleanPrefTriggers', function() {
  // TODO(dtseng): make generic to click all buttons.
  var showHomeButton = $('show-home-button');
  var trueListValue = [
    'browser.show_home_button',
    true,
    'Options_Homepage_HomeButton',
  ];
  // Note: this expectation is checked in testing::Test::tearDown.
  this.mockHandler.expects(once()).setBooleanPref(trueListValue);

  // Cause the handler to be called.
  showHomeButton.click();
  showHomeButton.blur();
  testDone();
});

// Not meant to run on ChromeOS at this time.
// Not finishing in windows. http://crbug.com/81723
TEST_F('OptionsWebUITest', 'DISABLED_testRefreshStaysOnCurrentPage',
    function() {
  assertTrue($('search-engine-manager-page').hidden);
  var item = $('manage-default-search-engines');
  item.click();

  assertFalse($('search-engine-manager-page').hidden);

  window.location.reload();

  assertEquals('chrome://settings-frame/searchEngines', document.location.href);
  assertFalse($('search-engine-manager-page').hidden);
  testDone();
});

/**
 * Test the default zoom factor select element.
 */
TEST_F('OptionsWebUITest', 'testDefaultZoomFactor', function() {
  // The expected minimum length of the |defaultZoomFactor| element.
  var defaultZoomFactorMinimumLength = 10;
  // Verify that the zoom factor element exists.
  var defaultZoomFactor = $('defaultZoomFactor');
  assertNotEquals(defaultZoomFactor, null);

  // Verify that the zoom factor element has a reasonable number of choices.
  expectGE(defaultZoomFactor.options.length, defaultZoomFactorMinimumLength);

  // Simulate a change event, selecting the highest zoom value.  Verify that
  // the javascript handler was invoked once.
  this.mockHandler.expects(once()).defaultZoomFactorAction(NOT_NULL).
      will(callFunction(function() { }));
  defaultZoomFactor.selectedIndex = defaultZoomFactor.options.length - 1;
  var event = { target: defaultZoomFactor };
  if (defaultZoomFactor.onchange) defaultZoomFactor.onchange(event);
  testDone();
});

// If |confirmInterstitial| is true, the OK button of the Do Not Track
// interstitial is pressed, otherwise the abort button is pressed.
OptionsWebUITest.prototype.testDoNotTrackInterstitial =
    function(confirmInterstitial) {
  Preferences.prefsFetchedCallback({'enable_do_not_track': {'value': false } });
  var buttonToClick = confirmInterstitial ? $('do-not-track-confirm-ok')
                                          : $('do-not-track-confirm-cancel');
  var dntCheckbox = $('do-not-track-enabled');
  var dntOverlay = OptionsPage.registeredOverlayPages['donottrackconfirm'];
  assertFalse(dntCheckbox.checked);

  var visibleChangeCounter = 0;
  var visibleChangeHandler = function() {
    ++visibleChangeCounter;
    switch (visibleChangeCounter) {
      case 1:
        window.setTimeout(function() {
          assertTrue(dntOverlay.visible);
          buttonToClick.click();
        }, 0);
        break;
      case 2:
        window.setTimeout(function() {
          assertFalse(dntOverlay.visible);
          assertEquals(confirmInterstitial, dntCheckbox.checked);
          dntOverlay.removeEventListener(visibleChangeHandler);
          testDone();
        }, 0);
        break;
      default:
        assertTrue(false);
    }
  }
  dntOverlay.addEventListener('visibleChange', visibleChangeHandler);

  if (confirmInterstitial) {
    this.mockHandler.expects(once()).setBooleanPref(
        ['enable_do_not_track', true, 'Options_DoNotTrackCheckbox']);
  } else {
    // The mock handler complains if setBooleanPref is called even though
    // it should not be.
  }

  dntCheckbox.click();
}

TEST_F('OptionsWebUITest', 'EnableDoNotTrackAndConfirmInterstitial',
       function() {
  this.testDoNotTrackInterstitial(true);
});

TEST_F('OptionsWebUITest', 'EnableDoNotTrackAndCancelInterstitial',
       function() {
  this.testDoNotTrackInterstitial(false);
});

// Check that the "Do not Track" preference can be correctly disabled.
// In order to do that, we need to enable it first.
TEST_F('OptionsWebUITest', 'EnableAndDisableDoNotTrack', function() {
  Preferences.prefsFetchedCallback({'enable_do_not_track': {'value': false } });
  var dntCheckbox = $('do-not-track-enabled');
  var dntOverlay = OptionsPage.registeredOverlayPages['donottrackconfirm'];
  assertFalse(dntCheckbox.checked);

  var visibleChangeCounter = 0;
  var visibleChangeHandler = function() {
    ++visibleChangeCounter;
    switch (visibleChangeCounter) {
      case 1:
        window.setTimeout(function() {
          assertTrue(dntOverlay.visible);
          $('do-not-track-confirm-ok').click();
        }, 0);
        break;
      case 2:
        window.setTimeout(function() {
          assertFalse(dntOverlay.visible);
          assertTrue(dntCheckbox.checked);
          dntOverlay.removeEventListener(visibleChangeHandler);
          dntCheckbox.click();
        }, 0);
        break;
      default:
        assertNotReached();
    }
  }
  dntOverlay.addEventListener('visibleChange', visibleChangeHandler);

  this.mockHandler.expects(once()).setBooleanPref(
      eq(["enable_do_not_track", true, 'Options_DoNotTrackCheckbox']));

  var verifyCorrectEndState = function() {
    window.setTimeout(function() {
      assertFalse(dntOverlay.visible);
      assertFalse(dntCheckbox.checked);
      testDone();
    }, 0)
  }
  this.mockHandler.expects(once()).setBooleanPref(
      eq(["enable_do_not_track", false, 'Options_DoNotTrackCheckbox'])).will(
          callFunction(verifyCorrectEndState));

  dntCheckbox.click();
});

// Verify that preventDefault() is called on 'Enter' keydown events that trigger
// the default button. If this doesn't happen, other elements that may get
// focus (by the overlay closing for instance), will execute in addition to the
// default button. See crbug.com/268336.
TEST_F('OptionsWebUITest', 'EnterPreventsDefault', function() {
  var page = HomePageOverlay.getInstance();
  OptionsPage.showPageByName(page.name);
  var event = new KeyboardEvent('keydown', {
    'bubbles': true,
    'cancelable': true,
    'keyIdentifier': 'Enter'
  });
  assertFalse(event.defaultPrevented);
  page.pageDiv.dispatchEvent(event);
  assertTrue(event.defaultPrevented);
  testDone();
});

/**
 * TestFixture for OptionsPage WebUI testing including tab history.
 * @extends {testing.Test}
 * @constructor
 */
function OptionsWebUINavigationTest() {}

OptionsWebUINavigationTest.prototype = {
  __proto__: testing.Test.prototype,

  /** @override */
  browsePreload: 'chrome://settings-frame',

  /** @override */
  typedefCppFixture: 'OptionsBrowserTest',

  /** @override */
  isAsync: true,

  /**
   * Asserts that two non-nested arrays are equal. The arrays must contain only
   * plain data types, no nested arrays or other objects.
   * @param {Array} expected An array of expected values.
   * @param {Array} result An array of actual values.
   * @param {boolean} doSort If true, the arrays will be sorted before being
   *     compared.
   * @param {string} description A brief description for the array of actual
   *     values, to use in an error message if the arrays differ.
   * @private
   */
  compareArrays_: function(expected, result, doSort, description) {
    var errorMessage = '\n' + description + ': ' + result +
                       '\nExpected: ' + expected;
    assertEquals(expected.length, result.length, errorMessage);

    var expectedSorted = expected.slice();
    var resultSorted = result.slice();
    if (doSort) {
      expectedSorted.sort();
      resultSorted.sort();
    }

    for (var i = 0; i < expectedSorted.length; ++i) {
      assertEquals(expectedSorted[i], resultSorted[i], errorMessage);
    }
  },

  /**
   * Verifies that the correct pages are currently open/visible.
   * @param {!Array.<string>} expectedPages An array of page names expected to
   *     be open, with the topmost listed last.
   * @param {string=} expectedUrl The URL path, including hash, expected to be
   *     open. If undefined, the topmost (last) page name in |expectedPages|
   *     will be used. In either case, 'chrome://settings-frame/' will be
   *     prepended.
   * @private
   */
  verifyOpenPages_: function(expectedPages, expectedUrl) {
    // Check the topmost page.
    assertEquals(null, OptionsPage.getVisibleBubble());
    var currentPage = OptionsPage.getTopmostVisiblePage();

    var lastExpected = expectedPages[expectedPages.length - 1];
    assertEquals(lastExpected, currentPage.name);
    // We'd like to check the title too, but we have to load the settings-frame
    // instead of the outer settings page in order to have access to
    // OptionsPage, and setting the title from within the settings-frame fails
    // because of cross-origin access restrictions.
    // TODO(pamg): Add a test fixture that loads chrome://settings and uses
    // UI elements to access sub-pages, so we can test the titles and
    // search-page URLs.
    var fullExpectedUrl = 'chrome://settings-frame/' +
                          (expectedUrl ? expectedUrl : lastExpected);
    assertEquals(fullExpectedUrl, window.location.href);

    // Collect open pages.
    var allPageNames = Object.keys(OptionsPage.registeredPages).concat(
                       Object.keys(OptionsPage.registeredOverlayPages));
    var openPages = [];
    for (var i = 0; i < allPageNames.length; ++i) {
      var name = allPageNames[i];
      var page = OptionsPage.registeredPages[name] ||
                 OptionsPage.registeredOverlayPages[name];
      if (page.visible)
        openPages.push(page.name);
    }

    this.compareArrays_(expectedPages, openPages, true, 'Open pages');
  },

  /*
   * Verifies that the correct URLs are listed in the history. Asynchronous.
   * @param {!Array.<string>} expectedHistory An array of URL paths expected to
   *     be in the tab navigation history, sorted by visit time, including the
   *     current page as the last entry. The base URL (chrome://settings-frame/)
   *     will be prepended to each. An initial 'about:blank' history entry is
   *     assumed and should not be included in this list.
   * @param {Function=} callback A function to be called after the history has
   *     been verified successfully. May be undefined.
   * @private
   */
  verifyHistory_: function(expectedHistory, callback) {
    var self = this;
    OptionsWebUINavigationTest.verifyHistoryCallback = function(results) {
      // The history always starts with a blank page.
      assertEquals('about:blank', results.shift());
      var fullExpectedHistory = [];
      for (var i = 0; i < expectedHistory.length; ++i) {
        fullExpectedHistory.push(
            'chrome://settings-frame/' + expectedHistory[i]);
      }
      self.compareArrays_(fullExpectedHistory, results, false, 'History');
      callback();
    };

    // The C++ fixture will call verifyHistoryCallback with the results.
    chrome.send('optionsTestReportHistory');
  },

  /**
   * Overrides the page callbacks for the given OptionsPage overlay to verify
   * that they are not called.
   * @param {Object} overlay The singleton instance of the overlay.
   * @private
   */
  prohibitChangesToOverlay_: function(overlay) {
    overlay.initializePage =
        overlay.didShowPage =
        overlay.didClosePage = function() {
          assertTrue(false,
                     'Overlay was affected when changes were prohibited.');
        };
  },
};

/*
 * Set by verifyHistory_ to incorporate a followup callback, then called by the
 * C++ fixture with the navigation history to be verified.
 * @type {Function}
 */
OptionsWebUINavigationTest.verifyHistoryCallback = null;

// Show the search page with no query string, to fall back to the settings page.
TEST_F('OptionsWebUINavigationTest', 'ShowSearchPageNoQuery', function() {
  OptionsPage.showPageByName('search');
  this.verifyOpenPages_(['settings']);
  this.verifyHistory_(['settings'], testDone);
});

// Show a page without updating history.
TEST_F('OptionsWebUINavigationTest', 'ShowPageNoHistory', function() {
  this.verifyOpenPages_(['settings']);
  // There are only two main pages, 'settings' and 'search'. It's not possible
  // to show the search page using OptionsPage.showPageByName, because it
  // reverts to the settings page if it has no search text set. So we show the
  // search page by performing a search, then test showPageByName.
  $('search-field').onsearch({currentTarget: {value: 'query'}});

  // The settings page is also still "open" (i.e., visible), in order to show
  // the search results. Furthermore, the URL hasn't been updated in the parent
  // page, because we've loaded the chrome-settings frame instead of the whole
  // settings page, so the cross-origin call to set the URL fails.
  this.verifyOpenPages_(['settings', 'search'], 'settings#query');
  var self = this;
  this.verifyHistory_(['settings', 'settings#query'], function() {
    OptionsPage.showPageByName('settings', false);
    self.verifyOpenPages_(['settings'], 'settings#query');
    self.verifyHistory_(['settings', 'settings#query'], testDone);
  });
});

TEST_F('OptionsWebUINavigationTest', 'ShowPageWithHistory', function() {
  // See comments for ShowPageNoHistory.
  $('search-field').onsearch({currentTarget: {value: 'query'}});
  var self = this;
  this.verifyHistory_(['settings', 'settings#query'], function() {
    OptionsPage.showPageByName('settings', true);
    self.verifyOpenPages_(['settings'], 'settings#query');
    self.verifyHistory_(['settings', 'settings#query', 'settings#query'],
                        testDone);
  });
});

TEST_F('OptionsWebUINavigationTest', 'ShowPageReplaceHistory', function() {
  // See comments for ShowPageNoHistory.
  $('search-field').onsearch({currentTarget: {value: 'query'}});
  var self = this;
  this.verifyHistory_(['settings', 'settings#query'], function() {
    OptionsPage.showPageByName('settings', true, {'replaceState': true});
    self.verifyOpenPages_(['settings'], 'settings#query');
    self.verifyHistory_(['settings', 'settings#query'], testDone);
  });
});

// This should be identical to ShowPageWithHisory.
TEST_F('OptionsWebUINavigationTest', 'NavigateToPage', function() {
  // See comments for ShowPageNoHistory.
  $('search-field').onsearch({currentTarget: {value: 'query'}});
  var self = this;
  this.verifyHistory_(['settings', 'settings#query'], function() {
    OptionsPage.navigateToPage('settings');
    self.verifyOpenPages_(['settings'], 'settings#query');
    self.verifyHistory_(['settings', 'settings#query', 'settings#query'],
                        testDone);
  });
});

// Settings overlays are much more straightforward than settings pages, opening
// normally with none of the latter's quirks in the expected history or URL.
TEST_F('OptionsWebUINavigationTest', 'ShowOverlayNoHistory', function() {
  // Open a layer-1 overlay, not updating history.
  OptionsPage.showPageByName('languages', false);
  this.verifyOpenPages_(['settings', 'languages'], 'settings');

  var self = this;
  this.verifyHistory_(['settings'], function() {
    // Open a layer-2 overlay for which the layer-1 is a parent, not updating
    // history.
    OptionsPage.showPageByName('addLanguage', false);
    self.verifyOpenPages_(['settings', 'languages', 'addLanguage'],
                          'settings');
    self.verifyHistory_(['settings'], testDone);
  });
});

TEST_F('OptionsWebUINavigationTest', 'ShowOverlayWithHistory', function() {
  // Open a layer-1 overlay, updating history.
  OptionsPage.showPageByName('languages', true);
  this.verifyOpenPages_(['settings', 'languages']);

  var self = this;
  this.verifyHistory_(['settings', 'languages'], function() {
    // Open a layer-2 overlay, updating history.
    OptionsPage.showPageByName('addLanguage', true);
    self.verifyOpenPages_(['settings', 'languages', 'addLanguage']);
    self.verifyHistory_(['settings', 'languages', 'addLanguage'], testDone);
  });
});

TEST_F('OptionsWebUINavigationTest', 'ShowOverlayReplaceHistory', function() {
  // Open a layer-1 overlay, updating history.
  OptionsPage.showPageByName('languages', true);
  var self = this;
  this.verifyHistory_(['settings', 'languages'], function() {
    // Open a layer-2 overlay, replacing history.
    OptionsPage.showPageByName('addLanguage', true, {'replaceState': true});
    self.verifyOpenPages_(['settings', 'languages', 'addLanguage']);
    self.verifyHistory_(['settings', 'addLanguage'], testDone);
  });
});

// Directly show an overlay further above this page, i.e. one for which the
// current page is an ancestor but not a parent.
TEST_F('OptionsWebUINavigationTest', 'ShowOverlayFurtherAbove', function() {
  // Open a layer-2 overlay directly.
  OptionsPage.showPageByName('addLanguage', true);
  this.verifyOpenPages_(['settings', 'languages', 'addLanguage']);
  var self = this;
  this.verifyHistory_(['settings', 'addLanguage'], testDone);
});

// Directly show a layer-2 overlay for which the layer-1 overlay is not a
// parent.
TEST_F('OptionsWebUINavigationTest', 'ShowUnrelatedOverlay', function() {
  // Open a layer-1 overlay.
  OptionsPage.showPageByName('languages', true);
  this.verifyOpenPages_(['settings', 'languages']);

  var self = this;
  this.verifyHistory_(['settings', 'languages'], function() {
    // Open an unrelated layer-2 overlay.
    OptionsPage.showPageByName('cookies', true);
    self.verifyOpenPages_(['settings', 'content', 'cookies']);
    self.verifyHistory_(['settings', 'languages', 'cookies'], testDone);
  });
});

// Close an overlay.
TEST_F('OptionsWebUINavigationTest', 'CloseOverlay', function() {
  // Open a layer-1 overlay, then a layer-2 overlay on top of it.
  OptionsPage.showPageByName('languages', true);
  this.verifyOpenPages_(['settings', 'languages']);
  OptionsPage.showPageByName('addLanguage', true);
  this.verifyOpenPages_(['settings', 'languages', 'addLanguage']);

  var self = this;
  this.verifyHistory_(['settings', 'languages', 'addLanguage'], function() {
    // Close the layer-2 overlay.
    OptionsPage.closeOverlay();
    self.verifyOpenPages_(['settings', 'languages']);
    self.verifyHistory_(
        ['settings', 'languages', 'addLanguage', 'languages'],
        function() {
      // Close the layer-1 overlay.
      OptionsPage.closeOverlay();
      self.verifyOpenPages_(['settings']);
      self.verifyHistory_(
          ['settings', 'languages', 'addLanguage', 'languages', 'settings'],
          testDone);
    });
  });
});

// Make sure an overlay isn't closed (even temporarily) when another overlay is
// opened on top.
TEST_F('OptionsWebUINavigationTest', 'OverlayAboveNoReset', function() {
  // Open a layer-1 overlay.
  OptionsPage.showPageByName('languages', true);
  this.verifyOpenPages_(['settings', 'languages']);

  // Open a layer-2 overlay on top. This should not close 'languages'.
  this.prohibitChangesToOverlay_(options.LanguageOptions.getInstance());
  OptionsPage.showPageByName('addLanguage', true);
  this.verifyOpenPages_(['settings', 'languages', 'addLanguage']);
  testDone();
});

TEST_F('OptionsWebUINavigationTest', 'OverlayTabNavigation', function() {
  // Open a layer-1 overlay, then a layer-2 overlay on top of it.
  OptionsPage.showPageByName('languages', true);
  OptionsPage.showPageByName('addLanguage', true);
  var self = this;

  // Go back twice, then forward twice.
  self.verifyOpenPages_(['settings', 'languages', 'addLanguage']);
  self.verifyHistory_(['settings', 'languages', 'addLanguage'], function() {
    window.history.back();
    waitForPopstate(function() {
      self.verifyOpenPages_(['settings', 'languages']);
      self.verifyHistory_(['settings', 'languages'], function() {
        window.history.back();
        waitForPopstate(function() {
          self.verifyOpenPages_(['settings']);
          self.verifyHistory_(['settings'], function() {
            window.history.forward();
            waitForPopstate(function() {
              self.verifyOpenPages_(['settings', 'languages']);
              self.verifyHistory_(['settings', 'languages'], function() {
                window.history.forward();
                waitForPopstate(function() {
                  self.verifyOpenPages_(
                      ['settings', 'languages', 'addLanguage']);
                  self.verifyHistory_(
                      ['settings', 'languages', 'addLanguage'], testDone);
                });
              });
            });
          });
        });
      });
    });
  });
});

// Going "back" to an overlay that's a child of the current overlay shouldn't
// close the current one.
TEST_F('OptionsWebUINavigationTest', 'OverlayBackToChild', function() {
  // Open a layer-1 overlay, then a layer-2 overlay on top of it.
  OptionsPage.showPageByName('languages', true);
  OptionsPage.showPageByName('addLanguage', true);
  var self = this;

  self.verifyOpenPages_(['settings', 'languages', 'addLanguage']);
  self.verifyHistory_(['settings', 'languages', 'addLanguage'], function() {
    // Close the top overlay, then go back to it.
    OptionsPage.closeOverlay();
    self.verifyOpenPages_(['settings', 'languages']);
    self.verifyHistory_(
        ['settings', 'languages', 'addLanguage', 'languages'],
        function() {
      // Going back to the 'addLanguage' page should not close 'languages'.
      self.prohibitChangesToOverlay_(options.LanguageOptions.getInstance());
      window.history.back();
      waitForPopstate(function() {
        self.verifyOpenPages_(['settings', 'languages', 'addLanguage']);
        self.verifyHistory_(['settings', 'languages', 'addLanguage'],
                            testDone);
      });
    });
  });
});

// Going back to an unrelated overlay should close the overlay and its parent.
TEST_F('OptionsWebUINavigationTest', 'OverlayBackToUnrelated', function() {
  // Open a layer-1 overlay, then an unrelated layer-2 overlay.
  OptionsPage.showPageByName('languages', true);
  OptionsPage.showPageByName('cookies', true);
  var self = this;
  self.verifyOpenPages_(['settings', 'content', 'cookies']);
  self.verifyHistory_(['settings', 'languages', 'cookies'], function() {
    window.history.back();
    waitForPopstate(function() {
      self.verifyOpenPages_(['settings', 'languages']);
      testDone();
    });
  });
});
