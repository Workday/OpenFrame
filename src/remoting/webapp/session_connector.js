// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * Connect set-up state machine for Me2Me and IT2Me
 */

'use strict';

/** @suppress {duplicate} */
var remoting = remoting || {};

/**
 * @param {Element} pluginParent The node under which to add the client plugin.
 * @param {function(remoting.ClientSession):void} onOk Callback on success.
 * @param {function(remoting.Error):void} onError Callback on error.
 * @constructor
 */
remoting.SessionConnector = function(pluginParent, onOk, onError) {
  /**
   * @type {Element}
   * @private
   */
  this.pluginParent_ = pluginParent;

  /**
   * @type {function(remoting.ClientSession):void}
   * @private
   */
  this.onOk_ = onOk;

  /**
   * @type {function(remoting.Error):void}
   * @private
   */
  this.onError_ = onError;

  /**
   * @type {string}
   * @private
   */
  this.clientJid_ = '';

  /**
   * @type {remoting.ClientSession.Mode}
   * @private
   */
  this.connectionMode_ = remoting.ClientSession.Mode.ME2ME;

  /**
   * A timer that polls for an updated access token.
   *
   * @type {number}
   * @private
   */
  this.wcsAccessTokenRefreshTimer_ = 0;

  // Initialize/declare per-connection state.
  this.reset();

  // Pre-load WCS to improve connection time.
  remoting.identity.callWithToken(this.loadWcs_.bind(this), this.onError_);
};

/**
 * Reset the per-connection state so that the object can be re-used for a
 * second connection. Note the none of the shared WCS state is reset.
 */
remoting.SessionConnector.prototype.reset = function() {
  /**
   * Set to true to indicate that the user requested pairing when entering
   * their PIN for a Me2Me connection.
   *
   * @type {boolean}
   */
  this.pairingRequested = false;

  /**
   * String used to identify the host to which to connect. For IT2Me, this is
   * the first 7 digits of the access code; for Me2Me it is the host identifier.
   *
   * @type {string}
   * @private
   */
  this.hostId_ = '';

  /**
   * For paired connections, the client id of this device, issued by the host.
   *
   * @type {string}
   * @private
   */
  this.clientPairingId_ = '';

  /**
   * For paired connections, the paired secret for this device, issued by the
   * host.
   *
   * @type {string}
   * @private
   */
  this.clientPairedSecret_ = '';

  /**
   * String used to authenticate to the host on connection. For IT2Me, this is
   * the access code; for Me2Me it is the PIN.
   *
   * @type {string}
   * @private
   */
  this.passPhrase_ = '';

  /**
   * @type {string}
   * @private
   */
  this.hostJid_ = '';

  /**
   * @type {string}
   * @private
   */
  this.hostPublicKey_ = '';

  /**
   * @type {boolean}
   * @private
   */
  this.refreshHostJidIfOffline_ = false;

  /**
   * @type {remoting.ClientSession}
   * @private
   */
  this.clientSession_ = null;

  /**
   * @type {XMLHttpRequest}
   * @private
   */
  this.pendingXhr_ = null;

  /**
   * Function to interactively obtain the PIN from the user.
   * @type {function(boolean, function(string):void):void}
   * @private
   */
  this.fetchPin_ = function(onPinFetched) {};

  /**
   * @type {function(string, string, string,
   *                 function(string, string):void): void}
   * @private
   */
  this.fetchThirdPartyToken_ = function(
      tokenUrl, scope, onThirdPartyTokenFetched) {};

  /**
   * Host 'name', as displayed in the client tool-bar. For a Me2Me connection,
   * this is the name of the host; for an IT2Me connection, it is the email
   * address of the person sharing their computer.
   *
   * @type {string}
   * @private
   */
  this.hostDisplayName_ = '';
};

/**
 * Initiate a Me2Me connection.
 *
 * @param {remoting.Host} host The Me2Me host to which to connect.
 * @param {function(boolean, function(string):void):void} fetchPin Function to
 *     interactively obtain the PIN from the user.
 * @param {function(string, string, string,
 *                  function(string, string): void): void}
 *     fetchThirdPartyToken Function to obtain a token from a third party
 *     authenticaiton server.
 * @param {string} clientPairingId The client id issued by the host when
 *     this device was paired, if it is already paired.
 * @param {string} clientPairedSecret The shared secret issued by the host when
 *     this device was paired, if it is already paired.
 * @return {void} Nothing.
 */
