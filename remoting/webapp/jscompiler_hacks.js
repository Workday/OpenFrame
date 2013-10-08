// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains various hacks needed to inform JSCompiler of various
// WebKit- and Chrome-specific properties and methods. It is used only with
// JSCompiler to verify the type-correctness of our code.

/** @type {HTMLElement} */
Document.prototype.activeElement;

/** @type {Array.<HTMLElement>} */
Document.prototype.all;

/** @type {function(string): void} */
Document.prototype.execCommand = function(command) {};

/** @return {void} Nothing. */
Document.prototype.webkitCancelFullScreen = function() {};

/** @type {boolean} */
Document.prototype.webkitIsFullScreen;

/** @type {boolean} */
Document.prototype.webkitHidden;

/** @type {number} */
Element.ALLOW_KEYBOARD_INPUT;

/** @param {number} flags
/** @return {void} Nothing. */
Element.prototype.webkitRequestFullScreen = function(flags) {};

/** @type {{getRandomValues: function((Uint16Array|Uint8Array)):void}} */
Window.prototype.crypto;

/** @constructor
    @extends {HTMLElement} */
var HTMLEmbedElement = function() { };

/** @type {number} */
HTMLEmbedElement.prototype.height;

/** @type {number} */
HTMLEmbedElement.prototype.width;

/** @type {Window} */
HTMLIFrameElement.prototype.contentWindow;

/** @constructor */
var MutationRecord = function() {};

/** @type {string} */
MutationRecord.prototype.attributeName;

/** @type {Element} */
MutationRecord.prototype.target;

/** @type {string} */
MutationRecord.prototype.type;

/** @constructor
    @param {function(Array.<MutationRecord>):void} callback */
var MutationObserver = function(callback) {};

/** @param {Element} element
    @param {Object} options */
MutationObserver.prototype.observe = function(element, options) {};

/** @type {Object} */
chrome.storage = {};

/** @type {chrome.Storage} */
chrome.storage.local;

/** @type {chrome.Storage} */
chrome.storage.sync;

/** @constructor */
chrome.Storage = function() {};

/**
 * @param {string|Array.<string>|Object.<string>} items
 * @param {function(Object.<string>):void} callback
 * @return {void}
 */
chrome.Storage.prototype.get = function(items, callback) {};

/**
 * @param {Object.<string>} items
 * @param {function():void=} opt_callback
 * @return {void}
 */
chrome.Storage.prototype.set = function(items, opt_callback) {};

/**
 * @param {string|Array.<string>} items
 * @param {function():void=} opt_callback
 * @return {void}
 */
chrome.Storage.prototype.remove = function(items, opt_callback) {};

/**
 * @param {function():void=} opt_callback
 * @return {void}
 */
chrome.Storage.prototype.clear = function(opt_callback) {};

/** @type {Object} */
chrome.app.runtime = {
  /** @type {chrome.Event} */
  onLaunched: null
};

/** @type {Object} */
chrome.app.window = {
  /**
   * @param {string} name
   * @param {Object} parameters
   */
  create: function(name, parameters) {}
};

/** @type {Object} */
chrome.identity = {
  /**
   * @param {Object.<string>} parameters
   * @param {function(string):void} callback
   */
  getAuthToken: function(parameters, callback) {},
  /**
   * @param {Object.<string>} parameters
   * @param {function(string):void} callback
   */
  launchWebAuthFlow: function(parameters, callback) {}
};

/** @constructor */
chrome.Event = function() {};

/** @param {function():void} callback */
chrome.Event.prototype.addListener = function(callback) {};

/** @param {function():void} callback */
chrome.Event.prototype.removeListener = function(callback) {};

/** @constructor */
chrome.extension.Port = function() {};

/** @type {chrome.Event} */
chrome.extension.Port.prototype.onMessage;

/** @type {chrome.Event} */
chrome.extension.Port.prototype.onDisconnect;

/**
 * @param {Object} message
 */
chrome.extension.Port.prototype.postMessage = function(message) {};

/** @type {Object} */
chrome.runtime = {
  /** @type {Object} */
  lastError: {
    /** @type {string} */
    message: ''
  },
  /** @return {{version: string, app: {background: Object}}} */
  getManifest: function() {}
};

/**
 * @type {?function(string):chrome.extension.Port}
 */
chrome.runtime.connectNative = function(name) {};

/** @type {Object} */
chrome.tabs;

/** @param {function(chrome.Tab):void} callback */
chrome.tabs.getCurrent = function(callback) {}

/** @constructor */
chrome.Tab = function() {
  /** @type {boolean} */
  this.pinned = false;
  /** @type {number} */
  this.windowId = 0;
};

/** @type {Object} */
chrome.windows;

/** @param {number} id
 *  @param {Object?} getInfo
 *  @param {function(chrome.Window):void} callback */
chrome.windows.get = function(id, getInfo, callback) {}

/** @constructor */
chrome.Window = function() {
  /** @type {string} */
  this.state = '';
  /** @type {string} */
  this.type = '';
};

/** @param {string} message*/
chrome.extension.sendMessage = function(message) {}

/** @type {chrome.Event} */
chrome.extension.onMessage;

/** @type {Object} */
chrome.permissions = {
  /**
   * @param {Object.<string>} permissions
   * @param {function(boolean):void} callback
   */
  contains: function(permissions, callback) {},
/**
 * @param {Object.<string>} permissions
 * @param {function(boolean):void} callback
 */
  request: function(permissions, callback) {}
};
