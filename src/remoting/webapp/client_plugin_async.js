// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * Class that wraps low-level details of interacting with the client plugin.
 *
 * This abstracts a <embed> element and controls the plugin which does
 * the actual remoting work. It also handles differences between
 * client plugins versions when it is necessary.
 */

'use strict';

/** @suppress {duplicate} */
var remoting = remoting || {};

/**
 * @param {remoting.ViewerPlugin} plugin The plugin embed element.
 * @constructor
 * @implements {remoting.ClientPlugin}
 */
remoting.ClientPluginAsync = function(plugin) {
  this.plugin = plugin;

  this.desktopWidth = 0;
  this.desktopHeight = 0;
  this.desktopXDpi = 96;
  this.desktopYDpi = 96;

  /** @param {string} iq The Iq stanza received from the host. */
  this.onOutgoingIqHandler = function (iq) {};
  /** @param {string} message Log message. */
  this.onDebugMessageHandler = function (message) {};
  /**
   * @param {number} state The connection state.
   * @param {number} error The error code, if any.
   */
  this.onConnectionStatusUpdateHandler = function(state, error) {};
  /** @param {boolean} ready Connection ready state. */
  this.onConnectionReadyHandler = function(ready) {};
  /**
   * @param {string} tokenUrl Token-request URL, received from the host.
   * @param {string} hostPublicKey Public key for the host.
   * @param {string} scope OAuth scope to request the token for.
   */
  this.fetchThirdPartyTokenHandler = function(
    tokenUrl, hostPublicKey, scope) {};
  this.onDesktopSizeUpdateHandler = function () {};
  /** @param {!Array.<string>} capabilities The negotiated capabilities. */
  this.onSetCapabilitiesHandler = function (capabilities) {};
  this.fetchPinHandler = function (supportsPairing) {};

  /** @type {number} */
  this.pluginApiVersion_ = -1;
  /** @type {Array.<string>} */
  this.pluginApiFeatures_ = [];
  /** @type {number} */
  this.pluginApiMinVersion_ = -1;
  /** @type {!Array.<string>} */
  this.capabilities_ = [];
  /** @type {boolean} */
  this.helloReceived_ = false;
  /** @type {function(boolean)|null} */
  this.onInitializedCallback_ = null;
  /** @type {function(string, string):void} */
  this.onPairingComplete_ = function(clientId, sharedSecret) {};
  /** @type {remoting.ClientSession.PerfStats} */
  this.perfStats_ = new remoting.ClientSession.PerfStats();

  /** @type {remoting.ClientPluginAsync} */
  var that = this;
  /** @param {Event} event Message event from the plugin. */
  this.plugin.addEventListener('message', function(event) {
      that.handleMessage_(event.data);
    }, false);
  window.setTimeout(this.showPluginForClickToPlay_.bind(this), 500);
};

/**
 * Chromoting session API version (for this javascript).
 * This is compared with the plugin API version to verify that they are
 * compatible.
 *
 * @const
 * @private
 */
remoting.ClientPluginAsync.prototype.API_VERSION_ = 6;

/**
 * The oldest API version that we support.
 * This will differ from the |API_VERSION_| if we maintain backward
 * compatibility with older API versions.
 *
 * @const
 * @private
 */
remoting.ClientPluginAsync.prototype.API_MIN_VERSION_ = 5;

/**
 * @param {string} messageStr Message from the plugin.
 */