remoting.SessionConnector.prototype.connectMe2Me =
    function(host, fetchPin, fetchThirdPartyToken,
             clientPairingId, clientPairedSecret) {
  this.connectMe2MeInternal_(
      host.hostId, host.jabberId, host.publicKey, host.hostName,
      fetchPin, fetchThirdPartyToken,
      clientPairingId, clientPairedSecret, true);
};

/**
 * Update the pairing info so that the reconnect function will work correctly.
 *
 * @param {string} clientId The paired client id.
 * @param {string} sharedSecret The shared secret.
 */
remoting.SessionConnector.prototype.updatePairingInfo =
    function(clientId, sharedSecret) {
  this.clientPairingId_ = clientId;
  this.clientPairedSecret_ = sharedSecret;
};

/**
 * Initiate a Me2Me connection.
 *
 * @param {string} hostId ID of the Me2Me host.
 * @param {string} hostJid XMPP JID of the host.
 * @param {string} hostPublicKey Public Key of the host.
 * @param {string} hostDisplayName Display name (friendly name) of the host.
 * @param {function(boolean, function(string):void):void} fetchPin Function to
 *     interactively obtain the PIN from the user.
 * @param {function(string, string, string,
 *                  function(string, string): void): void}
 *     fetchThirdPartyToken Function to obtain a token from a third party
 *     authenticaiton server.
 * @param {string} clientPairingId The client id issued by the host when
 *     this device was paired, if it is already paired.
 * @param {string} clientPairedSecret The shared secret issued by the host when
 *     this device was paired, if it is already paired.
 * @param {boolean} refreshHostJidIfOffline Whether to refresh the JID and retry
 *     the connection if the current JID is offline.
 * @return {void} Nothing.
 * @private
 */
remoting.SessionConnector.prototype.connectMe2MeInternal_ =
    function(hostId, hostJid, hostPublicKey, hostDisplayName,
             fetchPin, fetchThirdPartyToken,
             clientPairingId, clientPairedSecret,
             refreshHostJidIfOffline) {
  // Cancel any existing connect operation.
  this.cancel();

  this.hostId_ = hostId;
  this.hostJid_ = hostJid;
  this.hostPublicKey_ = hostPublicKey;
  this.fetchPin_ = fetchPin;
  this.fetchThirdPartyToken_ = fetchThirdPartyToken;
  this.hostDisplayName_ = hostDisplayName;
  this.connectionMode_ = remoting.ClientSession.Mode.ME2ME;
  this.refreshHostJidIfOffline_ = refreshHostJidIfOffline;
  this.updatePairingInfo(clientPairingId, clientPairedSecret);
  this.createSessionIfReady_();
};

/**
 * Initiate an IT2Me connection.
 *
 * @param {string} accessCode The access code as entered by the user.
 * @return {void} Nothing.
 */
remoting.SessionConnector.prototype.connectIT2Me = function(accessCode) {
  var kSupportIdLen = 7;
  var kHostSecretLen = 5;
  var kAccessCodeLen = kSupportIdLen + kHostSecretLen;

  // Cancel any existing connect operation.
  this.cancel();

  var normalizedAccessCode = this.normalizeAccessCode_(accessCode);
  if (normalizedAccessCode.length != kAccessCodeLen) {
    this.onError_(remoting.Error.INVALID_ACCESS_CODE);
    return;
  }

  this.hostId_ = normalizedAccessCode.substring(0, kSupportIdLen);
  this.passPhrase_ = normalizedAccessCode;
  this.connectionMode_ = remoting.ClientSession.Mode.IT2ME;
  remoting.identity.callWithToken(this.connectIT2MeWithToken_.bind(this),
                                  this.onError_);
};

/**
 * Reconnect a closed connection.
 *
 * @return {void} Nothing.
 */
remoting.SessionConnector.prototype.reconnect = function() {
  if (this.connectionMode_ == remoting.ClientSession.Mode.IT2ME) {
    console.error('reconnect not supported for IT2Me.');
    return;
  }
  this.connectMe2MeInternal_(
      this.hostId_, this.hostJid_, this.hostPublicKey_, this.hostDisplayName_,
      this.fetchPin_, this.fetchThirdPartyToken_,
      this.clientPairingId_, this.clientPairedSecret_, true);
};

/**
 * Cancel a connection-in-progress.
 */
remoting.SessionConnector.prototype.cancel = function() {
  if (this.clientSession_) {
    this.clientSession_.removePlugin();
    this.clientSession_ = null;
  }
  if (this.pendingXhr_) {
    this.pendingXhr_.abort();
    this.pendingXhr_ = null;
  }
  this.reset();
};

