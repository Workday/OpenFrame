// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Preferences API test
// Run with browser_tests --gtest_filter=ExtensionPreferenceApiTest.Standard

var pn = chrome.privacy.network;
var preferences_to_test = [
  {
    root: chrome.privacy.network,
    preferences: [
      'networkPredictionEnabled',
      'webRTCMultipleRoutesEnabled',
      'webRTCNonProxiedUdpEnabled'
    ]
  },
  {
    root: chrome.privacy.websites,
    preferences: [
      'thirdPartyCookiesAllowed',
      'hyperlinkAuditingEnabled',
      'referrersEnabled',
      'protectedContentEnabled'
    ]
  },
  {
    root: chrome.privacy.services,
    preferences: [
      'alternateErrorPagesEnabled',
      'autofillEnabled',
      'hotwordSearchEnabled',
      'passwordSavingEnabled',
      'safeBrowsingEnabled',
      'safeBrowsingExtendedReportingEnabled',
      'searchSuggestEnabled',
      'spellingServiceEnabled',
      'translationServiceEnabled'
    ]
  },
];

// Some preferences are only present on certain platforms or are hidden
// behind flags and might not be present when this test runs.
var possibly_missing_preferences = new Set([
  'protectedContentEnabled',             // Windows/ChromeOS only
  'webRTCMultipleRoutesEnabled',         // requires ENABLE_WEBRTC=1
  'webRTCNonProxiedUdpEnabled'           // requires ENABLE_WEBRTC=1
]);

function expect(expected, message) {
  return chrome.test.callbackPass(function(value) {
    chrome.test.assertEq(expected, value, message);
  });
}

function expectFalse(pref) {
  return expect({
    value: false,
    levelOfControl: 'controllable_by_this_extension'
  }, '`' + pref + '` is expected to be false.');
}

function prefGetter(pref) {
  if (possibly_missing_preferences.has(pref) && !this[pref]) {
    return true;
  }
  this[pref].get({}, expectFalse(pref));
}

function prefSetter(pref) {
  if (possibly_missing_preferences.has(pref) && !this[pref]) {
    return true;
  }
  this[pref].set({value: true}, chrome.test.callbackPass());
}

chrome.test.runTests([
  function getPreferences() {
    for (var i = 0; i < preferences_to_test.length; i++) {
      preferences_to_test[i].preferences.forEach(
          prefGetter.bind(preferences_to_test[i].root));
    }
  },
  function setGlobals() {
    for (var i = 0; i < preferences_to_test.length; i++) {
      preferences_to_test[i].preferences.forEach(
          prefSetter.bind(preferences_to_test[i].root));
    }
  },
  // Set the WebRTCIPHhandlingPolicy and verify it in the get function.
  function testWebRTCIPHandlingPolicy() {
    if (pn.webRTCIPHandlingPolicy == undefined) {
      chrome.test.callbackPass();
      return;
    }
    pn.webRTCIPHandlingPolicy.get(
      {},
      expect(
        {value: chrome.privacy.IPHandlingPolicy.DEFAULT_PUBLIC_INTERFACE_ONLY,
         levelOfControl: 'controllable_by_this_extension'},
        'should receive default_public_interface_only.'));

    pn.webRTCIPHandlingPolicy.set(
      {value: chrome.privacy.IPHandlingPolicy.DISABLE_NON_PROXIED_UDP});

    pn.webRTCIPHandlingPolicy.get(
      {},
      expect(
        {value: chrome.privacy.IPHandlingPolicy.DISABLE_NON_PROXIED_UDP,
         levelOfControl: 'controlled_by_this_extension'},
        'should receive disable_non_proxied_udp.'));
  }
]);
