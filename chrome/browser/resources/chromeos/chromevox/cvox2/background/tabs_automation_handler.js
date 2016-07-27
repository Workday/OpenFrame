// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Handles automation from a tabs automation node.
 */

goog.provide('TabsAutomationHandler');

goog.require('DesktopAutomationHandler');

goog.scope(function() {
var EventType = chrome.automation.EventType;
var RoleType = chrome.automation.RoleType;

/**
 * @param {!chrome.automation.AutomationNode} tabRoot
 * @constructor
 * @extends {DesktopAutomationHandler}
 */
TabsAutomationHandler = function(tabRoot) {
  DesktopAutomationHandler.call(this, tabRoot);

  if (tabRoot.role != RoleType.rootWebArea)
    throw new Error('Expected rootWebArea node but got ' + tabRoot.role);

  // When the root is focused, simulate what happens on a load complete.
  if (tabRoot.state.focused)
    this.onLoadComplete({target: tabRoot, type: EventType.loadComplete});
};

TabsAutomationHandler.prototype = {
  __proto__: DesktopAutomationHandler.prototype,

  /** @override */
  didHandleEvent_: function(evt) {
    evt.stopPropagation();
  },

  /** @override */
  onLoadComplete: function(evt) {
    ChromeVoxState.instance.refreshMode(evt.target.docUrl);
    var focused = evt.target.find({state: {focused: true}}) || evt.target;
    this.onFocus({target: focused, type: EventType.focus});
  }
};

});  // goog.scope
