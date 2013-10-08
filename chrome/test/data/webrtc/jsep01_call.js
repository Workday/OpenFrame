/**
 * Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/** @private */
var gTransformOutgoingSdp = function(sdp) { return sdp; };

/** @private */
var gCreateAnswerConstraints = {};

/** @private */
var gCreateOfferConstraints = {};

/** @private */
var gDataChannel = null;

/** @private */
var gDataStatusCallback = function(status) {};

/** @private */
var gDataCallback = function(data) {};

/** @private */
var gDtmfSender = null;

/** @private */
var gDtmfOnToneChange = function(tone) {};

/**
 * Sets the transform to apply just before setting the local description and
 * sending to the peer.
 * @param {function} transformFunction A function which takes one SDP string as
 *     argument and returns the modified SDP string.
 */
function setOutgoingSdpTransform(transformFunction) {
  gTransformOutgoingSdp = transformFunction;
}

/**
 * Sets the MediaConstraints to be used for PeerConnection createAnswer() calls.
 * @param {string} mediaConstraints The constraints, as defined in the
 *     PeerConnection JS API spec.
 */
function setCreateAnswerConstraints(mediaConstraints) {
  gCreateAnswerConstraints = mediaConstraints;
}

/**
 * Sets the MediaConstraints to be used for PeerConnection createOffer() calls.
 * @param {string} mediaConstraints The constraints, as defined in the
 *     PeerConnection JS API spec.
 */
function setCreateOfferConstraints(mediaConstraints) {
  gCreateOfferConstraints = mediaConstraints;
}

/**
 * Sets the callback functions that will receive DataChannel readyState updates
 * and received data.
 * @param {function} status_callback The function that will receive a string
 * with
 *     the current DataChannel readyState.
 * @param {function} data_callback The function that will a string with data
 *     received from the remote peer.
 */
function setDataCallbacks(status_callback, data_callback) {
  gDataStatusCallback = status_callback;
  gDataCallback = data_callback;
}

/**
 * Sends data on an active DataChannel.
 * @param {string} data The string that will be sent to the remote peer.
 */
function sendDataOnChannel(data) {
  if (gDataChannel == null)
    throw failTest('Trying to send data, but there is no DataChannel.');
  gDataChannel.send(data);
}

/**
 * Sets the callback function that will receive DTMF sender ontonechange events.
 * @param {function} ontonechange The function that will receive a string with
 *     the tone that has just begun playout.
 */
function setOnToneChange(ontonechange) {
  gDtmfOnToneChange = ontonechange;
}

/**
 * Inserts DTMF tones on an active DTMF sender.
 * @param {string} data The string that will be sent to the remote peer.
 */
function insertDtmf(tones, duration, interToneGap) {
  if (gDtmfSender == null)
    throw failTest('Trying to send DTMF, but there is no DTMF sender.');
  gDtmfSender.insertDTMF(tones, duration, interToneGap);
}

// Public interface towards the other javascript files, such as
// message_handling.js. The contract for these functions is described in
// message_handling.js.

function handleMessage(peerConnection, message) {
  var parsed_msg = JSON.parse(message);
  if (parsed_msg.type) {
    var session_description = new RTCSessionDescription(parsed_msg);
    peerConnection.setRemoteDescription(
        session_description,
        function() { success_('setRemoteDescription'); },
        function() { failure_('setRemoteDescription'); });
    if (session_description.type == 'offer') {
      debug('createAnswer with constraints: ' +
            JSON.stringify(gCreateAnswerConstraints, null, ' '));
      peerConnection.createAnswer(
        setLocalAndSendMessage_,
        function() { failure_('createAnswer'); },
        gCreateAnswerConstraints);
    }
    return;
  } else if (parsed_msg.candidate) {
    var candidate = new RTCIceCandidate(parsed_msg);
    peerConnection.addIceCandidate(candidate);
    return;
  }
  addTestFailure('unknown message received');
  return;
}