remoting.ClientPluginAsync.prototype.handleMessage_ = function(messageStr) {
  var message = /** @type {{method:string, data:Object.<string,string>}} */
      jsonParseSafe(messageStr);

  if (!message || !('method' in message) || !('data' in message)) {
    console.error('Received invalid message from the plugin: ' + messageStr);
    return;
  }

  /**
   * Splits a string into a list of words delimited by spaces.
   * @param {string} str String that should be split.
   * @return {!Array.<string>} List of words.
   */
  var tokenize = function(str) {
    /** @type {Array.<string>} */
    var tokens = str.match(/\S+/g);
    return tokens ? tokens : [];
  };

  if (message.method == 'hello') {
    // Reset the size in case we had to enlarge it to support click-to-play.
    this.plugin.width = 0;
    this.plugin.height = 0;
    if (typeof message.data['apiVersion'] != 'number' ||
        typeof message.data['apiMinVersion'] != 'number') {
      console.error('Received invalid hello message: ' + messageStr);
      return;
    }
    this.pluginApiVersion_ = /** @type {number} */ message.data['apiVersion'];

    if (this.pluginApiVersion_ >= 7) {
      if (typeof message.data['apiFeatures'] != 'string') {
        console.error('Received invalid hello message: ' + messageStr);
        return;
      }
      this.pluginApiFeatures_ =
          /** @type {Array.<string>} */ tokenize(message.data['apiFeatures']);

      // Negotiate capabilities.

      /** @type {!Array.<string>} */
      var requestedCapabilities = [];
      if ('requestedCapabilities' in message.data) {
        if (typeof message.data['requestedCapabilities'] != 'string') {
          console.error('Received invalid hello message: ' + messageStr);
          return;
        }
        requestedCapabilities = tokenize(message.data['requestedCapabilities']);
      }

      /** @type {!Array.<string>} */
      var supportedCapabilities = [];
      if ('supportedCapabilities' in message.data) {
        if (typeof message.data['supportedCapabilities'] != 'string') {
          console.error('Received invalid hello message: ' + messageStr);
          return;
        }
        supportedCapabilities = tokenize(message.data['supportedCapabilities']);
      }

      // At the moment the webapp does not recognize any of
      // 'requestedCapabilities' capabilities (so they all should be disabled)
      // and do not care about any of 'supportedCapabilities' capabilities (so
      // they all can be enabled).
      this.capabilities_ = supportedCapabilities;

      // Let the host know that the webapp can be requested to always send
      // the client's dimensions.
      this.capabilities_.push(
          remoting.ClientSession.Capability.SEND_INITIAL_RESOLUTION);

      // Let the host know that we're interested in knowing whether or not
      // it rate-limits desktop-resize requests.
      this.capabilities_.push(
          remoting.ClientSession.Capability.RATE_LIMIT_RESIZE_REQUESTS);
    } else if (this.pluginApiVersion_ >= 6) {
      this.pluginApiFeatures_ = ['highQualityScaling', 'injectKeyEvent'];
    } else {
      this.pluginApiFeatures_ = ['highQualityScaling'];
    }
    this.pluginApiMinVersion_ =
        /** @type {number} */ message.data['apiMinVersion'];
    this.helloReceived_ = true;
    if (this.onInitializedCallback_ != null) {
      this.onInitializedCallback_(true);
      this.onInitializedCallback_ = null;
    }
  } else if (message.method == 'sendOutgoingIq') {
    if (typeof message.data['iq'] != 'string') {
      console.error('Received invalid sendOutgoingIq message: ' + messageStr);
      return;
    }
    this.onOutgoingIqHandler(message.data['iq']);
  } else if (message.method == 'logDebugMessage') {
    if (typeof message.data['message'] != 'string') {
      console.error('Received invalid logDebugMessage message: ' + messageStr);
      return;
    }
    this.onDebugMessageHandler(message.data['message']);
  } else if (message.method == 'onConnectionStatus') {
    if (typeof message.data['state'] != 'string' ||
        !remoting.ClientSession.State.hasOwnProperty(message.data['state']) ||
        typeof message.data['error'] != 'string') {
      console.error('Received invalid onConnectionState message: ' +
                    messageStr);
      return;
    }

    /** @type {remoting.ClientSession.State} */
    var state = remoting.ClientSession.State[message.data['state']];
    var error;
    if (remoting.ClientSession.ConnectionError.hasOwnProperty(
        message.data['error'])) {
      error = /** @type {remoting.ClientSession.ConnectionError} */
          remoting.ClientSession.ConnectionError[message.data['error']];
    } else {
      error = remoting.ClientSession.ConnectionError.UNKNOWN;
    }

    this.onConnectionStatusUpdateHandler(state, error);
  } else if (message.method == 'onDesktopSize') {
    if (typeof message.data['width'] != 'number' ||
        typeof message.data['height'] != 'number') {
      console.error('Received invalid onDesktopSize message: ' + messageStr);
      return;
    }
    this.desktopWidth = /** @type {number} */ message.data['width'];
    this.desktopHeight = /** @type {number} */ message.data['height'];
    this.desktopXDpi = (typeof message.data['x_dpi'] == 'number') ?
        /** @type {number} */ (message.data['x_dpi']) : 96;
    this.desktopYDpi = (typeof message.data['y_dpi'] == 'number') ?
        /** @type {number} */ (message.data['y_dpi']) : 96;
    this.onDesktopSizeUpdateHandler();
  } else if (message.method == 'onPerfStats') {
    if (typeof message.data['videoBandwidth'] != 'number' ||
        typeof message.data['videoFrameRate'] != 'number' ||
        typeof message.data['captureLatency'] != 'number' ||
        typeof message.data['encodeLatency'] != 'number' ||
        typeof message.data['decodeLatency'] != 'number' ||
        typeof message.data['renderLatency'] != 'number' ||
        typeof message.data['roundtripLatency'] != 'number') {
      console.error('Received incorrect onPerfStats message: ' + messageStr);
      return;
    }
    this.perfStats_ =
        /** @type {remoting.ClientSession.PerfStats} */ message.data;
  } else if (message.method == 'injectClipboardItem') {
    if (typeof message.data['mimeType'] != 'string' ||
        typeof message.data['item'] != 'string') {
      console.error('Received incorrect injectClipboardItem message.');
      return;
    }
    if (remoting.clipboard) {
      remoting.clipboard.fromHost(message.data['mimeType'],
                                  message.data['item']);
    }
  } else if (message.method == 'onFirstFrameReceived') {
    if (remoting.clientSession) {
      remoting.clientSession.onFirstFrameReceived();
    }
  } else if (message.method == 'onConnectionReady') {
    if (typeof message.data['ready'] != 'boolean') {
      console.error('Received incorrect onConnectionReady message.');
      return;
    }
    var ready = /** @type {boolean} */ message.data['ready'];
    this.onConnectionReadyHandler(ready);
  } else if (message.method == 'fetchPin') {
    // The pairingSupported value in the dictionary indicates whether both
    // client and host support pairing. If the client doesn't support pairing,
    // then the value won't be there at all, so give it a default of false.
    /** @type {boolean} */
    var pairingSupported = false;
    if ('pairingSupported' in message.data) {
      pairingSupported =
          /** @type {boolean} */ message.data['pairingSupported'];
      if (typeof pairingSupported != 'boolean') {
        console.error('Received incorrect fetchPin message.');
        return;
      }
    }
    this.fetchPinHandler(pairingSupported);
  } else if (message.method == 'setCapabilities') {
    if (typeof message.data['capabilities'] != 'string') {
      console.error('Received incorrect setCapabilities message.');
      return;
    }

    /** @type {!Array.<string>} */
    var capabilities = tokenize(message.data['capabilities']);
    this.onSetCapabilitiesHandler(capabilities);
  } else if (message.method == 'fetchThirdPartyToken') {
    if (typeof message.data['tokenUrl'] != 'string' ||
        typeof message.data['hostPublicKey'] != 'string' ||
        typeof message.data['scope'] != 'string') {
      console.error('Received incorrect fetchThirdPartyToken message.');
      return;
    }
    var tokenUrl = /** @type {string} */ message.data['tokenUrl'];
    var hostPublicKey =
        /** @type {string} */ message.data['hostPublicKey'];
    var scope = /** @type {string} */ message.data['scope'];
    this.fetchThirdPartyTokenHandler(tokenUrl, hostPublicKey, scope);
  } else if (message.method == 'pairingResponse') {
    var clientId = /** @type {string} */ message.data['clientId'];
    var sharedSecret = /** @type {string} */ message.data['sharedSecret'];
    if (typeof clientId != 'string' || typeof sharedSecret != 'string') {
      console.error('Received incorrect pairingResponse message.');
      return;
    }
    this.onPairingComplete_(clientId, sharedSecret);
  } else if (message.method == 'extensionMessage') {
    // No messages currently supported.
    console.log('Unexpected message received: ' +
                message.data.type + ': ' + message.data.data);
  }
};

