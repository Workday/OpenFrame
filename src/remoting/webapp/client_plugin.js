// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @suppress {duplicate} */
var remoting = remoting || {};

/**
 * Interface used for ClientPlugin objects.
 * @interface
 */
remoting.ClientPlugin = function() {
};

/** @type {number} Desktop width */
remoting.ClientPlugin.prototype.desktopWidth;
/** @type {number} Desktop height */
remoting.ClientPlugin.prototype.desktopHeight;
/** @type {number} Desktop x DPI */
remoting.ClientPlugin.prototype.desktopXDpi;
/** @type {number} Desktop y DPI */
remoting.ClientPlugin.prototype.desktopYDpi;

/** @type {function(string): void} Outgoing signaling message callback. */
remoting.ClientPlugin.prototype.onOutgoingIqHandler;
/** @type {function(string): void} Debug messages callback. */
remoting.ClientPlugin.prototype.onDebugMessageHandler;
/** @type {function(number, number): void} State change callback. */
remoting.ClientPlugin.prototype.onConnectionStatusUpdateHandler;
/** @type {function(boolean): void} Connection ready state callback. */
remoting.ClientPlugin.prototype.onConnectionReadyHandler;
/** @type {function(): void} Desktop size change callback. */
remoting.ClientPlugin.prototype.onDesktopSizeUpdateHandler;
/** @type {function(!Array.<string>): void} Capabilities negotiated callback. */
remoting.ClientPlugin.prototype.onSetCapabilitiesHandler;
/** @type {function(boolean): void} Request a PIN from the user. */
remoting.ClientPlugin.prototype.fetchPinHandler;

/**
 * Initializes the plugin asynchronously and calls specified function
 * when done.
 *
 * @param {function(boolean): void} onDone Function to be called when
 * the plugin is initialized. Parameter is set to true when the plugin
 * is loaded successfully.
 */
remoting.ClientPlugin.prototype.initialize = function(onDone) {};

/**
 * @return {boolean} True if the plugin and web-app versions are compatible.
 */
remoting.ClientPlugin.prototype.isSupportedVersion = function() {};

/**
 * Set of features for which hasFeature() can be used to test.
 *
 * @enum {string}
 */
remoting.ClientPlugin.Feature = {
  INJECT_KEY_EVENT: 'injectKeyEvent',
  NOTIFY_CLIENT_DIMENSIONS: 'notifyClientDimensions',
  NOTIFY_CLIENT_RESOLUTION: 'notifyClientResolution',
  ASYNC_PIN: 'asyncPin',
  PAUSE_VIDEO: 'pauseVideo',
  PAUSE_AUDIO: 'pauseAudio',
  REMAP_KEY: 'remapKey',
  SEND_CLIPBOARD_ITEM: 'sendClipboardItem',
  THIRD_PARTY_AUTH: 'thirdPartyAuth',
  TRAP_KEY: 'trapKey',
  PINLESS_AUTH: 'pinlessAuth',
  EXTENSION_MESSAGE: 'extensionMessage'
};

/**
 * @param {remoting.ClientPlugin.Feature} feature The feature to test for.
 * @return {boolean} True if the plugin supports the named feature.
 */
remoting.ClientPlugin.prototype.hasFeature = function(feature) {};

/**
 * @return {HTMLEmbedElement} HTML element that corresponds to the plugin.
 */
remoting.ClientPlugin.prototype.element = function() {};

/**
 * Deletes the plugin.
 */
remoting.ClientPlugin.prototype.cleanup = function() {};

/**
 * Must be called for each incoming stanza received from the host.
 * @param {string} iq Incoming IQ stanza.
 */
remoting.ClientPlugin.prototype.onIncomingIq = function(iq) {};

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
remoting.ClientPlugin.prototype.connect = function(
    hostJid, hostPublicKey, localJid, sharedSecret,
    authenticationMethods, authenticationTag,
    clientPairingId, clientPairedSecret) {};

/**
 * Release all currently pressed keys.
 */
remoting.ClientPlugin.prototype.releaseAllKeys = function() {};

/**
 * Send a key event to the host.
 *
 * @param {number} usbKeycode The USB-style code of the key to inject.
 * @param {boolean} pressed True to inject a key press, False for a release.
 */
remoting.ClientPlugin.prototype.injectKeyEvent =
    function(usbKeycode, pressed) {};

/**
 * Remap one USB keycode to another in all subsequent key events.
 *
 * @param {number} fromKeycode The USB-style code of the key to remap.
 * @param {number} toKeycode The USB-style code to remap the key to.
 */
remoting.ClientPlugin.prototype.remapKey =
    function(fromKeycode, toKeycode) {};

/**
 * Enable/disable redirection of the specified key to the web-app.
 *
 * @param {number} keycode The USB-style code of the key.
 * @param {Boolean} trap True to enable trapping, False to disable.
 */
remoting.ClientPlugin.prototype.trapKey = function(keycode, trap) {};

/**
 * Returns an associative array with a set of stats for this connection.
 *
 * @return {remoting.ClientSession.PerfStats} The connection statistics.
 */
remoting.ClientPlugin.prototype.getPerfStats = function() {};

/**
 * Sends a clipboard item to the host.
 *
 * @param {string} mimeType The MIME type of the clipboard item.
 * @param {string} item The clipboard item.
 */
remoting.ClientPlugin.prototype.sendClipboardItem = function(mimeType, item) {};

/**
 * Notifies the host that the client has the specified size and pixel density.
 *
 * @param {number} width The available client width in DIPs.
 * @param {number} height The available client height in DIPs.
 * @param {number} device_scale The number of device pixels per DIP.
 */
remoting.ClientPlugin.prototype.notifyClientResolution =
    function(width, height, device_scale) {};

/**
 * Requests that the host pause or resume sending video updates.
 *
 * @param {boolean} pause True to suspend video updates, false otherwise.
 */
remoting.ClientPlugin.prototype.pauseVideo =
    function(pause) {};

/**
 * Requests that the host pause or resume sending audio updates.
 *
 * @param {boolean} pause True to suspend audio updates, false otherwise.
 */
remoting.ClientPlugin.prototype.pauseAudio =
    function(pause) {};

/**
 * Gives the client authenticator the PIN.
 *
 * @param {string} pin The PIN.
 */
remoting.ClientPlugin.prototype.onPinFetched = function(pin) {};

/**
 * Tells the plugin to ask for the PIN asynchronously.
 */
remoting.ClientPlugin.prototype.useAsyncPinDialog = function() {};

/**
 * Sets the third party authentication token and shared secret.
 *
 * @param {string} token The token received from the token URL.
 * @param {string} sharedSecret Shared secret received from the token URL.
 */
remoting.ClientPlugin.prototype.onThirdPartyTokenFetched =
    function(token, sharedSecret) {};

/**
 * Request pairing with the host for PIN-less authentication.
 *
 * @param {string} clientName The human-readable name of the client.
 * @param {function(string, string):void} onDone, Callback to receive the
 *     client id and shared secret when they are available.
 */
remoting.ClientPlugin.prototype.requestPairing = function(
    clientName, onDone) {};
