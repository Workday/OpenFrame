// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests that the 'body' element has had CSS injected, and that the CSS code
// has had the __MSG_@@extension_id__ message replaced ('extension_id' must
// not be present in any CSS code).

var rules = getMatchedCSSRules(document.getElementById('bodyId'));
if (rules != null) {
  for (var i = 0; i < rules.length; ++i) {
    if (rules.item(i).cssText.indexOf('extension_id') != -1) {
      chrome.extension.sendRequest({tag: 'extension_id', message:
          'Found unreplaced extension_id in: ' + rules.item(i).cssText});
      break;
    }
  }
  chrome.extension.sendRequest({tag: 'extension_id', message: 'passed'});
} else {
  chrome.extension.sendRequest({tag: 'extension_id', message:
      'No CSS rules found.'});
}