/**
 * Get the connection mode (Me2Me or IT2Me)
 *
 * @return {remoting.ClientSession.Mode}
 */
remoting.SessionConnector.prototype.getConnectionMode = function() {
  return this.connectionMode_;
};

/**
 * Continue an IT2Me connection once an access token has been obtained.
 *
 * @param {string} token An OAuth2 access token.
 * @return {void} Nothing.
 * @private
 */
remoting.SessionConnector.prototype.connectIT2MeWithToken_ = function(token) {
  // Resolve the host id to get the host JID.
  this.pendingXhr_ = remoting.xhr.get(
      remoting.settings.DIRECTORY_API_BASE_URL + '/support-hosts/' +
          encodeURIComponent(this.hostId_),
      this.onIT2MeHostInfo_.bind(this),
      '',
      { 'Authorization': 'OAuth ' + token });
};

/**
 * Continue an IT2Me connection once the host JID has been looked up.
 *
 * @param {XMLHttpRequest} xhr The server response to the support-hosts query.
 * @return {void} Nothing.
 * @private
 */
remoting.SessionConnector.prototype.onIT2MeHostInfo_ = function(xhr) {
  this.pendingXhr_ = null;
  if (xhr.status == 200) {
    var host = /** @type {{data: {jabberId: string, publicKey: string}}} */
        jsonParseSafe(xhr.responseText);
    if (host && host.data && host.data.jabberId && host.data.publicKey) {
      this.hostJid_ = host.data.jabberId;
      this.hostPublicKey_ = host.data.publicKey;
      this.hostDisplayName_ = this.hostJid_.split('/')[0];
      this.createSessionIfReady_();
      return;
    } else {
      console.error('Invalid "support-hosts" response from server.');
    }
  } else {
    this.onError_(this.translateSupportHostsError(xhr.status));
  }
};

/**
 * Load the WCS driver script.
 *
 * @param {string} token An OAuth2 access token.
 * @return {void} Nothing.
 * @private
 */
remoting.SessionConnector.prototype.loadWcs_ = function(token) {
  remoting.wcsSandbox.setOnLocalJid(this.onLocalJid_.bind(this));
  remoting.wcsSandbox.setOnError(this.onError_);
  remoting.wcsSandbox.setAccessToken(token);
  this.startAccessTokenRefreshTimer_();
};

/**
 * Continue an IT2Me or Me2Me connection once WCS has been loaded.
 *
 * @param {string} clientJid The full JID of the WCS client.
 * @return {void} Nothing.
 * @private
 */
remoting.SessionConnector.prototype.onLocalJid_ = function(clientJid) {
  this.clientJid_ = clientJid;
  this.createSessionIfReady_();
};

/**
 * If both the client and host JIDs are available, create a session and connect.
 *
 * @return {void} Nothing.
 * @private
 */
remoting.SessionConnector.prototype.createSessionIfReady_ = function() {
  if (!this.clientJid_ || !this.hostJid_) {
    return;
  }

  // In some circumstances, the WCS <iframe> can get reloaded, which results
  // in a new clientJid and a new callback. In this case, remove the old
  // client plugin before instantiating a new one.
  if (this.clientSession_) {
    this.clientSession_.removePlugin();
    this.clientSession_ = null;
  }

  var securityTypes = 'third_party,spake2_pair,spake2_hmac,spake2_plain';
  this.clientSession_ = new remoting.ClientSession(
      this.hostJid_, this.clientJid_, this.hostPublicKey_, this.passPhrase_,
      this.fetchPin_, this.fetchThirdPartyToken_, securityTypes, this.hostId_,
      this.connectionMode_, this.hostDisplayName_, this.clientPairingId_,
      this.clientPairedSecret_);
  this.clientSession_.logHostOfflineErrors(!this.refreshHostJidIfOffline_);
  this.clientSession_.setOnStateChange(this.onStateChange_.bind(this));
  this.clientSession_.createPluginAndConnect(this.pluginParent_);
};

/**
 * Handle a change in the state of the client session prior to successful
 * connection (after connection, this class no longer handles state change
 * events). Errors that occur while connecting either trigger a reconnect
 * or notify the onError handler.
 *
 * @param {number} oldState The previous state of the plugin.
 * @param {number} newState The current state of the plugin.
 * @return {void} Nothing.
 * @private
 */