/**
 * Deletes the plugin.
 */
remoting.ClientPluginAsync.prototype.cleanup = function() {
  this.plugin.parentNode.removeChild(this.plugin);
};

/**
 * @return {HTMLEmbedElement} HTML element that correspods to the plugin.
 */
remoting.ClientPluginAsync.prototype.element = function() {
  return this.plugin;
};

/**
 * @param {function(boolean): void} onDone
 */
remoting.ClientPluginAsync.prototype.initialize = function(onDone) {
  if (this.helloReceived_) {
    onDone(true);
  } else {
    this.onInitializedCallback_ = onDone;
  }
};

/**
 * @return {boolean} True if the plugin and web-app versions are compatible.
 */
remoting.ClientPluginAsync.prototype.isSupportedVersion = function() {
  if (!this.helloReceived_) {
    console.error(
        "isSupportedVersion() is called before the plugin is initialized.");
    return false;
  }
  return this.API_VERSION_ >= this.pluginApiMinVersion_ &&
      this.pluginApiVersion_ >= this.API_MIN_VERSION_;
};

/**
 * @param {remoting.ClientPlugin.Feature} feature The feature to test for.
 * @return {boolean} True if the plugin supports the named feature.
 */
remoting.ClientPluginAsync.prototype.hasFeature = function(feature) {
  if (!this.helloReceived_) {
    console.error(
        "hasFeature() is called before the plugin is initialized.");
    return false;
  }
  return this.pluginApiFeatures_.indexOf(feature) > -1;
};

