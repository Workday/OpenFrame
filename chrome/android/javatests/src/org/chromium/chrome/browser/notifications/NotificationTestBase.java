// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.notifications;

import static org.chromium.base.test.util.ScalableTimeout.scaleTimeout;

import android.app.Notification;

import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.preferences.website.ContentSetting;
import org.chromium.chrome.browser.preferences.website.PushNotificationInfo;
import org.chromium.chrome.test.ChromeActivityTestCaseBase;
import org.chromium.chrome.test.util.TestHttpServerClient;
import org.chromium.chrome.test.util.browser.notifications.MockNotificationManagerProxy;
import org.chromium.chrome.test.util.browser.notifications.MockNotificationManagerProxy.NotificationEntry;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;

import java.util.List;
import java.util.concurrent.TimeoutException;

/**
 * Instrumentation tests for the Notification UI Manager implementation on Android.
 *
 * Web Notifications are only supported on Android JellyBean and beyond.
 */
public class NotificationTestBase extends ChromeActivityTestCaseBase<ChromeActivity> {
    /** The maximum time to wait for a criteria to become valid. */
    private static final long MAX_TIME_TO_POLL_MS = scaleTimeout(6000);

    /** The polling interval to wait between checking for a satisfied criteria. */
    private static final long POLLING_INTERVAL_MS = 50;

    private MockNotificationManagerProxy mMockNotificationManager;

    protected NotificationTestBase() {
        super(ChromeActivity.class);
    }

    /**
     * Returns the origin of the HTTP server the test is being ran on.
     */
    protected static String getOrigin() {
        return TestHttpServerClient.getUrl("");
    }

    /**
     * Sets the permission to use Web Notifications for the test HTTP server's origin to |setting|.
     */
    protected void setNotificationContentSettingForCurrentOrigin(final ContentSetting setting)
            throws InterruptedException, TimeoutException {
        final String origin = getOrigin();

        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                // The notification content setting does not consider the embedder origin.
                PushNotificationInfo pushNotificationInfo =
                        new PushNotificationInfo(origin, "", false);
                pushNotificationInfo.setContentSetting(setting);
            }
        });

        String permission = runJavaScriptCodeInCurrentTab("Notification.permission");
        if (setting == ContentSetting.ALLOW) {
            assertEquals("\"granted\"", permission);
        } else if (setting == ContentSetting.BLOCK) {
            assertEquals("\"denied\"", permission);
        } else {
            assertEquals("\"default\"", permission);
        }
    }

    /**
     * Shows a notification with |title| and |options|, waits until it has been displayed and then
     * returns the Notification object to the caller. Requires that only a single notification is
     * being displayed in the notification manager.
     *
     * @param title Title of the Web Notification to show.
     * @param options Optional map of options to include when showing the notification.
     * @return The Android Notification object, as shown in the framework.
     */
    protected Notification showAndGetNotification(String title, String options) throws Exception {
        runJavaScriptCodeInCurrentTab("showNotification(\"" + title + "\", " + options + ");");
        return waitForNotification().notification;
    }

    /**
     * Waits until a notification has been displayed and then returns a NotificationEntry object to
     * the caller. Requires that only a single notification is displayed.
     *
     * @return The NotificationEntry object tracked by the MockNotificationManagerProxy.
     */
    protected NotificationEntry waitForNotification() throws Exception {
        assertTrue(waitForNotificationManagerMutation());
        List<NotificationEntry> notifications = getNotificationEntries();
        assertEquals(1, notifications.size());
        return notifications.get(0);
    }

    protected List<NotificationEntry> getNotificationEntries() {
        return mMockNotificationManager.getNotifications();
    }

    /**
     * Waits for a mutation to occur in the mocked notification manager. This indicates that Chrome
     * called into Android to notify or cancel a notification.
     *
     * @return Whether the wait was successful.
     */
    protected boolean waitForNotificationManagerMutation() throws Exception {
        return CriteriaHelper.pollForUIThreadCriteria(new Criteria() {
            @Override
            public boolean isSatisfied() {
                return mMockNotificationManager.getMutationCountAndDecrement() > 0;
            }
        }, MAX_TIME_TO_POLL_MS, POLLING_INTERVAL_MS);
    }

    @Override
    public void startMainActivity() throws InterruptedException {
        // The NotificationUIManager must be overriden prior to the browser process starting.
        mMockNotificationManager = new MockNotificationManagerProxy();
        NotificationUIManager.overrideNotificationManagerForTesting(mMockNotificationManager);

        startMainActivityFromLauncher();
    }

    @Override
    protected void tearDown() throws Exception {
        NotificationUIManager.overrideNotificationManagerForTesting(null);

        super.tearDown();
    }
}