remoting.SessionConnector.prototype.onStateChange_ =
    function(oldState, newState) {
  switch (newState) {
    case remoting.ClientSession.State.CONNECTED:
      // When the connection succeeds, deregister for state-change callbacks
      // and pass the session to the onOk callback. It is expected that it
      // will register a new state-change callback to handle disconnect
      // or error conditions.
      this.clientSession_.setOnStateChange(null);
      this.onOk_(this.clientSession_);
      break;

    case remoting.ClientSession.State.CREATED:
      console.log('Created plugin');
      break;

    case remoting.ClientSession.State.BAD_PLUGIN_VERSION:
      this.onError_(remoting.Error.BAD_PLUGIN_VERSION);
      break;

    case remoting.ClientSession.State.CONNECTING:
      console.log('Connecting as ' + remoting.identity.getCachedEmail());
      break;

    case remoting.ClientSession.State.INITIALIZING:
      console.log('Initializing connection');
      break;

    case remoting.ClientSession.State.CLOSED:
      // This class deregisters for state-change callbacks when the CONNECTED
      // state is reached, so it only sees the CLOSED state in exceptional
      // circumstances. For example, a CONNECTING -> CLOSED transition happens
      // if the host closes the connection without an error message instead of
      // accepting it. Since there's no way of knowing exactly what went wrong,
      // we rely on server-side logs in this case and report a generic error
      // message.
      this.onError_(remoting.Error.UNEXPECTED);
      break;

    case remoting.ClientSession.State.FAILED:
      var error = this.clientSession_.getError();
      console.error('Client plugin reported connection failed: ' + error);
      if (error == null) {
        error = remoting.Error.UNEXPECTED;
      }
      if (error == remoting.Error.HOST_IS_OFFLINE &&
          this.refreshHostJidIfOffline_) {
        remoting.hostList.refresh(this.onHostListRefresh_.bind(this));
      } else {
        this.onError_(error);
      }
      break;

    default:
      console.error('Unexpected client plugin state: ' + newState);
      // This should only happen if the web-app and client plugin get out of
      // sync, and even then the version check should ensure compatibility.
      this.onError_(remoting.Error.MISSING_PLUGIN);
  }
};

/**
 * @param {boolean} success True if the host list was successfully refreshed;
 *     false if an error occurred.
 * @private
 */
remoting.SessionConnector.prototype.onHostListRefresh_ = function(success) {
  if (success) {
    var host = remoting.hostList.getHostForId(this.hostId_);
    if (host) {
      this.connectMe2MeInternal_(
          host.hostId, host.jabberId, host.publicKey, host.hostName,
          this.fetchPin_, this.fetchThirdPartyToken_,
          this.clientPairingId_, this.clientPairedSecret_, false);
      return;
    }
  }
  this.onError_(remoting.Error.HOST_IS_OFFLINE);
};

/**
 * Start a timer to periodically refresh the access token used by WCS. Access
 * tokens have a limited lifespan, and since the WCS driver runs in a sandbox,
 * it can't obtain a new one directly.
 *
 * @return {void} Nothing.
 * @private
 */
remoting.SessionConnector.prototype.startAccessTokenRefreshTimer_ = function() {
  if (this.wcsAccessTokenRefreshTimer_ != 0) {
    return;
  }

  /** @type {remoting.SessionConnector} */
  var that = this;
  var refreshAccessToken = function() {
    remoting.identity.callWithToken(
        remoting.wcsSandbox.setAccessToken.bind(remoting.wcsSandbox),
        that.onError_);
  };
  /**
   * A timer that polls for an updated access token.
   * @type {number}
   * @private
   */
  this.wcsAccessTokenRefreshTimer_ = setInterval(refreshAccessToken,
                                                 60 * 1000);
}

/**
 * @param {number} error An HTTP error code returned by the support-hosts
 *     endpoint.
 * @return {remoting.Error} The equivalent remoting.Error code.
 * @private
 */
remoting.SessionConnector.prototype.translateSupportHostsError =
    function(error) {
  switch (error) {
    case 0: return remoting.Error.NETWORK_FAILURE;
    case 404: return remoting.Error.INVALID_ACCESS_CODE;
    case 502: // No break
    case 503: return remoting.Error.SERVICE_UNAVAILABLE;
    default: return remoting.Error.UNEXPECTED;
  }
};

/**
 * Normalize the access code entered by the user.
 *
 * @param {string} accessCode The access code, as entered by the user.
 * @return {string} The normalized form of the code (whitespace removed).
 */
remoting.SessionConnector.prototype.normalizeAccessCode_ =
    function(accessCode) {
  // Trim whitespace.
  return accessCode.replace(/\s/g, '');
};