/**
 * @return {boolean} True if the plugin supports the injectKeyEvent API.
 */
remoting.ClientPluginAsync.prototype.isInjectKeyEventSupported = function() {
  return this.pluginApiVersion_ >= 6;
};

/**
 * @param {string} iq Incoming IQ stanza.
 */
remoting.ClientPluginAsync.prototype.onIncomingIq = function(iq) {
  if (this.plugin && this.plugin.postMessage) {
    this.plugin.postMessage(JSON.stringify(
        { method: 'incomingIq', data: { iq: iq } }));
  } else {
    // plugin.onIq may not be set after the plugin has been shut
    // down. Particularly this happens when we receive response to
    // session-terminate stanza.
    console.warn('plugin.onIq is not set so dropping incoming message.');
  }
};

/**
 * @param {string} hostJid The jid of the host to connect to.
 * @param {string} hostPublicKey The base64 encoded version of the host's
 *     public key.
 * @param {string} localJid Local jid.
 * @param {string} sharedSecret The access code for IT2Me or the PIN
 *     for Me2Me.
 * @param {string} authenticationMethods Comma-separated list of
 *     authentication methods the client should attempt to use.
 * @param {string} authenticationTag A host-specific tag to mix into
 *     authentication hashes.
 * @param {string} clientPairingId For paired Me2Me connections, the
 *     pairing id for this client, as issued by the host.
 * @param {string} clientPairedSecret For paired Me2Me connections, the
 *     paired secret for this client, as issued by the host.
 */
remoting.ClientPluginAsync.prototype.connect = function(
    hostJid, hostPublicKey, localJid, sharedSecret,
    authenticationMethods, authenticationTag,
    clientPairingId, clientPairedSecret) {
  this.plugin.postMessage(JSON.stringify(
    { method: 'connect', data: {
        hostJid: hostJid,
        hostPublicKey: hostPublicKey,
        localJid: localJid,
        sharedSecret: sharedSecret,
        authenticationMethods: authenticationMethods,
        authenticationTag: authenticationTag,
        capabilities: this.capabilities_.join(" "),
        clientPairingId: clientPairingId,
        clientPairedSecret: clientPairedSecret
      }
    }));
};

