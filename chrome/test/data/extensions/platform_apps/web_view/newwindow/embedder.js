// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var embedder = {};
embedder.test = {};
embedder.baseGuestURL = '';
embedder.guestURL = '';

window.runTest = function(testName) {
  if (!embedder.test.testList[testName]) {
    console.log('Incorrect testName: ' + testName);
    embedder.test.fail();
    return;
  }

  // Run the test.
  embedder.test.testList[testName]();
};
// window.* exported functions end.

/** @private */
embedder.setUp_ = function(config) {
  embedder.baseGuestURL = 'http://localhost:' + config.testServer.port;
  embedder.guestURL = embedder.baseGuestURL +
      '/extensions/platform_apps/web_view/newwindow' +
      '/guest_opener.html';
  chrome.test.log('Guest url is: ' + embedder.guestURL);
};

/** @private */
embedder.setUpGuest_ = function(partitionName) {
  document.querySelector('#webview-tag-container').innerHTML =
      '<webview style="width: 100px; height: 100px;"></webview>';
  var webview = document.querySelector('webview');
  if (partitionName) {
    webview.partition = partitionName;
  }
  if (!webview) {
    embedder.test.fail('No <webview> element created');
  }
  return webview;
};

/** @private */
embedder.setUpNewWindowRequest_ = function(webview, url, frameName, testName) {
  var onWebViewLoadStop = function(e) {
    // Send post message to <webview> when it's ready to receive them.
    var redirect = testName.indexOf("_blank") != -1;
    webview.contentWindow.postMessage(
        JSON.stringify(
            ['open-window', '' + url, '' + frameName, redirect]), '*');
  };
  webview.addEventListener('loadstop', onWebViewLoadStop);
  webview.setAttribute('src', embedder.guestURL);
};

embedder.test = {};
embedder.test.succeed = function() {
  chrome.test.sendMessage('DoneNewWindowTest.PASSED');
};

embedder.test.fail = function() {
  chrome.test.sendMessage('DoneNewWindowTest.FAILED');
};

embedder.test.assertEq = function(a, b) {
  if (a != b) {
    console.log('assertion failed: ' + a + ' != ' + b);
    embedder.test.fail();
  }
};

embedder.test.assertTrue = function(condition) {
  if (!condition) {
    console.log('assertion failed: true != ' + condition);
    embedder.test.fail();
  }
};

embedder.test.assertFalse = function(condition) {
  if (condition) {
    console.log('assertion failed: false != ' + condition);
    embedder.test.fail();
  }
};

/** @private */
embedder.requestFrameName_ =
    function(webview, openerWebview, testName, expectedFrameName) {
  var onWebViewLoadStop = function(e) {
    // Send post message to <webview> when it's ready to receive them.
    // Note that loadstop will get called twice if the test is opening
    // a new window via a redirect: one for navigating to about:blank
    // and another for navigating to the final destination.
    // about:blank will not respond to the postMessage so it's OK
    // to send it again on the second loadstop event.
    webview.contentWindow.postMessage(
        JSON.stringify(['get-frame-name', testName]), '*');
  };
  webview.addEventListener('loadstop', onWebViewLoadStop);
  var onPostMessageReceived = function(e) {
    var data = JSON.parse(e.data);
    if (data[0] == 'get-frame-name') {
      var name = data[1];
      if (testName != name)
        return;
      var frameName = data[2];
      embedder.test.assertEq(expectedFrameName, frameName);
      embedder.test.assertEq(expectedFrameName, webview.name);
      embedder.test.assertEq(openerWebview.partition, webview.partition);
      window.removeEventListener('message', onPostMessageReceived);
      embedder.test.succeed();
    }
  };
  window.addEventListener('message', onPostMessageReceived);
};

/** @private */
embedder.requestClose_ = function(webview, testName) {
  var onWebViewLoadStop = function(e) {
    webview.contentWindow.postMessage(
        JSON.stringify(['close', testName]), '*');
  };
  webview.addEventListener('loadstop', onWebViewLoadStop);
  var onWebViewClose = function(e) {
    webview.removeEventListener('close', onWebViewClose);
    embedder.test.succeed();
  };
  webview.addEventListener('close', onWebViewClose);
};

/** @private */
embedder.requestExecuteScript_ =
    function(webview, script, expectedResult, testName) {
  var onWebViewLoadStop = function(e) {
    webview.executeScript(
      {code: script},
      function(results) {
        embedder.test.assertEq(1, results.length);
        embedder.test.assertEq(expectedResult, results[0]);
        embedder.test.succeed();
      });
  };
  webview.addEventListener('loadstop', onWebViewLoadStop);
};

/** @private */
embedder.assertCorrectEvent_ = function(e, guestName) {
  embedder.test.assertEq('newwindow', e.type);
  embedder.test.assertTrue(!!e.targetUrl);
  embedder.test.assertEq(guestName, e.name);
};

// Tests begin.

var testNewWindowName = function(testName,
                                 webViewName,
                                 guestName,
                                 partitionName,
                                 expectedFrameName) {
  var webview = embedder.setUpGuest_(partitionName);

  var onNewWindow = function(e) {
    chrome.test.log('Embedder notified on newwindow');
    embedder.assertCorrectEvent_(e, guestName);

    var newwebview = document.createElement('webview');
    newwebview.setAttribute('name', webViewName);
    document.querySelector('#webview-tag-container').appendChild(newwebview);
    embedder.requestFrameName_(
        newwebview, webview, testName, expectedFrameName);
    try {
      e.window.attach(newwebview);
    } catch (e) {
      embedder.test.fail();
    }
  };
  webview.addEventListener('newwindow', onNewWindow);

  // Load a new window with the given name.
  embedder.setUpNewWindowRequest_(webview, 'guest.html', guestName, testName);
};

