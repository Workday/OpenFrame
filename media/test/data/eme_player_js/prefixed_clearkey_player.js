// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ClearKey player responsible for playing media using Clear Key key system and
// prefixed EME API.
function PrefixedClearKeyPlayer(video, testConfig) {
  this.video = video;
  this.testConfig = testConfig;
}

PrefixedClearKeyPlayer.prototype.init = function() {
  // Returns a promise.
  return PlayerUtils.initEMEPlayer(this);
};

PrefixedClearKeyPlayer.prototype.registerEventListeners = function() {
  // Returns a promise.
  return PlayerUtils.registerPrefixedEMEEventListeners(this);
};

PrefixedClearKeyPlayer.prototype.onWebkitKeyMessage = function(message) {
  var initData = Utils.convertToUint8Array(message.message);
  var key = Utils.getDefaultKey(this.testConfig.forceInvalidResponse);
  Utils.timeLog('Adding key to sessionID: ' + message.sessionId);
  message.target.webkitAddKey(this.testConfig.keySystem, key, initData,
                              message.sessionId);
};
