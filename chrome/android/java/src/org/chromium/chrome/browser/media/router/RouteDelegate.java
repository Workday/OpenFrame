// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.media.router;

/**
 * The interface to get notified about route creation success or failure.
 */
public interface RouteDelegate {
    /**
     * Called when the route is created successfully.
     * @param requestId The id of the request to create the route.
     * @param route The route that was created.
     * @param routeController the route controller the route was created with.
     */
    void onRouteCreated(int requestId, MediaRoute route, RouteController routeController);

    /**
     * Called when the route creation failed.
     * @param message The error message.
     * @param requestId The id of the request to create the route.
     */
    void onRouteRequestError(String message, int requestId);

    /**
     * Called when the route is closed.
     * @param routeId the id of the closed route.
     */
    void onRouteClosed(String routeId);

    /**
     * Called when sending a message to the route has finished.
     * @param success if the message was successfully sent or not.
     * @param callbackId The id of the callback to process the result tracked by the native side.
     */
    void onMessageSentResult(boolean success, int callbackId);

    /**
     * Called when the route receives a message.
     * @param routeId The id of the route.
     * @param message The message received.
     */
    void onMessage(String routeId, String message);
}
