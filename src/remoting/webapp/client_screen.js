// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * Functions related to the 'client screen' for Chromoting.
 */

'use strict';

/** @suppress {duplicate} */
var remoting = remoting || {};

/**
 * @type {remoting.SessionConnector} The connector object, set when a connection
 *     is initiated.
 */
remoting.connector = null;

/**
 * @type {remoting.ClientSession} The client session object, set once the
 *     connector has invoked its onOk callback.
 */
remoting.clientSession = null;

/**
 * Initiate an IT2Me connection.
 */
remoting.connectIT2Me = function() {
  if (!remoting.connector) {
    remoting.connector = new remoting.SessionConnector(
        document.getElementById('session-mode'),
        remoting.onConnected,
        showConnectError_);
  }
  var accessCode = document.getElementById('access-code-entry').value;
  remoting.setMode(remoting.AppMode.CLIENT_CONNECTING);
  remoting.connector.connectIT2Me(accessCode);
};

/**
 * Update the remoting client layout in response to a resize event.
 *
 * @return {void} Nothing.
 */
remoting.onResize = function() {
  if (remoting.clientSession) {
    remoting.clientSession.onResize();
  }
};

/**
 * Handle changes in the visibility of the window, for example by pausing video.
 *
 * @return {void} Nothing.
 */
remoting.onVisibilityChanged = function() {
  if (remoting.clientSession) {
    remoting.clientSession.pauseVideo(document.webkitHidden);
  }
}

/**
 * Disconnect the remoting client.
 *
 * @return {void} Nothing.
 */
remoting.disconnect = function() {
  if (!remoting.clientSession) {
    return;
  }
  if (remoting.clientSession.mode == remoting.ClientSession.Mode.IT2ME) {
    remoting.setMode(remoting.AppMode.CLIENT_SESSION_FINISHED_IT2ME);
  } else {
    remoting.setMode(remoting.AppMode.CLIENT_SESSION_FINISHED_ME2ME);
  }
  remoting.clientSession.disconnect(true);
  remoting.clientSession = null;
  console.log('Disconnected.');
};

/**
 * Sends a Ctrl-Alt-Del sequence to the remoting client.
 *
 * @return {void} Nothing.
 */
remoting.sendCtrlAltDel = function() {
  if (remoting.clientSession) {
    console.log('Sending Ctrl-Alt-Del.');
    remoting.clientSession.sendCtrlAltDel();
  }
};

/**
 * Sends a Print Screen keypress to the remoting client.
 *
 * @return {void} Nothing.
 */
remoting.sendPrintScreen = function() {
  if (remoting.clientSession) {
    console.log('Sending Print Screen.');
    remoting.clientSession.sendPrintScreen();
  }
};

/**
 * Callback function called when the state of the client plugin changes. The
 * current state is available via the |state| member variable.
 *
 * @param {number} oldState The previous state of the plugin.
 * @param {number} newState The current state of the plugin.
 */
function onClientStateChange_(oldState, newState) {
  switch (newState) {
    case remoting.ClientSession.State.CLOSED:
      console.log('Connection closed by host');
      if (remoting.clientSession.mode == remoting.ClientSession.Mode.IT2ME) {
        remoting.setMode(remoting.AppMode.CLIENT_SESSION_FINISHED_IT2ME);
      } else {
        remoting.setMode(remoting.AppMode.CLIENT_SESSION_FINISHED_ME2ME);
      }
      break;

    case remoting.ClientSession.State.FAILED:
      var error = remoting.clientSession.getError();
      console.error('Client plugin reported connection failed: ' + error);
      if (error == null) {
        error = remoting.Error.UNEXPECTED;
      }
      showConnectError_(error);
      break;

    default:
      console.error('Unexpected client plugin state: ' + newState);
      // This should only happen if the web-app and client plugin get out of
      // sync, so MISSING_PLUGIN is a suitable error.
      showConnectError_(remoting.Error.MISSING_PLUGIN);
      break;
  }
  remoting.clientSession.disconnect(false);
  remoting.clientSession.removePlugin();
  remoting.clientSession = null;
}

/**
 * Show a client-side error message.
 *
 * @param {remoting.Error} errorTag The error to be localized and
 *     displayed.
 * @return {void} Nothing.
 */
