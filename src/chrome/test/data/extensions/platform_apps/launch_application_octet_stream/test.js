// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

chrome.app.runtime.onLaunched.addListener(function (launchData) {
  // Test that the id and items fields in FileEntry can be read.
  chrome.test.runTests([
    function testFileHandler() {
      chrome.test.assertFalse(!launchData, "No launchData");
      chrome.test.assertEq(launchData.id, "unknown",
          "launchData.id incorrect");
      chrome.test.assertEq(launchData.items.length, 1);
      chrome.test.assertEq(launchData.items[0].type,
          "application/octet-stream");
      chrome.test.assertTrue(
          chrome.fileSystem.retainEntry(launchData.items[0].entry) != null);

      launchData.items[0].entry.file(function(file) {
        var reader = new FileReader();
        reader.onloadend = function(e) {
          chrome.test.assertEq(
              reader.result.indexOf("This is a test. Word."), 0);
          chrome.test.succeed();
        };
        reader.onerror = function(e) {
          chrome.test.fail("Error reading file contents.");
        };
        reader.readAsText(file);
      });
    }
  ]);
});
