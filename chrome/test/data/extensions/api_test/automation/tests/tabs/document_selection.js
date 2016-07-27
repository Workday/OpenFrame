// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var EventType = chrome.automation.EventType;
var RoleType = chrome.automation.RoleType;
var html = "<head><title>testdoc</title></head>" +
    '<p>para1</p><input type="text" id="textField" value="hello world">';

var allTests = [
  function testInitialSelectionNotSet() {
    assertEq(undefined, rootNode.anchorObject);
    assertEq(undefined, rootNode.anchorOffset);
    assertEq(undefined, rootNode.focusObject);
    assertEq(undefined, rootNode.focusOffset);
    chrome.test.succeed();
  },

  function selectOutsideTextField() {
    var textNode = rootNode.find({role: RoleType.paragraph}).firstChild;
    assertTrue(!!textNode);
    chrome.automation.setDocumentSelection({anchorObject: textNode,
                                            anchorOffset: 0,
                                            focusObject: textNode,
                                            focusOffset: 3});
    listenOnce(rootNode, EventType.documentSelectionChanged, function(evt) {
      assertEq(textNode, rootNode.anchorObject);
      assertEq(0, rootNode.anchorOffset);
      assertEq(textNode, rootNode.focusObject);
      assertEq(3, rootNode.focusOffset);
      chrome.test.succeed();
    });
  },

  function selectInTextField() {
    var textField = rootNode.find({role: RoleType.textField});
    assertTrue(!!textField);
    textField.focus();
    listenOnce(textField, EventType.textSelectionChanged, function(evt) {
      listenOnce(rootNode, EventType.documentSelectionChanged, function(evt) {
        assertTrue(evt.target === rootNode);
        assertEq(textField, rootNode.anchorObject);
        assertEq(0, rootNode.anchorOffset);
        assertEq(textField, rootNode.focusObject);
        assertEq(0, rootNode.focusOffset);
        chrome.automation.setDocumentSelection({anchorObject: textField,
                                                anchorOffset: 1,
                                                focusObject: textField,
                                                focusOffset: 3});
        listenOnce(rootNode, EventType.documentSelectionChanged,
                   function(evt) {
          assertEq(textField, rootNode.anchorObject);
          assertEq(1, rootNode.anchorOffset);
          assertEq(textField, rootNode.focusObject);
          assertEq(3, rootNode.focusOffset);
          chrome.test.succeed();
        });
      });
    });
  },
];

setUpAndRunTests(allTests, 'document_selection.html');
