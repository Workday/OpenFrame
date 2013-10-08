// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// net/tools/testserver/testserver.py is picky about the format of what it
// calls its "echo" messages. One might go so far as to mutter to oneself that
// it isn't an echo server at all.
//
// The response is based on the request but obfuscated using a random key.
const request = "0100000005320000005hello";
var expectedResponsePattern = /0100000005320000005.{11}/;

const socket = chrome.socket;
var address;
var bytesWritten = 0;
var dataAsString;
var dataRead = [];
var port = -1;
var protocol = "none";
var socketId = 0;
var succeeded = false;
var waitCount = 0;

// Many thanks to Dennis for his StackOverflow answer: http://goo.gl/UDanx
// Since amended to handle BlobBuilder deprecation.
function string2ArrayBuffer(string, callback) {
  var blob = new Blob([string]);
  var f = new FileReader();
  f.onload = function(e) {
    callback(e.target.result);
  };
  f.readAsArrayBuffer(blob);
}

function arrayBuffer2String(buf, callback) {
  var blob = new Blob([new Uint8Array(buf)]);
  var f = new FileReader();
  f.onload = function(e) {
    callback(e.target.result);
  };
  f.readAsText(blob);
}

var testSocketCreation = function() {
  function onCreate(socketInfo) {
    function onGetInfo(info) {
      chrome.test.assertEq(info.socketType, protocol);
      chrome.test.assertFalse(info.connected);

      if (info.peerAddress || info.peerPort) {
        chrome.test.fail('Unconnected socket should not have peer');
      }
      if (info.localAddress || info.localPort) {
        chrome.test.fail('Unconnected socket should not have local binding');
      }

      socket.destroy(socketInfo.socketId);
      socket.getInfo(socketInfo.socketId, function(info) {
        chrome.test.assertEq(undefined, info);
        chrome.test.succeed();
      });
    }

    chrome.test.assertTrue(socketInfo.socketId > 0);

    // Obtaining socket information before a connect() call should be safe, but
    // return empty values.
    socket.getInfo(socketInfo.socketId, onGetInfo);
  }

  socket.create(protocol, {}, onCreate);
};


var testGetInfo = function() {
};

function onDataRead(readInfo) {
  if (readInfo.resultCode > 0 || readInfo.data.byteLength > 0) {
    chrome.test.assertEq(readInfo.resultCode, readInfo.data.byteLength);
  }

  arrayBuffer2String(readInfo.data, function(s) {
      dataAsString = s;  // save this for error reporting
      var match = !!s.match(expectedResponsePattern);
      chrome.test.assertTrue(match, "Received data does not match.");
      succeeded = true;
      chrome.test.succeed();
  });
}

function onWriteOrSendToComplete(writeInfo) {
  bytesWritten += writeInfo.bytesWritten;
  if (bytesWritten == request.length) {
    if (protocol == "tcp")
      socket.read(socketId, onDataRead);
    else
      socket.recvFrom(socketId, onDataRead);
  }
}

function onSetKeepAlive(result) {
  if (protocol == "tcp")
    chrome.test.assertTrue(result, "setKeepAlive failed for TCP.");
  else
    chrome.test.assertFalse(result, "setKeepAlive did not fail for UDP.");

  string2ArrayBuffer(request, function(arrayBuffer) {
      if (protocol == "tcp")
        socket.write(socketId, arrayBuffer, onWriteOrSendToComplete);
      else
        socket.sendTo(socketId, arrayBuffer, address, port,
                      onWriteOrSendToComplete);
    });
}

function onSetNoDelay(result) {
  if (protocol == "tcp")
    chrome.test.assertTrue(result, "setNoDelay failed for TCP.");
  else
    chrome.test.assertFalse(result, "setNoDelay did not fail for UDP.");
  socket.setKeepAlive(socketId, true, 1000, onSetKeepAlive);
}