function showConnectError_(errorTag) {
  console.error('Connection failed: ' + errorTag);
  var errorDiv = document.getElementById('connect-error-message');
  l10n.localizeElementFromTag(errorDiv, /** @type {string} */ (errorTag));
  remoting.accessCode = '';
  var mode = remoting.clientSession ? remoting.clientSession.mode
                                    : remoting.connector.getConnectionMode();
  if (mode == remoting.ClientSession.Mode.IT2ME) {
    remoting.setMode(remoting.AppMode.CLIENT_CONNECT_FAILED_IT2ME);
  } else {
    remoting.setMode(remoting.AppMode.CLIENT_CONNECT_FAILED_ME2ME);
  }
}

/**
 * Set the text on the buttons shown under the error message so that they are
 * easy to understand in the case where a successful connection failed, as
 * opposed to the case where a connection never succeeded.
 */
function setConnectionInterruptedButtonsText_() {
  var button1 = document.getElementById('client-reconnect-button');
  l10n.localizeElementFromTag(button1, /*i18n-content*/'RECONNECT');
  button1.removeAttribute('autofocus');
  var button2 = document.getElementById('client-finished-me2me-button');
  l10n.localizeElementFromTag(button2, /*i18n-content*/'OK');
  button2.setAttribute('autofocus', 'autofocus');
}

/**
 * Timer callback to update the statistics panel.
 */
function updateStatistics_() {
  if (!remoting.clientSession ||
      remoting.clientSession.state != remoting.ClientSession.State.CONNECTED) {
    return;
  }
  var perfstats = remoting.clientSession.getPerfStats();
  remoting.stats.update(perfstats);
  remoting.clientSession.logStatistics(perfstats);
  // Update the stats once per second.
  window.setTimeout(updateStatistics_, 1000);
}

/**
 * Entry-point for Me2Me connections, handling showing of the host-upgrade nag
 * dialog if necessary.
 *
 * @param {string} hostId The unique id of the host.
 * @return {void} Nothing.
 */
remoting.connectMe2Me = function(hostId) {
  var host = remoting.hostList.getHostForId(hostId);
  if (!host) {
    showConnectError_(remoting.Error.HOST_IS_OFFLINE);
    return;
  }
  var webappVersion = chrome.runtime.getManifest().version;
  if (remoting.Host.needsUpdate(host, webappVersion)) {
    var needsUpdateMessage =
        document.getElementById('host-needs-update-message');
    l10n.localizeElementFromTag(needsUpdateMessage,
                                /*i18n-content*/'HOST_NEEDS_UPDATE_TITLE',
                                host.hostName);
    /** @type {Element} */
    var connect = document.getElementById('host-needs-update-connect-button');
    /** @type {Element} */
    var cancel = document.getElementById('host-needs-update-cancel-button');
    /** @param {Event} event */
    var onClick = function(event) {
      connect.removeEventListener('click', onClick, false);
      cancel.removeEventListener('click', onClick, false);
      if (event.target == connect) {
        remoting.connectMe2MeHostVersionAcknowledged_(host);
      } else {
        remoting.setMode(remoting.AppMode.HOME);
      }
    }
    connect.addEventListener('click', onClick, false);
    cancel.addEventListener('click', onClick, false);
    remoting.setMode(remoting.AppMode.CLIENT_HOST_NEEDS_UPGRADE);
  } else {
    remoting.connectMe2MeHostVersionAcknowledged_(host);
  }
};

/**
 * Shows PIN entry screen localized to include the host name, and registers
 * a host-specific one-shot event handler for the form submission.
 *
 * @param {remoting.Host} host The Me2Me host to which to connect.
 * @return {void} Nothing.
 */
