// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.notifications;

import android.annotation.SuppressLint;
import android.app.Notification;
import android.content.Context;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.os.Build;
import android.test.suitebuilder.annotation.LargeTest;
import android.test.suitebuilder.annotation.MediumTest;
import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.base.annotations.SuppressFBWarnings;
import org.chromium.base.test.util.Feature;
import org.chromium.chrome.browser.preferences.website.ContentSetting;
import org.chromium.chrome.browser.util.UrlUtilities;
import org.chromium.chrome.browser.widget.RoundedIconGenerator;
import org.chromium.chrome.test.util.TestHttpServerClient;
import org.chromium.chrome.test.util.browser.notifications.MockNotificationManagerProxy.NotificationEntry;

import java.util.Arrays;
import java.util.List;

/**
 * Instrumentation tests for the Notification UI Manager implementation on Android.
 *
 * Web Notifications are only supported on Android JellyBean and beyond.
 */
// TODO(peter): remove @SuppressLint once crbug.com/501900 is fixed.
@SuppressLint("NewApi")
// TODO(peter): fix deprecation warnings crbug.com/528076
@SuppressWarnings("deprecation")
public class NotificationUIManagerTest extends NotificationTestBase {
    private static final String NOTIFICATION_TEST_PAGE =
            TestHttpServerClient.getUrl("chrome/test/data/notifications/android_test.html");

