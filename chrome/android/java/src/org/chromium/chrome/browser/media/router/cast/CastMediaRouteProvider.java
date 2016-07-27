// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.media.router.cast;

import android.content.Context;
import android.os.Handler;
import android.support.v7.media.MediaRouteSelector;
import android.support.v7.media.MediaRouter;
import android.support.v7.media.MediaRouter.RouteInfo;

import org.chromium.chrome.browser.media.router.ChromeMediaRouter;
import org.chromium.chrome.browser.media.router.DiscoveryDelegate;
import org.chromium.chrome.browser.media.router.MediaRoute;
import org.chromium.chrome.browser.media.router.MediaRouteManager;
import org.chromium.chrome.browser.media.router.MediaRouteProvider;
import org.chromium.chrome.browser.media.router.RouteController;
import org.chromium.chrome.browser.media.router.RouteDelegate;

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import javax.annotation.Nullable;

/**
 * A {@link MediaRouteProvider} implementation for Cast devices and applications.
 */
public class CastMediaRouteProvider
        implements MediaRouteProvider, DiscoveryDelegate, RouteDelegate {

    private static final String AUTO_JOIN_PRESENTATION_ID = "auto-join";
    private static final String PRESENTATION_ID_SESSION_ID_PREFIX = "cast-session_";

    private final Context mApplicationContext;
    private final MediaRouter mAndroidMediaRouter;
    private final MediaRouteManager mManager;
    private final Map<String, DiscoveryCallback> mDiscoveryCallbacks =
            new HashMap<String, DiscoveryCallback>();
    private final Map<String, MediaRoute> mRoutes = new HashMap<String, MediaRoute>();
    private ClientRecord mLastRemovedRouteRecord;
    private final List<ClientRecord> mClientRecords = new ArrayList<ClientRecord>();

    // There can be only one Cast session at the same time on Android.
    private SessionRecord mSession;
    private CreateRouteRequest mPendingCreateRouteRequest;
    private Handler mHandler = new Handler();

    private static class OnSinksReceivedRunnable implements Runnable {

        private final WeakReference<MediaRouteManager> mRouteManager;
        private final MediaRouteProvider mRouteProvider;
        private final String mSourceId;
        private final List<MediaSink> mSinks;

        OnSinksReceivedRunnable(MediaRouteManager manager, MediaRouteProvider routeProvider,
                String sourceId, List<MediaSink> sinks) {
            mRouteManager = new WeakReference<MediaRouteManager>(manager);
            mRouteProvider = routeProvider;
            mSourceId = sourceId;
            mSinks = sinks;
        }

        @Override
        public void run() {
            MediaRouteManager manager = mRouteManager.get();
            if (manager != null) manager.onSinksReceived(mSourceId, mRouteProvider, mSinks);
        }
    };

    @Override
    public void onSinksReceived(String sourceId, List<MediaSink> sinks) {
        mHandler.post(new OnSinksReceivedRunnable(mManager, this, sourceId, sinks));
    }

    @Override
    public void onRouteCreated(int requestId, MediaRoute route, RouteController routeController) {
        String routeId = route.id;

        MediaSource source = MediaSource.from(route.sourceId);
        final String clientId = source.getClientId();
        if (clientId != null && getClientRecordByClientId(clientId) == null) {
            mClientRecords.add(new ClientRecord(
                    routeId,
                    clientId,
                    source.getApplicationId(),
                    source.getAutoJoinPolicy(),
                    routeController.getOrigin(),
                    routeController.getTabId()));
        }

        if (mSession == null) {
            mSession = new SessionRecord(route.sinkId, (CastRouteController) routeController);
        }
        mSession.routeIds.add(routeId);

        if (clientId != null && !mSession.clientIds.contains(clientId)) {
            mSession.clientIds.add(clientId);
        }

        mRoutes.put(routeId, route);

        mManager.onRouteCreated(routeId, route.sinkId, requestId, this, true);
    }

    @Override
    public void onRouteRequestError(String message, int requestId) {
        mManager.onRouteRequestError(message, requestId);
    }

    @Override
    public void onRouteClosed(String routeId) {
        mLastRemovedRouteRecord = getClientRecordByRouteId(routeId);
        mClientRecords.remove(mLastRemovedRouteRecord);

        mManager.onRouteClosed(routeId);
        if (mSession != null) {
            for (String sessionRouteId : mSession.routeIds) {
                if (sessionRouteId.equals(routeId)) continue;

                mManager.onRouteClosed(routeId);
            }
        }

        mSession = null;

        if (mPendingCreateRouteRequest != null) {
            mPendingCreateRouteRequest.start(mApplicationContext);
            mPendingCreateRouteRequest = null;
        } else if (mAndroidMediaRouter != null) {
            mAndroidMediaRouter.selectRoute(mAndroidMediaRouter.getDefaultRoute());
        }
    }

    @Override
    public void onMessageSentResult(boolean success, int callbackId) {
        mManager.onMessageSentResult(success, callbackId);
    }

    @Override
    public void onMessage(String routeId, String message) {
        mManager.onMessage(routeId, message);
    }

    /**
     * @param applicationContext The application context to use for this route provider.
     * @return Initialized {@link CastMediaRouteProvider} object or null if it's not supported.
     */
    @Nullable
    public static CastMediaRouteProvider create(
            Context applicationContext, MediaRouteManager manager) {
        assert applicationContext != null;
        MediaRouter androidMediaRouter =
                ChromeMediaRouter.getAndroidMediaRouter(applicationContext);
        if (androidMediaRouter == null) return null;

        return new CastMediaRouteProvider(applicationContext, androidMediaRouter, manager);
    }

    @Override
    public boolean supportsSource(String sourceId) {
        return MediaSource.from(sourceId) != null;
    }

    @Override
    public void startObservingMediaSinks(String sourceId) {
        if (mAndroidMediaRouter == null) return;

        MediaSource source = MediaSource.from(sourceId);
        if (source == null) return;

        // If the source is a Cast source but invalid, report no sinks available.
        MediaRouteSelector routeSelector;
        try {
            routeSelector = source.buildRouteSelector();
        } catch (IllegalArgumentException e) {
            // If the application invalid, report no devices available.
            onSinksReceived(sourceId, new ArrayList<MediaSink>());
            return;
        }

        String applicationId = source.getApplicationId();
        DiscoveryCallback callback = mDiscoveryCallbacks.get(applicationId);
        if (callback != null) {
            callback.addSourceUrn(sourceId);
            return;
        }

        List<MediaSink> knownSinks = new ArrayList<MediaSink>();
        for (RouteInfo route : mAndroidMediaRouter.getRoutes()) {
            if (route.matchesSelector(routeSelector)) {
                knownSinks.add(MediaSink.fromRoute(route));
            }
        }

        callback = new DiscoveryCallback(sourceId, knownSinks, this);
        mAndroidMediaRouter.addCallback(
                routeSelector,
                callback,
                MediaRouter.CALLBACK_FLAG_REQUEST_DISCOVERY);
        mDiscoveryCallbacks.put(applicationId, callback);
    }

    @Override
    public void stopObservingMediaSinks(String sourceId) {
        if (mAndroidMediaRouter == null) return;

        MediaSource source = MediaSource.from(sourceId);
        if (source == null) return;

        String applicationId = source.getApplicationId();
        DiscoveryCallback callback = mDiscoveryCallbacks.get(applicationId);
        if (callback == null) return;

        callback.removeSourceUrn(sourceId);

        if (callback.isEmpty()) {
            mAndroidMediaRouter.removeCallback(callback);
            mDiscoveryCallbacks.remove(applicationId);
        }
    }

    @Override
    public void createRoute(String sourceId, String sinkId, String presentationId, String origin,
            int tabId, int nativeRequestId) {
        if (mAndroidMediaRouter == null) {
            mManager.onRouteRequestError("Not supported", nativeRequestId);
            return;
        }

        MediaSink sink = MediaSink.fromSinkId(sinkId, mAndroidMediaRouter);
        if (sink == null) {
            mManager.onRouteRequestError("No sink", nativeRequestId);
            return;
        }

        MediaSource source = MediaSource.from(sourceId);
        if (source == null) {
            mManager.onRouteRequestError("Unsupported presentation URL", nativeRequestId);
            return;
        }

        CreateRouteRequest createRouteRequest = new CreateRouteRequest(
                source, sink, presentationId, origin, tabId, nativeRequestId, this);

        // TODO(avayvod): Implement ReceiverAction.CAST, https://crbug.com/561470.

        // Since we only have one session, close it before starting a new one.
        if (mSession != null && !mSession.isStopping) {
            mPendingCreateRouteRequest = createRouteRequest;
            mSession.isStopping = true;
            mSession.session.close();
            return;
        }

        createRouteRequest.start(mApplicationContext);
    }

    @Override
    public void joinRoute(String sourceId, String presentationId, String origin, int tabId,
            int nativeRequestId) {
        if (mSession == null) {
            mManager.onRouteRequestError("No presentation", nativeRequestId);
            return;
        }

        MediaSource source = MediaSource.from(sourceId);
        if (source == null || source.getClientId() == null) {
            mManager.onRouteRequestError("Unsupported presentation URL", nativeRequestId);
            return;
        }

        // TODO(avayvod): Implement _receiver-action route for ReceiverAction messages,
        // https://crbug.com/561470.

        if (!canJoinExistingSession(presentationId, origin, tabId, source)) {
            mManager.onRouteRequestError("No matching route", nativeRequestId);
            return;
        }

        MediaRoute route = new MediaRoute(mSession.session.getSinkId(), sourceId, presentationId);
        mRoutes.put(route.id, route);

        this.onRouteCreated(nativeRequestId, route, mSession.session);
    }

    @Override
    public void closeRoute(String routeId) {
        MediaRoute route = mRoutes.get(routeId);

        if (route == null) {
            onRouteClosed(routeId);
            return;
        }

        if (mSession == null || !mSession.routeIds.contains(routeId)) {
            mRoutes.remove(routeId);

            onRouteClosed(routeId);
            return;
        }

        // TODO(avayvod): Implement ReceiverAction.STOP, https://crbug.com/561470.

        if (mSession.isStopping) return;

        mSession.isStopping = true;
        mSession.session.close();
    }

    @Override
    public void detachRoute(String routeId) {
        mRoutes.remove(routeId);
        if (mSession != null) mSession.routeIds.remove(routeId);

        for (int i = mClientRecords.size() - 1; i >= 0; --i) {
            ClientRecord client = mClientRecords.get(i);
            if (client.routeId.equals(routeId)) mClientRecords.remove(i);
            if (mSession != null) mSession.clientIds.remove(client.clientId);
        }
    }

    @Override
    public void sendStringMessage(String routeId, String message, int nativeCallbackId) {
        if (mSession == null || !mSession.routeIds.contains(routeId)) {
            mManager.onMessageSentResult(false, nativeCallbackId);
            return;
        }

        mSession.session.sendStringMessage(message, nativeCallbackId);
    }

    @Override
    public void sendBinaryMessage(String routeId, byte[] data, int nativeCallbackId) {
        // TODO(crbug.com/524128): Cast API does not support sending binary message
        // to receiver application. Binary data may be converted to String and send as
        // an app_message within it's own message namespace, using the string version.
        // Sending failure in the result callback for now.
        mManager.onMessageSentResult(false, nativeCallbackId);
    }

    private CastMediaRouteProvider(
            Context applicationContext, MediaRouter androidMediaRouter, MediaRouteManager manager) {
        mApplicationContext = applicationContext;
        mAndroidMediaRouter = androidMediaRouter;
        mManager = manager;
    }

    @Nullable
    private boolean canAutoJoin(MediaSource source, String origin, int tabId) {
        MediaSource currentSource = MediaSource.from(mSession.session.getSourceId());
        if (!currentSource.getApplicationId().equals(source.getApplicationId())) return false;

        ClientRecord client = null;
        if (!mSession.clientIds.isEmpty()) {
            String clientId = mSession.clientIds.iterator().next();
            client = getClientRecordByClientId(clientId);
        } else if (mLastRemovedRouteRecord != null) {
            client = mLastRemovedRouteRecord;
            return origin.equals(client.origin) && tabId == client.tabId;
        }

        if (client == null) return false;

        if (source.getAutoJoinPolicy().equals(MediaSource.AUTOJOIN_PAGE_SCOPED)) {
            return false;
        } else if (source.getAutoJoinPolicy().equals(MediaSource.AUTOJOIN_ORIGIN_SCOPED)) {
            return origin.equals(client.origin);
        } else if (source.getAutoJoinPolicy().equals(MediaSource.AUTOJOIN_TAB_AND_ORIGIN_SCOPED)) {
            return origin.equals(client.origin) && tabId == client.tabId;
        }

        return false;
    }

    private boolean canJoinExistingSession(String presentationId, String origin, int tabId,
            MediaSource source) {
        if (AUTO_JOIN_PRESENTATION_ID.equals(presentationId)) {
            return canAutoJoin(source, origin, tabId);
        } else if (presentationId.startsWith(PRESENTATION_ID_SESSION_ID_PREFIX)) {
            String sessionId = presentationId.substring(PRESENTATION_ID_SESSION_ID_PREFIX.length());

            if (mSession.session.getSessionId().equals(sessionId)) return true;
        } else {
            for (String routeId : mSession.routeIds) {
                MediaRoute route = mRoutes.get(routeId);
                if (route != null && route.presentationId.equals(presentationId)) return true;
            }
        }
        return false;
    }

    @Nullable
    private ClientRecord getClientRecordByClientId(String clientId) {
        for (ClientRecord record : mClientRecords) {
            if (record.clientId.equals(clientId)) return record;
        }
        return null;
    }

    @Nullable
    private ClientRecord getClientRecordByRouteId(String routeId) {
        for (ClientRecord record : mClientRecords) {
            if (record.routeId.equals(routeId)) return record;
        }
        return null;
    }
}
