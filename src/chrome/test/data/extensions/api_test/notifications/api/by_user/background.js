// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const notifications = chrome.notifications;
var theOnlyTestDone = null;

var notificationData = {
  type: "basic",
  iconUrl: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAUAAAAFCAYAAA" +
           "CNbyblAAAAHElEQVQI12P4//8/w38GIAXDIBKE0DHxgljNBAAO9TXL0Y4OHw" +
           "AAAABJRU5ErkJggg==",
  title: "Attention!",
  message: "Check out Cirque du Soleil"
};

var results = {
  FOO: false,
  BAR: true,
  BAT: false,
  BIFF: false,
  BLAT: true,
  BLOT: true
};

function createCallback(id) { }

var onClosedHooks = {
  BIFF: function() {
    notifications.create("BLAT", notificationData, createCallback);
    notifications.create("BLOT", notificationData, createCallback);
  },
};

function onClosedListener(id, by_user) {
  if (results[id] !== by_user) {
    chrome.test.notifyFail("Notification " + id +
                           " closed with bad by_user param ( "+ by_user +" )");
    return;
  }
  chrome.test.notifyPass();
  delete results[id];

  if (typeof onClosedHooks[id] === "function")
    onClosedHooks[id]();

  if (Object.keys(results).length === 0)
    theOnlyTestDone();
}

notifications.onClosed.addListener(onClosedListener);

function theOnlyTest() {
  theOnlyTestDone = chrome.test.callbackAdded();

  notifications.create("FOO", notificationData, createCallback);
  notifications.create("BAR", notificationData, createCallback);
  notifications.create("BAT", notificationData, createCallback);
  notifications.create("BIFF", notificationData, createCallback);
}

chrome.test.runTests([ theOnlyTest ]);
