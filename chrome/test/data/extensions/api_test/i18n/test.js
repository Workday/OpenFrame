// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// i18n api test
// browser_tests.exe --gtest_filter=ExtensionApiTest.I18N --lib=browser_tests

var testCallback = chrome.test.testCallback;
var callbackPass = chrome.test.callbackPass;

chrome.test.getConfig(function(config) {

  var TEST_FILE_URL = "http://localhost:PORT/extensions/test_file.html"
      .replace(/PORT/, config.testServer.port);

  chrome.test.runTests([
    function getAcceptLanguages() {
      chrome.i18n.getAcceptLanguages(callbackPass(function(results) {
        chrome.test.assertEq(results.length, 2);
        chrome.test.assertEq(results[0], "en-US");
        chrome.test.assertEq(results[1], "en");
      }));
    },
    function getMessage() {
      var message = chrome.i18n.getMessage("simple_message");
      chrome.test.assertEq(message, "Simple message");

      message = chrome.i18n.getMessage("message_with_placeholders",
                                       ["Cira", "John"]);
      chrome.test.assertEq(message, "Cira and John work for Google");

      message = chrome.i18n.getMessage("message_with_one_placeholder", "19");
      chrome.test.assertEq(message, "Number of errors: 19");

      message = chrome.i18n.getMessage("message_with_double_dollar_sign");
      chrome.test.assertEq(message, "I need $500 please.");

      message = chrome.i18n.getMessage(
          "message_with_double_dollar_sign_and_placeholders",
          ["Mitchell", "Chris"]);
      chrome.test.assertEq(message,
          "We should really be paying Mitchell and Chris more $$$.");

      chrome.test.succeed();
    },
    function getMessageFromContentScript() {
      chrome.extension.onRequest.addListener(
        function(request, sender, sendResponse) {
          chrome.test.assertEq(request, "Number of errors: 19");
        }
      );
      chrome.test.log("Creating tab...");
      chrome.tabs.create({
        url: TEST_FILE_URL
      });
      chrome.test.succeed();
    },
    function getUILanguage() {
      chrome.test.assertEq('en-US', chrome.i18n.getUILanguage());
      chrome.test.succeed();
    },
    function detectLanguage() {
      var text = "undef";
      chrome.i18n.detectLanguage(text, function (result) {
        chrome.test.assertEq([], result.languages);
      });

      text = "This text is obviously in English";
      chrome.i18n.detectLanguage(text, function (result) {
        chrome.test.assertEq([{ "language": "en", "percentage": 97 }],
            result.languages);
      });

      text = "Данный текст явно не на английском языке. \
      But this one definitely is. Welcome to Google!";
      chrome.i18n.detectLanguage(text, function (result) {
        chrome.test.assertEq([{ "language": "ru", "percentage": 61 },
            { "language": "en", "percentage": 37 }], result.languages);
      });

      chrome.test.succeed();
    }
  ]);
});