// Loads a guest which requests a new window.
function testNewWindowNameTakesPrecedence() {
  var webViewName = 'foo';
  var guestName = 'bar';
  var partitionName = 'foobar';
  var expectedName = guestName;
  testNewWindowName('testNewWindowNameTakesPrecedence',
                    webViewName, guestName, partitionName, expectedName);
};

function testWebViewNameTakesPrecedence() {
  var webViewName = 'foo';
  var guestName = '';
  var partitionName = 'persist:foobar';
  var expectedName = webViewName;
  testNewWindowName('testWebViewNameTakesPrecedence',
                    webViewName, guestName, partitionName, expectedName);
};

function testNoName() {
  var webViewName = '';
  var guestName = '';
  var partitionName = '';
  var expectedName = '';
  testNewWindowName('testNoName',
                    webViewName, guestName, partitionName, expectedName);
};

// This test exercises the need for queuing events that occur prior to
// attachment. In this test a new window is opened that initially navigates to
// about:blank and then subsequently redirects to its final destination. This
// test responds to loadstop in the new <webview>. Since "about:blank" does not
// have any external resources, it loads immediately prior to attachment, and
// the <webview> that is eventually attached never gets a chance to see the
// event. GuestView solves this problem by queuing events that occur prior to
// attachment and firing them immediately after attachment.
function testNewWindowRedirect() {
  var webViewName = 'foo';
  var guestName = '';
  var partitionName = 'persist:foobar';
  var expectedName = webViewName;
  testNewWindowName('testNewWindowRedirect_blank',
                    webViewName, guestName, partitionName, expectedName);
};

function testNewWindowClose() {
  var testName = 'testNewWindowClose';
  var webview = embedder.setUpGuest_('foobar');

  var onNewWindow = function(e) {
    chrome.test.log('Embedder notified on newwindow');
    embedder.assertCorrectEvent_(e, '');

    var newwebview = document.createElement('webview');
    document.querySelector('#webview-tag-container').appendChild(newwebview);
    embedder.requestClose_(newwebview, testName);
    try {
      e.window.attach(newwebview);
    } catch (e) {
      embedder.test.fail();
    }
  };
  webview.addEventListener('newwindow', onNewWindow);

  // Load a new window with the given name.
  embedder.setUpNewWindowRequest_(webview, 'guest.html', '', testName);
};


function testNewWindowExecuteScript() {
  var testName = 'testNewWindowExecuteScript';
  var webview = embedder.setUpGuest_('foobar');

  var onNewWindow = function(e) {
    chrome.test.log('Embedder notified on newwindow');
    embedder.assertCorrectEvent_(e, '');

    var newwebview = document.createElement('webview');
    document.querySelector('#webview-tag-container').appendChild(newwebview);
    embedder.requestExecuteScript_(
        newwebview,
        'document.body.style.backgroundColor = "red";',
        'red',
        testName);
    try {
      e.window.attach(newwebview);
    } catch (e) {
      embedder.test.fail();
    }
  };
  webview.addEventListener('newwindow', onNewWindow);

  // Load a new window with the given name.
  embedder.setUpNewWindowRequest_(webview, 'about:blank', '', testName);
};

function testNewWindowWebRequest() {
  var testName = 'testNewWindowExecuteScript';
  var webview = embedder.setUpGuest_('foobar');

  var onNewWindow = function(e) {
    chrome.test.log('Embedder notified on newwindow');
    embedder.assertCorrectEvent_(e, '');

    var newwebview = document.createElement('webview');
    document.querySelector('#webview-tag-container').appendChild(newwebview);
    var calledWebRequestEvent = false;
    e.preventDefault();
    // The WebRequest API is not exposed until the <webview> MutationObserver
    // picks up the <webview> in the DOM. Thus, we cannot immediately access
    // WebRequest API.
    window.setTimeout(function() {
      newwebview.onBeforeRequest.addListener(function(e) {
          if (!calledWebRequestEvent) {
            calledWebRequestEvent = true;
            embedder.test.succeed();
          }
        }, { urls: ['<all_urls>'] }, ['blocking']);
      try {
        e.window.attach(newwebview);
      } catch (e) {
        embedder.test.fail();
      }
    }, 0);
  };
  webview.addEventListener('newwindow', onNewWindow);

  // Load a new window with the given name.
  embedder.setUpNewWindowRequest_(webview, 'guest.html', '', testName);
};

embedder.test.testList = {
  'testNewWindowNameTakesPrecedence': testNewWindowNameTakesPrecedence,
  'testWebViewNameTakesPrecedence': testWebViewNameTakesPrecedence,
  'testNoName': testNoName,
  'testNewWindowRedirect':  testNewWindowRedirect,
  'testNewWindowClose': testNewWindowClose,
  'testNewWindowExecuteScript': testNewWindowExecuteScript,
  'testNewWindowWebRequest': testNewWindowWebRequest
};

onload = function() {
  chrome.test.getConfig(function(config) {
    embedder.setUp_(config);
    chrome.test.sendMessage('Launched');
  });
};