    /**
     * Verifies that the intended default properties of a notification will indeed be set on the
     * Notification object that will be send to Android.
     */
    @MediumTest
    @Feature({"Browser", "Notifications"})
    public void testDefaultNotificationProperties() throws Exception {
        loadUrl(NOTIFICATION_TEST_PAGE);
        setNotificationContentSettingForCurrentOrigin(ContentSetting.ALLOW);

        Notification notification = showAndGetNotification("MyNotification", "{ body: 'Hello' }");

        // Validate the contents of the notification.
        assertEquals("MyNotification", notification.extras.getString(Notification.EXTRA_TITLE));
        assertEquals("Hello", notification.extras.getString(Notification.EXTRA_TEXT));
        assertEquals(UrlUtilities.formatUrlForSecurityDisplay(getOrigin(), false /* showScheme */),
                notification.extras.getString(Notification.EXTRA_SUB_TEXT));

        // Verify that the ticker text contains the notification's title and body.
        String tickerText = notification.tickerText.toString();

        assertTrue(tickerText.contains("MyNotification"));
        assertTrue(tickerText.contains("Hello"));

        // Validate the appearance style of the notification. The EXTRA_TEMPLATE was inroduced
        // in Android Lollipop, we cannot verify this in earlier versions.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            assertEquals("android.app.Notification$BigTextStyle",
                    notification.extras.getString(Notification.EXTRA_TEMPLATE));
        }

        assertNotNull(notification.largeIcon);

        // Validate the notification's behavior.
        assertEquals(Notification.DEFAULT_ALL, notification.defaults);
        assertEquals(Notification.PRIORITY_DEFAULT, notification.priority);
    }

    /**
     * Verifies that notifications created with the "silent" flag do not inherit system defaults
     * in regards to their sound, vibration and light indicators.
     */
    @MediumTest
    @Feature({"Browser", "Notifications"})
    public void testNotificationSilentProperty() throws Exception {
        loadUrl(NOTIFICATION_TEST_PAGE);
        setNotificationContentSettingForCurrentOrigin(ContentSetting.ALLOW);

        Notification notification = showAndGetNotification("MyNotification", "{ silent: true }");

        // Zero indicates that no defaults should be inherited from the system.
        assertEquals(0, notification.defaults);
    }

    /**
     * Verifies that notifications which specify an icon will have that icon fetched, converted into
     * a Bitmap and included as the large icon in the notification.
     */
    @MediumTest
    @Feature({"Browser", "Notifications"})
    public void testShowNotificationWithIcon() throws Exception {
        loadUrl(NOTIFICATION_TEST_PAGE);
        setNotificationContentSettingForCurrentOrigin(ContentSetting.ALLOW);

        Notification notification = showAndGetNotification("MyNotification", "{icon: 'icon.png'}");

        assertEquals("MyNotification", notification.extras.getString(Notification.EXTRA_TITLE));
        assertNotNull(notification.largeIcon);

        // TODO(peter): Do some more sensible checking that |icon.png| could actually be loaded.
        // One option might be to give that icon a solid color and check for it in the Bitmap, but
        // I'm not certain how reliable that would be.
    }

    /**
     * Verifies that notifications which don't specify an icon will get an automatically generated
     * icon based on their origin. The size of these icons are dependent on the resolution of the
     * device the test is being ran on, so we create it again in order to compare.
     */
    @MediumTest
    @Feature({"Browser", "Notifications"})
    public void testShowNotificationWithoutIcon() throws Exception {
        loadUrl(NOTIFICATION_TEST_PAGE);
        setNotificationContentSettingForCurrentOrigin(ContentSetting.ALLOW);

        Notification notification = showAndGetNotification("NoIconNotification", "{}");

        assertEquals("NoIconNotification", notification.extras.getString(Notification.EXTRA_TITLE));
        assertNotNull(notification.largeIcon);

        NotificationUIManager manager = NotificationUIManager.getInstanceForTests();
        assertNotNull(manager);

        // Create a second rounded icon for the test's origin, and compare its dimensions against
        // those of the icon associated to the notification itself.
        RoundedIconGenerator generator = manager.mIconGenerator;
        assertNotNull(generator);

        Bitmap generatedIcon = generator.generateIconForUrl(getOrigin());
        assertNotNull(generatedIcon);

        assertEquals(generatedIcon.getWidth(), notification.largeIcon.getWidth());
        assertEquals(generatedIcon.getHeight(), notification.largeIcon.getHeight());
    }

    /**
     * Tests the three paths for ensuring that a notification will be shown with a normalized icon:
     *     (1) NULL bitmaps should have an auto-generated image.
     *     (2) Large bitmaps should be resized to the device's intended size.
     *     (3) Smaller bitmaps should be left alone.
     */
    @MediumTest
    @Feature({"Browser", "Notifications"})
    public void testEnsureNormalizedIconBehavior() throws Exception {
        loadUrl(NOTIFICATION_TEST_PAGE);
        setNotificationContentSettingForCurrentOrigin(ContentSetting.ALLOW);

        // Create a notification to ensure that the NotificationUIManager is initialized.
        showAndGetNotification("MyNotification", "{}");

        // Get the dimensions of the notification icon that will be presented to the user.
        Context appContext = getInstrumentation().getTargetContext().getApplicationContext();
        Resources resources = appContext.getResources();

        int largeIconWidthPx =
                resources.getDimensionPixelSize(android.R.dimen.notification_large_icon_width);
        int largeIconHeightPx =
                resources.getDimensionPixelSize(android.R.dimen.notification_large_icon_height);

        String origin = "https://example.com";

        NotificationUIManager manager = NotificationUIManager.getInstanceForTests();
        assertNotNull(manager);

        Bitmap fromNullIcon = manager.ensureNormalizedIcon(null, origin);
        assertNotNull(fromNullIcon);
        assertEquals(largeIconWidthPx, fromNullIcon.getWidth());
        assertEquals(largeIconHeightPx, fromNullIcon.getHeight());

        Bitmap largeIcon = Bitmap.createBitmap(largeIconWidthPx * 2, largeIconHeightPx * 2,
                                               Bitmap.Config.ALPHA_8);

        Bitmap fromLargeIcon = manager.ensureNormalizedIcon(largeIcon, origin);
        assertNotNull(fromLargeIcon);
        assertEquals(largeIconWidthPx, fromLargeIcon.getWidth());
        assertEquals(largeIconHeightPx, fromLargeIcon.getHeight());

        Bitmap smallIcon = Bitmap.createBitmap(largeIconWidthPx / 2, largeIconHeightPx / 2,
                                               Bitmap.Config.ALPHA_8);

        Bitmap fromSmallIcon = manager.ensureNormalizedIcon(smallIcon, origin);
        assertNotNull(fromSmallIcon);
        assertEquals(smallIcon, fromSmallIcon);
    }

    /*
     * Verifies that starting the PendingIntent stored as the notification's content intent will
     * start up the associated Service Worker, where the JavaScript code will close the notification
     * by calling event.notification.close().
     */
    @LargeTest
    @Feature({"Browser", "Notifications"})
    public void testNotificationContentIntentClosesNotification() throws Exception {
        loadUrl(NOTIFICATION_TEST_PAGE);
        setNotificationContentSettingForCurrentOrigin(ContentSetting.ALLOW);

        Notification notification = showAndGetNotification("MyNotification", "{}");

        // Sending the PendingIntent resembles activating the notification.
        assertNotNull(notification.contentIntent);
        notification.contentIntent.send();

        // The Service Worker will close the notification upon receiving the notificationclick
        // event. This will eventually bubble up to a call to cancel() in the NotificationManager.
        assertTrue(waitForNotificationManagerMutation());
        assertTrue(getNotificationEntries().isEmpty());
    }

    /**
     * Verifies that creating a notification with an associated "tag" will cause any previous
     * notification with the same tag to be dismissed prior to being shown.
     */
    @SuppressFBWarnings("DLS_DEAD_LOCAL_STORE")
    @MediumTest
    @Feature({"Browser", "Notifications"})
    public void testNotificationTagReplacement() throws Exception {
        loadUrl(NOTIFICATION_TEST_PAGE);
        setNotificationContentSettingForCurrentOrigin(ContentSetting.ALLOW);

        runJavaScriptCodeInCurrentTab("showNotification('MyNotification', {tag: 'myTag'});");
        assertTrue(waitForNotificationManagerMutation());
        List<NotificationEntry> notifications = getNotificationEntries();
        String tag = notifications.get(0).tag;
        int id = notifications.get(0).id;

        runJavaScriptCodeInCurrentTab("showNotification('SecondNotification', {tag: 'myTag'});");
        assertTrue(waitForNotificationManagerMutation());

        // Verify that the notification was successfully replaced.
        notifications = getNotificationEntries();
        assertEquals(1, notifications.size());
        assertEquals("SecondNotification",
                notifications.get(0).notification.extras.getString(Notification.EXTRA_TITLE));

        // Verify that for replaced notifications their tag was the same.
        assertEquals(tag, notifications.get(0).tag);

        // Verify that as always, the same integer is used, also for replaced notifications.
        assertEquals(id, notifications.get(0).id);
        assertEquals(NotificationUIManager.PLATFORM_ID, notifications.get(0).id);
    }

    /**
     * Verifies that multiple notifications without a tag can be opened and closed without
     * affecting eachother.
     */
    @LargeTest
    @Feature({"Browser", "Notifications"})
    public void testShowAndCloseMultipleNotifications() throws Exception {
        loadUrl(NOTIFICATION_TEST_PAGE);
        setNotificationContentSettingForCurrentOrigin(ContentSetting.ALLOW);

        // Open the first notification and verify it is displayed.
        runJavaScriptCodeInCurrentTab("showNotification('One');");
        assertTrue(waitForNotificationManagerMutation());
        List<NotificationEntry> notifications = getNotificationEntries();
        assertEquals(1, notifications.size());
        Notification notificationOne = notifications.get(0).notification;
        assertEquals("One", notificationOne.extras.getString(Notification.EXTRA_TITLE));

        // Open the second notification and verify it is displayed.
        runJavaScriptCodeInCurrentTab("showNotification('Two');");
        assertTrue(waitForNotificationManagerMutation());
        notifications = getNotificationEntries();
        assertEquals(2, notifications.size());
        Notification notificationTwo = notifications.get(1).notification;
        assertEquals("Two", notificationTwo.extras.getString(Notification.EXTRA_TITLE));

        // The same integer id is always used as it is not needed for uniqueness, we rely on the tag
        // for uniqueness when the replacement behavior is not needed.
        assertEquals(NotificationUIManager.PLATFORM_ID, notifications.get(0).id);
        assertEquals(NotificationUIManager.PLATFORM_ID, notifications.get(1).id);

        // As these notifications were not meant to replace eachother, they must not have the same
        // tag internally.
        assertFalse(notifications.get(0).tag.equals(notifications.get(1).tag));

        // Verify that the PendingIntent for content and delete is different for each notification.
        assertFalse(notificationOne.contentIntent.equals(notificationTwo.contentIntent));
        assertFalse(notificationOne.deleteIntent.equals(notificationTwo.deleteIntent));

        // Close the first notification and verify that only the second remains.
        // Sending the content intent resembles touching the notification. In response tho this the
        // notificationclick event is fired. The test service worker will close the notification
        // upon receiving the event.
        notificationOne.contentIntent.send();
        assertTrue(waitForNotificationManagerMutation());
        notifications = getNotificationEntries();
        assertEquals(1, notifications.size());
        assertEquals("Two",
                notifications.get(0).notification.extras.getString(Notification.EXTRA_TITLE));

        // Close the last notification and verify that none remain.
        notifications.get(0).notification.contentIntent.send();
        assertTrue(waitForNotificationManagerMutation());
        assertTrue(getNotificationEntries().isEmpty());
    }

    /**
     * Verifies that the getOriginFromTag method returns the origin for valid input, and null for
     * invalid input.
     */
    @SmallTest
    @Feature({"Browser", "Notifications"})
    public void testGetOriginFromTag() throws Exception {
        // The common case.
        assertEquals("https://example.com", NotificationUIManager.getOriginFromTag(
                "NotificationUIManager;https://example.com;42"));

        // An tag that includes the separator. Probably a bit unusual, but valid.
        assertEquals("https://example.com", NotificationUIManager.getOriginFromTag(
                "NotificationUIManager;https://example.com;this;tag;contains;the;separator"));

        // Some invalid input.
        assertNull(NotificationUIManager.getOriginFromTag("SystemDownloadNotifier"));
        assertNull(NotificationUIManager.getOriginFromTag(null));
        assertNull(NotificationUIManager.getOriginFromTag(""));
        assertNull(NotificationUIManager.getOriginFromTag(";"));
        assertNull(NotificationUIManager.getOriginFromTag(";;;;;;;"));
        assertNull(NotificationUIManager.getOriginFromTag(
                "SystemDownloadNotifier;NotificationUIManager;42"));
        assertNull(NotificationUIManager.getOriginFromTag(
                "SystemDownloadNotifier;https://example.com;42"));
        assertNull(NotificationUIManager.getOriginFromTag(
                "NotificationUIManager;SystemDownloadNotifier;42"));
    }

    /**
     * Verifies that the makeDefaults method returns the generated notification defaults.
     */
    @SmallTest
    @Feature({"Browser", "Notifications"})
    public void testMakeDefaults() throws Exception {
        // 0 should be returned if silent is true and vibration's length is 0.
        assertEquals(0, NotificationUIManager.makeDefaults(0, true));

        // Notification.DEFAULT_ALL should be returned if silent is false and
        // vibration's length is 0.
        assertEquals(Notification.DEFAULT_ALL,
                NotificationUIManager.makeDefaults(0, false));

        // Notification.DEFAULT_ALL & ~Notification.DEFAULT_VIBRATE should be returned
        // if silent is false and vibration's length is greater than 0.
        assertEquals(Notification.DEFAULT_ALL & ~Notification.DEFAULT_VIBRATE,
                NotificationUIManager.makeDefaults(10, false));
    }

    /**
     * Verifies that the makeVibrationPattern method returns vibration pattern used
     * in Android notification.
     */
    @SmallTest
    @Feature({"Browser", "Notifications"})
    public void testMakeVibrationPattern() throws Exception {
        assertTrue(Arrays.equals(new long[] {0, 100, 200, 300},
                NotificationUIManager.makeVibrationPattern(new int[] {100, 200, 300})));
    }
}
