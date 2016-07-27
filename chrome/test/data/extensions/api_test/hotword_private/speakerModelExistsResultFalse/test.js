// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

chrome.test.runTests([
  function speakerModelExistsResultFalse() {
    chrome.hotwordPrivate.speakerModelExistsResult(
        false, chrome.test.callbackPass(function() {
          chrome.test.sendMessage("ready");
        }));
  }
]);