/**
 * Release all currently pressed keys.
 */
remoting.ClientPluginAsync.prototype.releaseAllKeys = function() {
  this.plugin.postMessage(JSON.stringify(
      { method: 'releaseAllKeys', data: {} }));
};

/**
 * Send a key event to the host.
 *
 * @param {number} usbKeycode The USB-style code of the key to inject.
 * @param {boolean} pressed True to inject a key press, False for a release.
 */
remoting.ClientPluginAsync.prototype.injectKeyEvent =
    function(usbKeycode, pressed) {
  this.plugin.postMessage(JSON.stringify(
      { method: 'injectKeyEvent', data: {
          'usbKeycode': usbKeycode,
          'pressed': pressed}
      }));
};

/**
 * Remap one USB keycode to another in all subsequent key events.
 *
 * @param {number} fromKeycode The USB-style code of the key to remap.
 * @param {number} toKeycode The USB-style code to remap the key to.
 */
remoting.ClientPluginAsync.prototype.remapKey =
    function(fromKeycode, toKeycode) {
  this.plugin.postMessage(JSON.stringify(
      { method: 'remapKey', data: {
          'fromKeycode': fromKeycode,
          'toKeycode': toKeycode}
      }));
};

/**
 * Enable/disable redirection of the specified key to the web-app.
 *
 * @param {number} keycode The USB-style code of the key.
 * @param {Boolean} trap True to enable trapping, False to disable.
 */
remoting.ClientPluginAsync.prototype.trapKey = function(keycode, trap) {
  this.plugin.postMessage(JSON.stringify(
      { method: 'trapKey', data: {
          'keycode': keycode,
          'trap': trap}
      }));
};

/**
 * Returns an associative array with a set of stats for this connecton.
 *
 * @return {remoting.ClientSession.PerfStats} The connection statistics.
 */
remoting.ClientPluginAsync.prototype.getPerfStats = function() {
  return this.perfStats_;
};

/**
 * Sends a clipboard item to the host.
 *
 * @param {string} mimeType The MIME type of the clipboard item.
 * @param {string} item The clipboard item.
 */
remoting.ClientPluginAsync.prototype.sendClipboardItem =
    function(mimeType, item) {
  if (!this.hasFeature(remoting.ClientPlugin.Feature.SEND_CLIPBOARD_ITEM))
    return;
  this.plugin.postMessage(JSON.stringify(
      { method: 'sendClipboardItem',
        data: { mimeType: mimeType, item: item }}));
};

/**
 * Notifies the host that the client has the specified size and pixel density.
 *
 * @param {number} width The available client width in DIPs.
 * @param {number} height The available client height in DIPs.
 * @param {number} device_scale The number of device pixels per DIP.
 */
remoting.ClientPluginAsync.prototype.notifyClientResolution =
    function(width, height, device_scale) {
  if (this.hasFeature(remoting.ClientPlugin.Feature.NOTIFY_CLIENT_RESOLUTION)) {
    var dpi = device_scale * 96;
    this.plugin.postMessage(JSON.stringify(
        { method: 'notifyClientResolution',
          data: { width: width * device_scale,
                  height: height * device_scale,
                  x_dpi: dpi, y_dpi: dpi }}));
  } else if (this.hasFeature(
                 remoting.ClientPlugin.Feature.NOTIFY_CLIENT_DIMENSIONS)) {
    this.plugin.postMessage(JSON.stringify(
        { method: 'notifyClientDimensions',
          data: { width: width, height: height }}));
  }
};

/**
 * Requests that the host pause or resume sending video updates.
 *
 * @param {boolean} pause True to suspend video updates, false otherwise.
 */