function createPeerConnection(stun_server, loggingSessionId) {
  servers = {iceServers: [{url: 'stun:' + stun_server}]};
  try {
    if (loggingSessionId) {
      var constraints =
          { optional: [{ RtpDataChannels: true },
                       { googLog: loggingSessionId }]};
    } else {
      var constraints = { optional: [{ RtpDataChannels: true }]};
    }
    peerConnection = new webkitRTCPeerConnection(servers, constraints);
  } catch (exception) {
    throw failTest('Failed to create peer connection: ' + exception);
  }
  peerConnection.onaddstream = addStreamCallback_;
  peerConnection.onremovestream = removeStreamCallback_;
  peerConnection.onicecandidate = iceCallback_;
  peerConnection.ondatachannel = onCreateDataChannelCallback_;
  return peerConnection;
}

function setupCall(peerConnection) {
  debug('createOffer with constraints: ' +
        JSON.stringify(gCreateOfferConstraints, null, ' '));
  peerConnection.createOffer(
      setLocalAndSendMessage_,
      function() { failure_('createOffer'); },
      gCreateOfferConstraints);
}

function answerCall(peerConnection, message) {
  handleMessage(peerConnection, message);
}

function createDataChannel(peerConnection, label) {
  if (gDataChannel != null && gDataChannel.readyState != 'closed') {
    throw failTest('Creating DataChannel, but we already have one.');
  }

  gDataChannel = peerConnection.createDataChannel(label, { reliable: false });
  debug('DataChannel with label ' + gDataChannel.label + ' initiated locally.');
  hookupDataChannelEvents();
}

function closeDataChannel(peerConnection) {
  if (gDataChannel == null)
    throw failTest('Closing DataChannel, but none exists.');
  debug('DataChannel with label ' + gDataChannel.label + ' is beeing closed.');
  gDataChannel.close();
}

function createDtmfSender(peerConnection) {
  if (gDtmfSender != null)
    throw failTest('Creating DTMF sender, but we already have one.');

  var localStream = getLocalStream();
  if (localStream == null)
    throw failTest('Creating DTMF sender but local stream is null.');
  local_audio_track = localStream.getAudioTracks()[0];
  gDtmfSender = peerConnection.createDTMFSender(local_audio_track);
  gDtmfSender.ontonechange = gDtmfOnToneChange;
}

// Internals.
/** @private */
function success_(method) {
  debug(method + '(): success.');
}

/** @private */
function failure_(method, error) {
  throw failTest(method + '() failed: ' + error);
}

/** @private */
function iceCallback_(event) {
  if (event.candidate)
    sendToPeer(gRemotePeerId, JSON.stringify(event.candidate));
}

/** @private */
function setLocalAndSendMessage_(session_description) {
  session_description.sdp = gTransformOutgoingSdp(session_description.sdp);
  peerConnection.setLocalDescription(
    session_description,
    function() { success_('setLocalDescription'); },
    function() { failure_('setLocalDescription'); });
  debug('Sending SDP message:\n' + session_description.sdp);
  sendToPeer(gRemotePeerId, JSON.stringify(session_description));
}

/** @private */
function addStreamCallback_(event) {
  debug('Receiving remote stream...');
  var videoTag = document.getElementById('remote-view');
  videoTag.src = webkitURL.createObjectURL(event.stream);

  // Due to crbug.com/110938 the size is 0 when onloadedmetadata fires.
  // videoTag.onloadedmetadata = displayVideoSize_(videoTag);
  // Use setTimeout as a workaround for now.
  // Displays the remote video size for both the video element and the stream.
  setTimeout(function() {displayVideoSize_(videoTag);}, 500);
}

/** @private */
function removeStreamCallback_(event) {
  debug('Call ended.');
  document.getElementById('remote-view').src = '';
}

/** @private */
function onCreateDataChannelCallback_(event) {
  if (gDataChannel != null && gDataChannel.readyState != 'closed') {
    throw failTest('Received DataChannel, but we already have one.');
  }

  gDataChannel = event.channel;
  debug('DataChannel with label ' + gDataChannel.label +
      ' initiated by remote peer.');
  hookupDataChannelEvents();
}

/** @private */
function hookupDataChannelEvents() {
  gDataChannel.onmessage = gDataCallback;
  gDataChannel.onopen = onDataChannelReadyStateChange_;
  gDataChannel.onclose = onDataChannelReadyStateChange_;
  // Trigger gDataStatusCallback so an application is notified
  // about the created data channel.
  onDataChannelReadyStateChange_();
}

/** @private */
function onDataChannelReadyStateChange_() {
  var readyState = gDataChannel.readyState;
  debug('DataChannel state:' + readyState);
  gDataStatusCallback(readyState);
}
