// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// API invoked by the browser MediaRouterWebUIMessageHandler to communicate
// with this UI.
cr.define('media_router.ui', function() {
  'use strict';

  // The media-router-container element.
  var container = null;

  /**
   * Handles timeout of previous create route attempt.
   */
  function onNotifyRouteCreationTimeout() {
    container.onNotifyRouteCreationTimeout();
  }

  /**
   * Handles response of previous create route attempt.
   *
   * @param {string} sinkId The ID of the sink to which the Media Route was
   *     creating a route.
   * @param {?media_router.Route} route The newly create route to the sink
   *     if route creation succeeded; null otherwise
   */
  function onCreateRouteResponseReceived(sinkId, route) {
    container.onCreateRouteResponseReceived(sinkId, route);
  }

  /**
   * Sets the cast mode list.
   *
   * @param {!Array<!media_router.CastMode>} castModeList
   */
  function setCastModeList(castModeList) {
    container.castModeList = castModeList;
  }

  /**
   * Sets |container|.
   *
   * @param {!MediaRouterContainerElement} mediaRouterContainer
   */
  function setContainer(mediaRouterContainer) {
    container = mediaRouterContainer;
  }

  /**
   * Populates the WebUI with data obtained from Media Router.
   *
   * @param {deviceMissingUrl: string,
   *         sinks: !Array<!media_router.Sink>,
   *         routes: !Array<!media_router.Route>,
   *         castModes: !Array<!media_router.CastMode>,
   *         initialCastModeType: number} data
   * Parameters in data:
   *   deviceMissingUrl - url to be opened on "Device missing?" clicked.
   *   sinks - list of sinks to be displayed.
   *   routes - list of routes that are associated with the sinks.
   *   castModes - list of available cast modes.
   *   initialCastModeType - cast mode to show initially. Expected to be
   *       included in |castModes|.
   */
  function setInitialData(data) {
    container.deviceMissingUrl = data['deviceMissingUrl'];
    container.allSinks = data['sinks'];
    container.routeList = data['routes'];
    container.initializeCastModes(data['castModes'],
        data['initialCastModeType']);
  }

  /**
   * Sets current issue to |issue|, or clears the current issue if |issue| is
   * null.
   *
   * @param {?media_router.Issue} issue
   */
  function setIssue(issue) {
    container.issue = issue;
  }

  /**
   * Sets the list of currently active routes.
   *
   * @param {!Array<!media_router.Route>} routeList
   */
  function setRouteList(routeList) {
    container.routeList = routeList;
  }

  /**
   * Sets the list of discovered sinks.
   *
   * @param {!Array<!media_router.Sink>} sinkList
   */
  function setSinkList(sinkList) {
    container.allSinks = sinkList;
  }

  return {
    onNotifyRouteCreationTimeout: onNotifyRouteCreationTimeout,
    onCreateRouteResponseReceived: onCreateRouteResponseReceived,
    setCastModeList: setCastModeList,
    setContainer: setContainer,
    setInitialData: setInitialData,
    setIssue: setIssue,
    setRouteList: setRouteList,
    setSinkList: setSinkList,
  };
});

// API invoked by this UI to communicate with the browser WebUI message handler.
cr.define('media_router.browserApi', function() {
  'use strict';

  /**
   * Acts on the given issue.
   *
   * @param {string} issueId
   * @param {number} actionType Type of action that the user clicked.
   * @param {?number} helpPageId The numeric help center ID.
   */
  function actOnIssue(issueId, actionType, helpPageId) {
    chrome.send('actOnIssue', [{issueId: issueId, actionType: actionType,
                                helpPageId: helpPageId}]);
  }

  /**
   * Closes the dialog.
   */
  function closeDialog() {
    chrome.send('closeDialog');
  }

  /**
   * Closes the given route.
   *
   * @param {!media_router.Route} route
   */
  function closeRoute(route) {
    chrome.send('closeRoute', [{routeId: route.id}]);
  }

  /**
   * Reports the current number of sinks.
   *
   * @param {number} sinkCount
   */
  function reportSinkCount(sinkCount) {
    chrome.send('reportSinkCount', [sinkCount]);
  }

  /**
   * Requests data to initialize the WebUI with.
   * The data will be returned via media_router.ui.setInitialData.
   */
  function requestInitialData() {
    chrome.send('requestInitialData');
  }

  /**
   * Requests that a media route be started with the given sink.
   *
   * @param {string} sinkId The sink ID.
   * @param {number} selectedCastMode The value of the cast mode the user
   *   selected.
   */
  function requestRoute(sinkId, selectedCastMode) {
    chrome.send('requestRoute',
                [{sinkId: sinkId, selectedCastMode: selectedCastMode}]);
  }

  return {
    actOnIssue: actOnIssue,
    closeDialog: closeDialog,
    closeRoute: closeRoute,
    reportSinkCount: reportSinkCount,
    requestInitialData: requestInitialData,
    requestRoute: requestRoute,
  };
});