remoting.ClientPluginAsync.prototype.pauseVideo =
    function(pause) {
  if (!this.hasFeature(remoting.ClientPlugin.Feature.PAUSE_VIDEO))
    return;
  this.plugin.postMessage(JSON.stringify(
      { method: 'pauseVideo', data: { pause: pause }}));
};

/**
 * Requests that the host pause or resume sending audio updates.
 *
 * @param {boolean} pause True to suspend audio updates, false otherwise.
 */
remoting.ClientPluginAsync.prototype.pauseAudio =
    function(pause) {
  if (!this.hasFeature(remoting.ClientPlugin.Feature.PAUSE_AUDIO))
    return;
  this.plugin.postMessage(JSON.stringify(
      { method: 'pauseAudio', data: { pause: pause }}));
};

/**
 * Called when a PIN is obtained from the user.
 *
 * @param {string} pin The PIN.
 */
remoting.ClientPluginAsync.prototype.onPinFetched =
    function(pin) {
  if (!this.hasFeature(remoting.ClientPlugin.Feature.ASYNC_PIN)) {
    return;
  }
  this.plugin.postMessage(JSON.stringify(
      { method: 'onPinFetched', data: { pin: pin }}));
};

/**
 * Tells the plugin to ask for the PIN asynchronously.
 */
remoting.ClientPluginAsync.prototype.useAsyncPinDialog =
    function() {
  if (!this.hasFeature(remoting.ClientPlugin.Feature.ASYNC_PIN)) {
    return;
  }
  this.plugin.postMessage(JSON.stringify(
      { method: 'useAsyncPinDialog', data: {} }));
};

/**
 * Sets the third party authentication token and shared secret.
 *
 * @param {string} token The token received from the token URL.
 * @param {string} sharedSecret Shared secret received from the token URL.
 */
remoting.ClientPluginAsync.prototype.onThirdPartyTokenFetched = function(
    token, sharedSecret) {
  this.plugin.postMessage(JSON.stringify(
    { method: 'onThirdPartyTokenFetched',
      data: { token: token, sharedSecret: sharedSecret}}));
};

/**
 * Request pairing with the host for PIN-less authentication.
 *
 * @param {string} clientName The human-readable name of the client.
 * @param {function(string, string):void} onDone, Callback to receive the
 *     client id and shared secret when they are available.
 */
remoting.ClientPluginAsync.prototype.requestPairing =
    function(clientName, onDone) {
  if (!this.hasFeature(remoting.ClientPlugin.Feature.PINLESS_AUTH)) {
    return;
  }
  this.onPairingComplete_ = onDone;
  this.plugin.postMessage(JSON.stringify(
      { method: 'requestPairing', data: { clientName: clientName } }));
};

/**
 * Send an extension message to the host.
 *
 * @param {string} type The message type.
 * @param {Object} message The message payload.
 */
remoting.ClientPluginAsync.prototype.sendClientMessage =
    function(type, message) {
  if (!this.hasFeature(remoting.ClientPlugin.Feature.EXTENSION_MESSAGE)) {
    return;
  }
  this.plugin.postMessage(JSON.stringify(
    { method: 'extensionMessage',
      data: { type: type, data: JSON.stringify(message) } }));

};

/**
 * If we haven't yet received a "hello" message from the plugin, change its
 * size so that the user can confirm it if click-to-play is enabled, or can
 * see the "this plugin is disabled" message if it is actually disabled.
 * @private
 */
remoting.ClientPluginAsync.prototype.showPluginForClickToPlay_ = function() {
  if (!this.helloReceived_) {
    var width = 200;
    var height = 200;
    this.plugin.width = width;
    this.plugin.height = height;
    // Center the plugin just underneath the "Connnecting..." dialog.
    var parentNode = this.plugin.parentNode;
    var dialog = document.getElementById('client-dialog');
    var dialogRect = dialog.getBoundingClientRect();
    parentNode.style.top = (dialogRect.bottom + 16) + 'px';
    parentNode.style.left = (window.innerWidth - width) / 2 + 'px';
  }
};
