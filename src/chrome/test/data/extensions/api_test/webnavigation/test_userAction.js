// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

onload = function() {
  var getURL = chrome.extension.getURL;
  chrome.tabs.create({"url": "about:blank"}, function(tab) {
    var tabId = tab.id;

    chrome.test.runTests([
      // Opens a tab and waits for the user to click on a link in it.
      function userAction() {
        expect([
          { label: "a-onBeforeNavigate",
            event: "onBeforeNavigate",
            details: { frameId: 0,
                       parentFrameId: -1,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('userAction/a.html') }},
          { label: "a-onCommitted",
            event: "onCommitted",
            details: { frameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       transitionQualifiers: [],
                       transitionType: "typed",
                       url: getURL('userAction/a.html') }},
          { label: "a-onDOMContentLoaded",
            event: "onDOMContentLoaded",
            details: { frameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('userAction/a.html') }},
          { label: "a-onCompleted",
            event: "onCompleted",
            details: { frameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('userAction/a.html') }},
          { label: "b-onCreatedNavigationTarget",
            event: "onCreatedNavigationTarget",
            details: { sourceFrameId: 0,
                       sourceProcessId: 0,
                       sourceTabId: 0,
                       tabId: 1,
                       timeStamp: 0,
                       url: getURL('userAction/b.html') }},
          { label: "b-onBeforeNavigate",
            event: "onBeforeNavigate",
            details: { frameId: 0,
                       parentFrameId: -1,
                       processId: 0,
                       tabId: 1,
                       timeStamp: 0,
                       url: getURL('userAction/b.html') }},
          { label: "b-onCommitted",
            event: "onCommitted",
            details: { frameId: 0,
                       processId: 0,
                       tabId: 1,
                       timeStamp: 0,
                       transitionQualifiers: [],
                       transitionType: "link",
                       url: getURL('userAction/b.html') }},
          { label: "b-onDOMContentLoaded",
            event: "onDOMContentLoaded",
            details: { frameId: 0,
                       processId: 0,
                       tabId: 1,
                       timeStamp: 0,
                       url: getURL('userAction/b.html') }},
          { label: "b-onCompleted",
            event: "onCompleted",
            details: { frameId: 0,
                       processId: 0,
                       tabId: 1,
                       timeStamp: 0,
                       url: getURL('userAction/b.html') }}],
          [ navigationOrder("a-"),
            navigationOrder("b-"),
            [ "a-onDOMContentLoaded",
              "b-onCreatedNavigationTarget",
              "b-onBeforeNavigate" ]]);

        // Notify the api test that we're waiting for the user.
        chrome.test.notifyPass();
      },
    ]);
  });
};
