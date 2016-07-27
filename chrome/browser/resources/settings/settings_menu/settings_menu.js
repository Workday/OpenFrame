// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-menu' shows a menu with a hardcoded set of pages and subpages.
 *
 * Example:
 *
 *     <settings-menu selected-page-id="{{selectedPageId}}">
 *     </settings-menu>
 *
 * @group Chrome Settings Elements
 * @element settings-menu
 */
Polymer({
  is: 'settings-menu',

  properties: {
    /**
     * The current active route.
     */
    currentRoute: {
      type: Object,
      notify: true,
      observer: 'currentRouteChanged_',
    },
  },

  /** @private */
  currentRouteChanged_: function() {
    var submenu = this.shadowRoot.querySelector(
        'paper-submenu[data-page="' + this.currentRoute.page + '"]');
    if (submenu)
      submenu.opened = true;
  },

  /** @private */
  openPage_: function(event) {
    var submenuRoute = event.currentTarget.dataset.page;
    if (submenuRoute) {
      this.currentRoute = {
        page: submenuRoute,
        section: '',
        subpage: [],
      };
    }
  }
});
