// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-on-startup-page' is a settings page.
 *
 * Example:
 *
 *    <neon-animated-pages>
 *      <settings-on-startup-page prefs="{{prefs}}">
 *      </settings-on-startup-page>
 *      ... other pages ...
 *    </neon-animated-pages>
 *
 * @group Chrome Settings Elements
 * @element settings-on-startup-page
 */
Polymer({
  is: 'settings-on-startup-page',

  properties: {
    /**
     * Preferences state.
     */
    prefs: {
      type: Object,
      notify: true,
    },

    /**
     * The current active route.
     */
    currentRoute: {
      type: Object,
      notify: true,
    },

    prefValues_: {
      readOnly: true,
      type: Object,
      value: {
        OPEN_NEW_TAB: 5,
        CONTINUE: 1,
        OPEN_SPECIFIC: 4,
      },
    }
  },

  /** @private */
  onSetPagesTap_: function() {
    this.$.pages.setSubpageChain(['startup-urls']);
  },
});