function onGetInfo(result) {
  chrome.test.assertTrue(!!result.localAddress,
                         "Bound socket should always have local address");
  chrome.test.assertTrue(!!result.localPort,
                         "Bound socket should always have local port");
  chrome.test.assertEq(result.socketType, protocol, "Unexpected socketType");

  if (protocol == "tcp") {
    // NOTE: We're always called with 'localhost', but getInfo will only return
    // IPs, not names.
    chrome.test.assertEq(result.peerAddress, "127.0.0.1",
                         "Peer addresss should be the listen server");
    chrome.test.assertEq(result.peerPort, port,
                         "Peer port should be the listen server");
    chrome.test.assertTrue(result.connected, "Socket should be connected");
  } else {
    chrome.test.assertFalse(result.connected, "UDP socket was not connected");
    chrome.test.assertTrue(!result.peerAddress,
        "Unconnected UDP socket should not have peer address");
    chrome.test.assertTrue(!result.peerPort,
        "Unconnected UDP socket should not have peer port");
  }

  socket.setNoDelay(socketId, true, onSetNoDelay);
}

function onConnectOrBindComplete(result) {
  chrome.test.assertEq(0, result,
                       "Connect or bind failed with error " + result);
  if (result == 0) {
    socket.getInfo(socketId, onGetInfo);
  }
}

function onCreate(socketInfo) {
  socketId = socketInfo.socketId;
  chrome.test.assertTrue(socketId > 0, "failed to create socket");
  if (protocol == "tcp")
    socket.connect(socketId, address, port, onConnectOrBindComplete);
  else
    socket.bind(socketId, "0.0.0.0", 0, onConnectOrBindComplete);
}

function waitForBlockingOperation() {
  if (++waitCount < 10) {
    setTimeout(waitForBlockingOperation, 1000);
  } else {
    // We weren't able to succeed in the given time.
    chrome.test.fail("Operations didn't complete after " + waitCount + " " +
                     "seconds. Response so far was <" + dataAsString + ">.");
  }
}

var testSending = function() {
  dataRead = "";
  succeeded = false;
  waitCount = 0;

  setTimeout(waitForBlockingOperation, 1000);
  socket.create(protocol, {}, onCreate);
};

// Tests listening on a socket and sending/receiving from accepted sockets.
var testSocketListening = function() {
  var tmpSocketId = 0;

  function onServerSocketAccept(acceptInfo) {
    chrome.test.assertEq(0, acceptInfo.resultCode);
    var acceptedSocketId = acceptInfo.socketId;
    socket.read(acceptedSocketId, function(readInfo) {
      arrayBuffer2String(readInfo.data, function(s) {
        var match = !!s.match(request);
        chrome.test.assertTrue(match, "Received data does not match.");
        succeeded = true;
        // Test whether socket.getInfo correctly reflects the connection status
        // if the peer has closed the connection.
        setTimeout(function() {
          socket.getInfo(acceptedSocketId, function(info) {
            chrome.test.assertFalse(info.connected);
            chrome.test.succeed();
          });
        }, 500);
      });
    });
  }

  function onListen(result) {
    chrome.test.assertEq(0, result, "Listen failed.");
    socket.accept(socketId, onServerSocketAccept);

    // Trying to schedule a second accept callback should fail.
    socket.accept(socketId, function(acceptInfo) {
      chrome.test.assertEq(-2, acceptInfo.resultCode);
    });

    // Create a new socket to connect to the TCP server.
    socket.create('tcp', {}, function(socketInfo) {
      tmpSocketId = socketInfo.socketId;
      socket.connect(tmpSocketId, address, port,
        function(result) {
          chrome.test.assertEq(0, result, "Connect failed");

          // Write.
          string2ArrayBuffer(request, function(buf) {
            socket.write(tmpSocketId, buf, function() {
              socket.disconnect(tmpSocketId);
            });
          });
        });
    });
  }

  function onServerSocketCreate(socketInfo) {
    socketId = socketInfo.socketId;
    socket.listen(socketId, address, port, onListen);
  }

  socket.create('tcp', {}, onServerSocketCreate);
};

var onMessageReply = function(message) {
  var parts = message.split(":");
  var test_type = parts[0];
  address = parts[1];
  port = parseInt(parts[2]);
  console.log("Running tests, protocol " + protocol + ", echo server " +
              address + ":" + port);
  if (test_type == 'tcp_server') {
    chrome.test.runTests([ testSocketListening ]);
  } else if (test_type == 'multicast') {
    console.log("Running multicast tests");
    chrome.test.runTests([ testMulticast ]);
  } else {
    protocol = test_type;
    chrome.test.runTests([ testSocketCreation, testSending ]);
  }
};

// Find out which protocol we're supposed to test, and which echo server we
// should be using, then kick off the tests.
chrome.test.sendMessage("info_please", onMessageReply);
