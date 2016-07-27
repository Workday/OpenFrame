// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-router' is a simple router for settings. Its responsibilities:
 *  - Update the URL when the routing state changes.
 *  - Initialize the routing state with the initial URL.
 *  - Process and validate all routing state changes.
 *
 * Example:
 *
 *    <settings-router current-route="{{currentRoute}}">
 *    </settings-router>
 *
 * @group Chrome Settings Elements
 * @element settings-router
 */
Polymer({
  is: 'settings-router',

  properties: {
    /**
     * The current active route. This is reflected to the URL. Updates to this
     * property should replace the whole object.
     *
     * currentRoute.page refers to top-level pages such as Basic and Advanced.
     *
     * currentRoute.section is only non-empty when the user is on a subpage. If
     * the user is on Basic, for instance, this is an empty string.
     *
     * currentRoute.subpage is an Array. The last element is the actual subpage
     * the user is on. The previous elements are the ancestor subpages. This
     * enables support for multiple paths to the same subpage. This is used by
     * both the Back button and the Breadcrumb to determine ancestor subpages.
     */
    currentRoute: {
      notify: true,
      observer: 'currentRouteChanged_',
      type: Object,
      value: function() {
        // Take the current URL, find a matching pre-defined route, and
        // initialize the currentRoute to that pre-defined route.
        for (var i = 0; i < this.routes_.length; ++i) {
          var route = this.routes_[i];
          if (route.url == window.location.pathname) {
            return {
              page: route.page,
              section: route.section,
              subpage: route.subpage,
            };
          }
        }

        // As a fallback return the default route.
        return this.routes_[0];
      },
    },

    /**
     * Page titles for the currently active route. Updated by the currentRoute
     * property observer.
     * @type {{pageTitle: string, subpageTitles: Array<string>}}
     */
    currentRouteTitles: {
      notify: true,
      type: Object,
      value: function() { return {}; },
    },
  },


 /**
  * @private
  * The 'url' property is not accessible to other elements.
  */
 routes_: [
    {
      url: '/',
      page: 'basic',
      section: '',
      subpage: [],
      subpageTitles: [],
    },
    {
      url: '/advanced',
      page: 'advanced',
      section: '',
      subpage: [],
      subpageTitles: [],
    },
    {
      url: '/fonts',
      page: 'basic',
      section: 'appearance',
      subpage: ['appearance-fonts'],
      subpageTitles: ['customizeFonts'],
    },
    {
      url: '/startup',
      page: 'basic',
      section: 'on-startup',
      subpage: ['startup-urls'],
      subpageTitles: ['onStartupSetPages'],
    },
    {
      url: '/searchEngines',
      page: 'basic',
      section: 'search',
      subpage: ['search-engines'],
      subpageTitles: ['searchEnginesPageTitle'],
    },
    {
      url: '/searchEngines/advanced',
      page: 'basic',
      section: 'search',
      subpage: ['search-engines', 'search-engines-advanced'],
      subpageTitles: ['searchEnginesPageTitle', 'advancedPageTitle'],
    },
    {
      url: '/syncSetup',
      page: 'basic',
      section: 'people',
      subpage: ['sync'],
      subpageTitles: ['syncPageTitle'],
    },
    {
      url: '/certificates',
      page: 'advanced',
      section: 'privacy',
      subpage: ['manage-certificates'],
      subpageTitles: ['manageCertificates'],
    },
    {
      url: '/content',
      page: 'advanced',
      section: 'privacy',
      subpage: ['site-settings'],
      subpageTitles: ['siteSettings'],
    },
    {
      url: '/clearBrowserData',
      page: 'advanced',
      section: 'privacy',
      subpage: ['clear-browsing-data'],
      subpageTitles: ['clearBrowsingData'],
    },
    {
      url: '/networkDetail',
      page: 'basic',
      section: 'internet',
      subpage: ['network-detail'],
      subpageTitles: ['internetDetailPageTitle'],
    },
    {
      url: '/knownNetworks',
      page: 'basic',
      section: 'internet',
      subpage: ['known-networks'],
      subpageTitles: ['internetKnownNetworksPageTitle'],
    },
    {
      url: '/languages',
      page: 'advanced',
      section: 'languages',
      subpage: ['manage-languages'],
      subpageTitles: ['manageLanguagesPageTitle'],
    },
    {
      url: '/languages/edit',
      page: 'advanced',
      section: 'languages',
      subpage: ['language-detail'],
      subpageTitles: ['manageLanguagesPageTitle'],
    },
<if expr="not is_macosx">
    {
      url: '/editDictionary',
      page: 'advanced',
      section: 'languages',
      subpage: ['edit-dictionary'],
      subpageTitles: ['editDictionaryPageTitle'],
    },
</if>
  ],

  /**
   * Sets up a history popstate observer.
   */
  created: function() {
    window.addEventListener('popstate', function(event) {
      if (event.state && event.state.page)
        this.currentRoute = event.state;
    }.bind(this));
  },

  /**
   * @private
   * Is called when another element modifies the route. This observer validates
   * the route change against the pre-defined list of routes, and updates the
   * URL appropriately.
   */
  currentRouteChanged_: function(newRoute, oldRoute) {
    for (var i = 0; i < this.routes_.length; ++i) {
      var route = this.routes_[i];
      if (route.page == newRoute.page && route.section == newRoute.section &&
          route.subpage.length == newRoute.subpage.length &&
          newRoute.subpage.every(function(value, index) {
            return value == route.subpage[index];
          })) {

        // Update the property containing the titles for the current route.
        this.currentRouteTitles = {
          pageTitle: loadTimeData.getString(route.page + 'PageTitle'),
          subpageTitles: route.subpageTitles.map(function(titleCode) {
            return loadTimeData.getString(titleCode);
          }),
        };

        // If we are restoring a state from history, don't push it again.
        if (newRoute.inHistory)
          return;

        // Mark routes persisted in history as already stored in history.
        var historicState = {
          inHistory: true,
          page: newRoute.page,
          section: newRoute.section,
          subpage: newRoute.subpage,
        };

        // Push the current route to the history state, so when the user
        // navigates with the browser back button, we can recall the route.
        if (oldRoute) {
          history.pushState(historicState, null, route.url);
        } else {
          // For the very first route (oldRoute will be undefined), we replace
          // the existing state instead of pushing a new one. This is to allow
          // the user to use the browser back button to exit Settings entirely.
          history.replaceState(historicState, null);
        }

        return;
      }
    }

    assertNotReached('Route not found: ' + JSON.stringify(newRoute));
  },
});
