// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-internet-page' is the settings page containing internet
 * settings.
 *
 * Example:
 *
 *    <core-animated-pages>
 *      <settings-internet-page prefs='{{prefs}}'>
 *      </settings-internet-page>
 *      ... other pages ...
 *    </core-animated-pages>
 *
 * @group Chrome Settings Elements
 * @element settings-internet-page
 */
Polymer({
  is: 'settings-internet-page',

  properties: {
    /**
     * The current active route.
     */
    currentRoute: {
      type: Object,
      notify: true,
    },

    /**
     * The network GUID for the detail subpage.
     */
    detailGuid: {
      type: String,
    },

    /**
     * The network type for the known networks subpage.
     */
    knownNetworksType: {
      type: String,
    },
  },

  /**
   * @param {!{detail: !CrOnc.NetworkStateProperties}} event
   * @private
   */
  onShowDetail_: function(event) {
    this.detailGuid = event.detail.GUID;
    this.$.pages.setSubpageChain(['network-detail']);
  },

  /**
   * @param {!{detail: {type: string}}} event
   * @private
   */
  onShowKnownNetworks_: function(event) {
    this.knownNetworksType = event.detail.type;
    this.$.pages.setSubpageChain(['known-networks']);
  },

});