remoting.connectMe2MeHostVersionAcknowledged_ = function(host) {
  if (!remoting.connector) {
    remoting.connector = new remoting.SessionConnector(
        document.getElementById('session-mode'),
        remoting.onConnected,
        showConnectError_);
  }
  remoting.setMode(remoting.AppMode.CLIENT_CONNECTING);

  /**
   * @param {string} tokenUrl Token-issue URL received from the host.
   * @param {string} scope OAuth scope to request the token for.
   * @param {string} hostPublicKey Host public key (DER and Base64 encoded).
   * @param {function(string, string):void} onThirdPartyTokenFetched Callback.
   */
  var fetchThirdPartyToken = function(
      tokenUrl, hostPublicKey, scope, onThirdPartyTokenFetched) {
    var thirdPartyTokenFetcher = new remoting.ThirdPartyTokenFetcher(
        tokenUrl, hostPublicKey, scope, host.tokenUrlPatterns,
        onThirdPartyTokenFetched);
    thirdPartyTokenFetcher.fetchToken();
  };

  /**
   * @param {boolean} supportsPairing
   * @param {function(string):void} onPinFetched
   */
  var requestPin = function(supportsPairing, onPinFetched) {
    /** @type {Element} */
    var pinForm = document.getElementById('pin-form');
    /** @type {Element} */
    var pinCancel = document.getElementById('cancel-pin-entry-button');
    /** @type {Element} */
    var rememberPin = document.getElementById('remember-pin');
    /** @type {Element} */
    var rememberPinCheckbox = document.getElementById('remember-pin-checkbox');
    /**
     * Event handler for both the 'submit' and 'cancel' actions. Using
     * a single handler for both greatly simplifies the task of making
     * them one-shot. If separate handlers were used, each would have
     * to unregister both itself and the other.
     *
     * @param {Event} event The click or submit event.
     */
    var onSubmitOrCancel = function(event) {
      pinForm.removeEventListener('submit', onSubmitOrCancel, false);
      pinCancel.removeEventListener('click', onSubmitOrCancel, false);
      var pinField = document.getElementById('pin-entry');
      var pin = pinField.value;
      pinField.value = '';
      if (event.target == pinForm) {
        event.preventDefault();
        remoting.setMode(remoting.AppMode.CLIENT_CONNECTING);
        onPinFetched(pin);
        if (/** @type {boolean} */(rememberPinCheckbox.checked)) {
          remoting.connector.pairingRequested = true;
        }
      } else {
        remoting.setMode(remoting.AppMode.HOME);
      }
    };
    pinForm.addEventListener('submit', onSubmitOrCancel, false);
    pinCancel.addEventListener('click', onSubmitOrCancel, false);
    rememberPin.hidden = !supportsPairing;
    rememberPinCheckbox.checked = false;
    var message = document.getElementById('pin-message');
    l10n.localizeElement(message, host.hostName);
    remoting.setMode(remoting.AppMode.CLIENT_PIN_PROMPT);
  };

  /** @param {Object} settings */
  var connectMe2MeHostSettingsRetrieved = function(settings) {
    /** @type {string} */
    var clientId = '';
    /** @type {string} */
    var sharedSecret = '';
    var pairingInfo = /** @type {Object} */ (settings['pairingInfo']);
    if (pairingInfo) {
      clientId = /** @type {string} */ (pairingInfo['clientId']);
      sharedSecret = /** @type {string} */ (pairingInfo['sharedSecret']);
    }
    remoting.connector.connectMe2Me(host, requestPin, fetchThirdPartyToken,
                                    clientId, sharedSecret);
  }

  remoting.HostSettings.load(host.hostId, connectMe2MeHostSettingsRetrieved);
};

/** @param {remoting.ClientSession} clientSession */
remoting.onConnected = function(clientSession) {
  remoting.clientSession = clientSession;
  remoting.clientSession.setOnStateChange(onClientStateChange_);
  setConnectionInterruptedButtonsText_();
  var connectedTo = document.getElementById('connected-to');
  connectedTo.innerText = clientSession.hostDisplayName;
  document.getElementById('access-code-entry').value = '';
  remoting.setMode(remoting.AppMode.IN_SESSION);
  remoting.toolbar.center();
  remoting.toolbar.preview();
  remoting.clipboard.startSession();
  updateStatistics_();
  if (remoting.connector.pairingRequested) {
    /**
     * @param {string} clientId
     * @param {string} sharedSecret
     */
    var onPairingComplete = function(clientId, sharedSecret) {
      var pairingInfo = {
        pairingInfo: {
          clientId: clientId,
          sharedSecret: sharedSecret
        }
      };
      remoting.HostSettings.save(clientSession.hostId, pairingInfo);
      remoting.connector.updatePairingInfo(clientId, sharedSecret);
    };
    // Use the platform name as a proxy for the local computer name.
    // TODO(jamiewalch): Use a descriptive name for the local computer, for
    // example, its Chrome Sync name.
    var clientName = '';
    if (navigator.platform.indexOf('Mac') != -1) {
      clientName = 'Mac';
    } else if (navigator.platform.indexOf('Win32') != -1) {
      clientName = 'Windows';
    } else if (navigator.userAgent.match(/\bCrOS\b/)) {
      clientName = 'ChromeOS';
    } else if (navigator.platform.indexOf('Linux') != -1) {
      clientName = 'Linux';
    } else {
      console.log('Unrecognized client platform. Using navigator.platform.');
      clientName = navigator.platform;
    }
    clientSession.requestPairing(clientName, onPairingComplete);
  }
};
