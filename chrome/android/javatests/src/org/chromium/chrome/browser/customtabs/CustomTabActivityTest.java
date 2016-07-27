// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.customtabs;

import static org.chromium.base.test.util.Restriction.RESTRICTION_TYPE_NON_LOW_END_DEVICE;
import static org.chromium.base.test.util.Restriction.RESTRICTION_TYPE_PHONE;

import android.app.Activity;
import android.app.Application;
import android.app.Instrumentation;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.drawable.BitmapDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.IBinder;
import android.support.customtabs.CustomTabsCallback;
import android.support.customtabs.CustomTabsIntent;
import android.support.customtabs.ICustomTabsCallback;
import android.test.suitebuilder.annotation.SmallTest;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.ImageButton;

import org.chromium.base.ThreadUtils;
import org.chromium.base.library_loader.LibraryProcessType;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Restriction;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.ChromeTabbedActivity;
import org.chromium.chrome.browser.IntentHandler;
import org.chromium.chrome.browser.document.ChromeLauncherActivity;
import org.chromium.chrome.browser.document.DocumentActivity;
import org.chromium.chrome.browser.tab.EmptyTabObserver;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.toolbar.CustomTabToolbar;
import org.chromium.chrome.browser.util.ColorUtils;
import org.chromium.chrome.browser.util.FeatureUtilities;
import org.chromium.chrome.test.util.TestHttpServerClient;
import org.chromium.chrome.test.util.browser.contextmenu.ContextMenuUtils;
import org.chromium.content.browser.BrowserStartupController;
import org.chromium.content.browser.BrowserStartupController.StartupCallback;
import org.chromium.content.browser.test.util.CallbackHelper;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;
import org.chromium.content_public.browser.LoadUrlParams;

import java.util.ArrayList;
import java.util.concurrent.Callable;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Instrumentation tests for app menu, context menu, and toolbar of a {@link CustomTabActivity}.
 */
public class CustomTabActivityTest extends CustomTabActivityTestBase {

    /**
     * An empty {@link BroadcastReceiver} that exists only to make the PendingIntent to carry an
     * explicit intent. Otherwise the framework will not send it after {@link PendingIntent#send()}.
     */
    public static class DummyBroadcastReceiver extends BroadcastReceiver {
        // The url has to be copied from the instrumentation class, because a BroadcastReceiver is
        // deployed as a different package, and it cannot get access to data from the
        // instrumentation package.
        private static final String TEST_PAGE_COPY = TestHttpServerClient.getUrl(
                "chrome/test/data/android/google.html");

        @Override
        public void onReceive(Context context, Intent intent) {
            // Note: even if this assertion fails, the test might still pass, because
            // BroadcastReceiver is not treated as part of the instrumentation test.
            assertEquals(TEST_PAGE_COPY, intent.getDataString());
        }
    }

    private static final int MAX_MENU_CUSTOM_ITEMS = 5;
    private static final int NUM_CHROME_MENU_ITEMS = 3;
    private static final String
            TEST_ACTION = "org.chromium.chrome.browser.customtabs.TEST_PENDING_INTENT_SENT";
    private static final String TEST_PAGE = TestHttpServerClient.getUrl(
            "chrome/test/data/android/google.html");
    private static final String TEST_PAGE_2 = TestHttpServerClient.getUrl(
            "chrome/test/data/android/test.html");
    private static final String TEST_MENU_TITLE = "testMenuTitle";

    private CustomTabActivity mActivity;

    @Override
    protected void startActivityCompletely(Intent intent) {
        super.startActivityCompletely(intent);
        mActivity = getActivity();
    }

    /**
     * @see CustomTabsTestUtils#createMinimalCustomTabIntent(Context, String, IBinder).
     */
    private Intent createMinimalCustomTabIntent() {
        return CustomTabsTestUtils.createMinimalCustomTabIntent(
                getInstrumentation().getTargetContext(), TEST_PAGE, null);
    }

    /**
     * Add a bundle specifying a custom menu entry.
     * @param intent The intent to modify.
     * @return The pending intent associated with the menu entry.
     */
    private PendingIntent addMenuEntriesToIntent(Intent intent, int numEntries) {
        Intent menuIntent = new Intent();
        menuIntent.setClass(getInstrumentation().getContext(), DummyBroadcastReceiver.class);
        menuIntent.setAction(TEST_ACTION);
        PendingIntent pi = PendingIntent.getBroadcast(getInstrumentation().getTargetContext(), 0,
                menuIntent, 0);
        ArrayList<Bundle> menuItems = new ArrayList<Bundle>();
        for (int i = 0; i < numEntries; i++) {
            Bundle bundle = new Bundle();
            bundle.putString(CustomTabsIntent.KEY_MENU_ITEM_TITLE, TEST_MENU_TITLE);
            bundle.putParcelable(CustomTabsIntent.KEY_PENDING_INTENT, pi);
            menuItems.add(bundle);
        }
        intent.putParcelableArrayListExtra(CustomTabsIntent.EXTRA_MENU_ITEMS, menuItems);
        return pi;
    }

    private void addToolbarColorToIntent(Intent intent, int color) {
        intent.putExtra(CustomTabsIntent.EXTRA_TOOLBAR_COLOR, color);
    }

    /**
     * Adds an action button to the custom tab toolbar.
     * @return The {@link PendingIntent} that will be triggered when the action button is clicked.
     */
    private PendingIntent addActionButtonToIntent(Intent intent, Bitmap icon, String description) {
        Intent actionIntent = new Intent();
        actionIntent.setClass(getInstrumentation().getContext(), DummyBroadcastReceiver.class);
        actionIntent.setAction(TEST_ACTION);

        Bundle bundle = new Bundle();
        bundle.putParcelable(CustomTabsIntent.KEY_ICON, icon);
        bundle.putString(CustomTabsIntent.KEY_DESCRIPTION, description);
        PendingIntent pi = PendingIntent.getBroadcast(getInstrumentation().getTargetContext(), 0,
                actionIntent, 0);
        bundle.putParcelable(CustomTabsIntent.KEY_PENDING_INTENT, pi);

        intent.putExtra(CustomTabsIntent.EXTRA_ACTION_BUTTON_BUNDLE, bundle);
        return pi;
    }

    private void openAppMenuAndAssertMenuShown() throws InterruptedException {
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mActivity.onMenuOrKeyboardAction(R.id.show_menu, false);
            }
        });

        assertTrue("App menu was not shown", CriteriaHelper.pollForUIThreadCriteria(new Criteria() {
            @Override
            public boolean isSatisfied() {
                return mActivity.getAppMenuHandler().isAppMenuShowing();
            }
        }));
    }

    /**
     * Test the entries in the context menu shown when long clicking an image.
     */
    @SmallTest
    public void testContextMenuEntriesForImage() throws InterruptedException, TimeoutException {
        startCustomTabActivityWithIntent(createMinimalCustomTabIntent());

        final int expectedMenuSize = 9;
        Menu menu = ContextMenuUtils.openContextMenu(this, getActivity().getActivityTab(), "logo");
        assertEquals(expectedMenuSize, menu.size());

        assertNotNull(menu.findItem(R.id.contextmenu_copy_link_address));
        assertNotNull(menu.findItem(R.id.contextmenu_copy_email_address));
        assertNotNull(menu.findItem(R.id.contextmenu_copy_link_text));
        assertNotNull(menu.findItem(R.id.contextmenu_save_link_as));
        assertNotNull(menu.findItem(R.id.contextmenu_save_image));
        assertNotNull(menu.findItem(R.id.contextmenu_share_image));
        assertNotNull(menu.findItem(R.id.contextmenu_open_image));
        assertNotNull(menu.findItem(R.id.contextmenu_save_video));

        assertTrue(menu.findItem(R.id.contextmenu_save_image).isVisible());
        assertTrue(menu.findItem(R.id.contextmenu_share_image).isVisible());
        assertTrue(menu.findItem(R.id.contextmenu_open_image).isVisible());
        assertTrue(menu.findItem(R.id.contextmenu_search_by_image).isVisible());

        assertFalse(menu.findItem(R.id.contextmenu_copy_link_address).isVisible());
        assertFalse(menu.findItem(R.id.contextmenu_copy_email_address).isVisible());
        assertFalse(menu.findItem(R.id.contextmenu_copy_link_text).isVisible());
        assertFalse(menu.findItem(R.id.contextmenu_save_link_as).isVisible());
        assertFalse(menu.findItem(R.id.contextmenu_save_video).isVisible());
    }

    /**
     * Test the entries in the context menu shown when long clicking an link.
     */
    @SmallTest
    public void testContextMenuEntriesForLink() throws InterruptedException, TimeoutException {
        startCustomTabActivityWithIntent(createMinimalCustomTabIntent());

        final int expectedMenuSize = 9;
        Menu menu = ContextMenuUtils.openContextMenu(this, getActivity().getActivityTab(),
                "aboutLink");
        assertEquals(expectedMenuSize, menu.size());

        assertNotNull(menu.findItem(R.id.contextmenu_copy_link_address));
        assertNotNull(menu.findItem(R.id.contextmenu_copy_email_address));
        assertNotNull(menu.findItem(R.id.contextmenu_copy_link_text));
        assertNotNull(menu.findItem(R.id.contextmenu_save_link_as));
        assertNotNull(menu.findItem(R.id.contextmenu_save_image));
        assertNotNull(menu.findItem(R.id.contextmenu_share_image));
        assertNotNull(menu.findItem(R.id.contextmenu_open_image));
        assertNotNull(menu.findItem(R.id.contextmenu_save_video));

        assertTrue(menu.findItem(R.id.contextmenu_copy_link_address).isVisible());
        assertTrue(menu.findItem(R.id.contextmenu_copy_link_text).isVisible());
        assertTrue(menu.findItem(R.id.contextmenu_save_link_as).isVisible());

        assertFalse(menu.findItem(R.id.contextmenu_share_image).isVisible());
        assertFalse(menu.findItem(R.id.contextmenu_copy_email_address).isVisible());
        assertFalse(menu.findItem(R.id.contextmenu_save_image).isVisible());
        assertFalse(menu.findItem(R.id.contextmenu_open_image).isVisible());
        assertFalse(menu.findItem(R.id.contextmenu_search_by_image).isVisible());
        assertFalse(menu.findItem(R.id.contextmenu_save_video).isVisible());
    }

    /**
     * Test the entries in the app menu.
     */
    @SmallTest
    public void testCustomTabAppMenu() throws InterruptedException {
        Intent intent = createMinimalCustomTabIntent();
        int numMenuEntries = 1;
        addMenuEntriesToIntent(intent, numMenuEntries);
        startCustomTabActivityWithIntent(intent);

        openAppMenuAndAssertMenuShown();
        final int expectedMenuSize = numMenuEntries + NUM_CHROME_MENU_ITEMS;
        Menu menu = getActivity().getAppMenuHandler().getAppMenuForTest().getMenuForTest();
        assertNotNull("App menu is not initialized: ", menu);
        assertEquals(expectedMenuSize, menu.size());
        assertNotNull(menu.findItem(R.id.forward_menu_id));
        assertNotNull(menu.findItem(R.id.info_menu_id));
        assertNotNull(menu.findItem(R.id.reload_menu_id));
        assertNotNull(menu.findItem(R.id.find_in_page_id));
        assertNotNull(menu.findItem(R.id.open_in_chrome_id));
    }

    /**
     * Test that only up to 5 entries are added to the custom menu.
     */
    @SmallTest
    public void testCustomTabMaxMenuItems() throws InterruptedException {
        Intent intent = createMinimalCustomTabIntent();
        int numMenuEntries = 7;
        addMenuEntriesToIntent(intent, numMenuEntries);
        startCustomTabActivityWithIntent(intent);

        openAppMenuAndAssertMenuShown();
        int expectedMenuSize = MAX_MENU_CUSTOM_ITEMS + NUM_CHROME_MENU_ITEMS;
        Menu menu = getActivity().getAppMenuHandler().getAppMenuForTest().getMenuForTest();
        assertNotNull("App menu is not initialized: ", menu);
        assertEquals(expectedMenuSize, menu.size());
    }

    /**
     * Test whether the custom menu is correctly shown and clicking it sends the right
     * {@link PendingIntent}.
     */
    @SmallTest
    public void testCustomMenuEntry() throws InterruptedException {
        Intent intent = createMinimalCustomTabIntent();
        final PendingIntent pi = addMenuEntriesToIntent(intent, 1);
        startCustomTabActivityWithIntent(intent);

        final OnFinishedForTest onFinished = new OnFinishedForTest(pi);
        mActivity.getIntentDataProvider().setPendingIntentOnFinishedForTesting(onFinished);

        openAppMenuAndAssertMenuShown();
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                MenuItem item = mActivity.getAppMenuPropertiesDelegate().getMenuItemForTitle(
                        TEST_MENU_TITLE);
                assertNotNull(item);
                assertTrue(mActivity.onOptionsItemSelected(item));
            }
        });

        assertTrue("Pending Intent was not sent.", CriteriaHelper.pollForCriteria(new Criteria() {
            @Override
            public boolean isSatisfied() {
                return onFinished.isSent();
            }
        }));
    }

    /**
     * Test whether clicking "Open in Chrome" takes us to a chrome normal tab, loading the same url.
     */
    @SmallTest
    public void testOpenInChrome() throws InterruptedException {
        startCustomTabActivityWithIntent(createMinimalCustomTabIntent());

        boolean isDocumentMode = FeatureUtilities.isDocumentMode(
                getInstrumentation().getTargetContext());
        String activityName;
        if (isDocumentMode) {
            activityName = DocumentActivity.class.getName();
        } else {
            activityName = ChromeTabbedActivity.class.getName();
        }
        Instrumentation.ActivityMonitor monitor = getInstrumentation().addMonitor(activityName,
                null, false);

        openAppMenuAndAssertMenuShown();
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mActivity.onMenuOrKeyboardAction(R.id.open_in_chrome_id, false);
            }
        });

        final ChromeActivity chromeActivity = (ChromeActivity) monitor
                .waitForActivityWithTimeout(ACTIVITY_START_TIMEOUT_MS);
        assertNotNull("A normal chrome activity did not start.", chromeActivity);
        assertTrue("The normal tab was not initiated correctly.",
                CriteriaHelper.pollForUIThreadCriteria(new Criteria() {
                    @Override
                    public boolean isSatisfied() {
                        Tab tab = chromeActivity.getActivityTab();
                        return tab != null && tab.getUrl().equals(TEST_PAGE);
                    }
                }));
    }

    /**
     * Test whether the color of the toolbar is correctly customized. For L or later releases,
     * status bar color is also tested.
     */
    @SmallTest
    public void testToolbarColor() throws InterruptedException {
        Intent intent = createMinimalCustomTabIntent();
        final int expectedColor = Color.RED;
        addToolbarColorToIntent(intent, expectedColor);
        startCustomTabActivityWithIntent(intent);

        View toolbarView = getActivity().findViewById(R.id.toolbar);
        assertTrue("A custom tab toolbar is never shown", toolbarView instanceof CustomTabToolbar);
        CustomTabToolbar toolbar = (CustomTabToolbar) toolbarView;
        assertEquals(expectedColor, toolbar.getBackground().getColor());
        if (Build.VERSION.SDK_INT > Build.VERSION_CODES.LOLLIPOP) {
            assertEquals(ColorUtils.getDarkenedColorForStatusBar(expectedColor),
                    getActivity().getWindow().getStatusBarColor());
        }
    }

    /**
     * Test if an action button is shown with correct image and size, and clicking it sends the
     * correct {@link PendingIntent}.
     */
    @SmallTest
    public void testActionButton() throws InterruptedException {
        final int iconHeightDp = 48;
        final int iconWidthDp = 96;
        Resources testRes = getInstrumentation().getTargetContext().getResources();
        float density = testRes.getDisplayMetrics().density;
        Bitmap expectedIcon = Bitmap.createBitmap((int) (iconWidthDp * density),
                (int) (iconHeightDp * density), Bitmap.Config.ARGB_8888);

        Intent intent = createMinimalCustomTabIntent();
        final PendingIntent pi = addActionButtonToIntent(intent, expectedIcon, "Good test");
        startCustomTabActivityWithIntent(intent);

        final OnFinishedForTest onFinished = new OnFinishedForTest(pi);
        mActivity.getIntentDataProvider().setPendingIntentOnFinishedForTesting(onFinished);

        View toolbarView = getActivity().findViewById(R.id.toolbar);
        assertTrue("A custom tab toolbar is never shown", toolbarView instanceof CustomTabToolbar);
        CustomTabToolbar toolbar = (CustomTabToolbar) toolbarView;
        final ImageButton actionButton = toolbar.getCustomActionButtonForTest();

        assertNotNull(actionButton);
        assertNotNull(actionButton.getDrawable());
        assertTrue("Action button's background is not a BitmapDrawable.",
                actionButton.getDrawable() instanceof BitmapDrawable);

        assertTrue("Action button does not have the correct bitmap.",
                expectedIcon.sameAs(((BitmapDrawable) actionButton.getDrawable()).getBitmap()));

        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                actionButton.performClick();
            }
        });

        assertTrue("Pending Intent was not sent.", CriteriaHelper.pollForCriteria(new Criteria() {
            @Override
            public boolean isSatisfied() {
                return onFinished.isSent();
            }
        }));
    }

    /**
     * Test the case that the action button should not be shown, given a bitmap with unacceptable
     * height/width ratio.
     */
    @SmallTest
    public void testActionButtonBadRatio() throws InterruptedException {
        final int iconHeightDp = 20;
        final int iconWidthDp = 60;
        Resources testRes = getInstrumentation().getTargetContext().getResources();
        float density = testRes.getDisplayMetrics().density;
        Bitmap expectedIcon = Bitmap.createBitmap((int) (iconWidthDp * density),
                (int) (iconHeightDp * density), Bitmap.Config.ARGB_8888);

        Intent intent = createMinimalCustomTabIntent();
        addActionButtonToIntent(intent, expectedIcon, "Good test");
        startCustomTabActivityWithIntent(intent);

        View toolbarView = getActivity().findViewById(R.id.toolbar);
        assertTrue("A custom tab toolbar is never shown", toolbarView instanceof CustomTabToolbar);
        CustomTabToolbar toolbar = (CustomTabToolbar) toolbarView;
        final ImageButton actionButton = toolbar.getCustomActionButtonForTest();

        assertNotNull(actionButton);
        assertTrue("Action button should not be shown",
                View.VISIBLE != actionButton.getVisibility());

        CustomTabIntentDataProvider dataProvider = mActivity.getIntentDataProvider();
        assertNull(dataProvider.getActionButtonParams());
    }

    @SmallTest
    public void testLaunchWithSession() throws InterruptedException {
        IBinder session = warmUpAndLaunchUrlWithSession();
        assertEquals(mActivity.getIntentDataProvider().getSession(), session);
    }

    @SmallTest
    public void testLoadNewUrlWithSession() throws InterruptedException {
        final IBinder session = warmUpAndLaunchUrlWithSession();
        final Context context = getInstrumentation().getTargetContext();
        assertEquals(mActivity.getIntentDataProvider().getSession(), session);
        assertFalse("CustomTabContentHandler handled intent with wrong session",
                ThreadUtils.runOnUiThreadBlockingNoException(new Callable<Boolean>() {
                    @Override
                    public Boolean call() throws Exception {
                        return CustomTabActivity.handleInActiveContentIfNeeded(
                                CustomTabsTestUtils.createMinimalCustomTabIntent(context,
                                        TEST_PAGE_2,
                                        (new CustomTabsTestUtils.DummyCallback()).asBinder()));
                    }
                }));
        assertTrue(CriteriaHelper.pollForCriteria(new Criteria() {
                    @Override
                    public boolean isSatisfied() {
                        return mActivity.getActivityTab().getUrl().equals(TEST_PAGE);
                    }
        }));
        assertTrue("CustomTabContentHandler can't handle intent with same session",
                ThreadUtils.runOnUiThreadBlockingNoException(new Callable<Boolean>() {
                    @Override
                    public Boolean call() throws Exception {
                        return CustomTabActivity.handleInActiveContentIfNeeded(
                            CustomTabsTestUtils.createMinimalCustomTabIntent(context,
                                    TEST_PAGE_2, session));
                    }
                }));
        final Tab tab = mActivity.getActivityTab();
        final CallbackHelper pageLoadFinishedHelper = new CallbackHelper();
        tab.addObserver(new EmptyTabObserver() {
            @Override
            public void onPageLoadFinished(Tab tab) {
                pageLoadFinishedHelper.notifyCalled();
            }
        });
        try {
            pageLoadFinishedHelper.waitForCallback(0);
        } catch (TimeoutException e) {
            fail();
        }
        assertTrue(CriteriaHelper.pollForCriteria(new Criteria() {
                    @Override
                    public boolean isSatisfied() {
                        return mActivity.getActivityTab().getUrl().equals(TEST_PAGE_2);
                    }
        }));
    }

    @SmallTest
    public void testReferrerAddedAutomatically() throws InterruptedException {
        final IBinder session = warmUpAndLaunchUrlWithSession();
        assertEquals(mActivity.getIntentDataProvider().getSession(), session);
        final Context context = getInstrumentation().getTargetContext().getApplicationContext();
        CustomTabsConnection connection = CustomTabsConnection.getInstance((Application) context);
        String packageName = context.getPackageName();
        final String referrer =
                IntentHandler.constructValidReferrerForAuthority(packageName).getUrl();
        assertEquals(referrer, connection.getReferrerForSession(session).getUrl());

        final Tab tab = mActivity.getActivityTab();
        final CallbackHelper pageLoadFinishedHelper = new CallbackHelper();
        tab.addObserver(new EmptyTabObserver() {
            @Override
            public void onLoadUrl(Tab tab, LoadUrlParams params, int loadType) {
                assertEquals(referrer, params.getReferrer().getUrl());
            }

            @Override
            public void onPageLoadFinished(Tab tab) {
                pageLoadFinishedHelper.notifyCalled();
            }
        });
        assertTrue("CustomTabContentHandler can't handle intent with same session",
                ThreadUtils.runOnUiThreadBlockingNoException(new Callable<Boolean>() {
                    @Override
                    public Boolean call() throws Exception {
                        return CustomTabActivity.handleInActiveContentIfNeeded(
                            CustomTabsTestUtils.createMinimalCustomTabIntent(context,
                                    TEST_PAGE_2, session));
                    }
                }));
        try {
            pageLoadFinishedHelper.waitForCallback(0);
        } catch (TimeoutException e) {
            fail();
        }
    }

    /**
     * Tests that the navigation callbacks are sent.
     */
    @SmallTest
    public void testCallbacksAreSent() {
        final ArrayList<Integer> navigationEvents = new ArrayList<>();
        ICustomTabsCallback cb = new CustomTabsTestUtils.DummyCallback() {
            @Override
            public void onNavigationEvent(int navigationEvent, Bundle extras) {
                navigationEvents.add(navigationEvent);
            }
        };
        try {
            warmUpAndLaunchUrlWithSession(cb);
            assertTrue(navigationEvents.contains(CustomTabsCallback.NAVIGATION_STARTED));
            assertTrue(navigationEvents.contains(CustomTabsCallback.NAVIGATION_FINISHED));
            assertFalse(navigationEvents.contains(CustomTabsCallback.NAVIGATION_FAILED));
            assertFalse(navigationEvents.contains(CustomTabsCallback.NAVIGATION_ABORTED));
        } catch (InterruptedException e) {
            fail();
        }
    }

    /**
     * Tests that when we use a pre-created renderer, the page loaded is the
     * only one in the navigation history.
     */
    @SmallTest
    @Restriction(RESTRICTION_TYPE_NON_LOW_END_DEVICE)
    public void testPrecreatedRenderer() {
        CustomTabsConnection connection = warmUpAndWait();
        ICustomTabsCallback cb = new CustomTabsTestUtils.DummyCallback();
        assertTrue(connection.newSession(cb));
        Bundle extras = new Bundle();
        extras.putBoolean(CustomTabsConnection.NO_PRERENDERING_KEY, true);
        assertTrue(connection.mayLaunchUrl(cb, Uri.parse(TEST_PAGE), extras, null));
        Context context = getInstrumentation().getTargetContext().getApplicationContext();
        try {
            startCustomTabActivityWithIntent(CustomTabsTestUtils.createMinimalCustomTabIntent(
                    context, TEST_PAGE, cb.asBinder()));
        } catch (InterruptedException e) {
            fail();
        }
        Tab tab = getActivity().getActivityTab();
        assertEquals(TEST_PAGE, tab.getUrl());
        assertFalse(tab.canGoBack());
    }

    /** Tests that calling mayLaunchUrl() without warmup() succeeds. */
    @SmallTest
    public void testMayLaunchUrlWithoutWarmup() {
        mayLaunchUrlWithoutWarmup(false);
    }

    /** Tests that calling warmup() is optional without prerendering. */
    @SmallTest
    public void testMayLaunchUrlWithoutWarmupNoPrerendering() {
        mayLaunchUrlWithoutWarmup(true);
    }

    /**
     * Tests that launching a regular Chrome tab after warmup() gives the right layout.
     *
     * About the restrictions and switches: No FRE and no document mode to get a
     * ChromeTabbedActivity, and no tablets to have the tab switcher button.
     *
     * Non-regression test for crbug.com/547121.
     */
    @SmallTest
    @Restriction(RESTRICTION_TYPE_PHONE)
    @CommandLineFlags.Add({
            ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE, ChromeSwitches.DISABLE_DOCUMENT_MODE})
    public void testWarmupAndLaunchRegularChrome() {
        warmUpAndWait();
        Intent intent =
                new Intent(getInstrumentation().getTargetContext(), ChromeLauncherActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);

        Instrumentation.ActivityMonitor monitor =
                getInstrumentation().addMonitor(ChromeTabbedActivity.class.getName(), null, false);
        Activity activity = getInstrumentation().startActivitySync(intent);
        assertNotNull("Main activity did not start", activity);
        ChromeTabbedActivity tabbedActivity =
                (ChromeTabbedActivity) monitor.waitForActivityWithTimeout(
                        ACTIVITY_START_TIMEOUT_MS);
        assertNotNull("ChromeTabbedActivity did not start", tabbedActivity);
        assertNotNull("Should have a tab switcher button.",
                tabbedActivity.findViewById(R.id.tab_switcher_button));
    }

    /**
     * Tests that launching a Custom Tab after warmup() gives the right layout.
     *
     * Non-regression test for crbug.com/547121.
     */
    @SmallTest
    @Restriction(RESTRICTION_TYPE_PHONE)
    @CommandLineFlags.Add({
            ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE, ChromeSwitches.DISABLE_DOCUMENT_MODE})
    public void testWarmupAndLaunchRightToolbarLayout() {
        warmUpAndWait();
        startActivityCompletely(createMinimalCustomTabIntent());
        assertNull("Should not have a tab switcher button.",
                mActivity.findViewById(R.id.tab_switcher_button));
    }

    private void mayLaunchUrlWithoutWarmup(boolean noPrerendering) {
        Context context = getInstrumentation().getTargetContext().getApplicationContext();
        CustomTabsConnection connection =
                CustomTabsTestUtils.setUpConnection((Application) context);
        ICustomTabsCallback cb = new CustomTabsTestUtils.DummyCallback();
        connection.newSession(cb);
        Bundle extras = null;
        if (noPrerendering) {
            extras = new Bundle();
            extras.putBoolean(CustomTabsConnection.NO_PRERENDERING_KEY, true);
        }
        assertTrue(connection.mayLaunchUrl(cb, Uri.parse(TEST_PAGE), extras, null));
        try {
            startCustomTabActivityWithIntent(CustomTabsTestUtils.createMinimalCustomTabIntent(
                    context, TEST_PAGE, cb.asBinder()));
        } catch (InterruptedException e) {
            fail();
        }
        Tab tab = getActivity().getActivityTab();
        assertEquals(TEST_PAGE, tab.getUrl());
    }

    private CustomTabsConnection warmUpAndWait() {
        final Context context = getInstrumentation().getTargetContext().getApplicationContext();
        CustomTabsConnection connection =
                CustomTabsTestUtils.setUpConnection((Application) context);
        final CallbackHelper startupCallbackHelper = new CallbackHelper();
        assertTrue(connection.warmup(0));
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                BrowserStartupController.get(context, LibraryProcessType.PROCESS_BROWSER)
                        .addStartupCompletedObserver(new StartupCallback() {
                            @Override
                            public void onSuccess(boolean alreadyStarted) {
                                startupCallbackHelper.notifyCalled();
                            }

                            @Override
                            public void onFailure() {
                                fail();
                            }
                        });
            }
        });

        try {
            startupCallbackHelper.waitForCallback(0);
        } catch (TimeoutException | InterruptedException e) {
            fail();
        }
        return connection;
    }

    private IBinder warmUpAndLaunchUrlWithSession(ICustomTabsCallback cb)
            throws InterruptedException {
        CustomTabsConnection connection = warmUpAndWait();
        connection.newSession(cb);
        Context context = getInstrumentation().getTargetContext().getApplicationContext();
        startCustomTabActivityWithIntent(CustomTabsTestUtils.createMinimalCustomTabIntent(
                context, TEST_PAGE, cb.asBinder()));
        return cb.asBinder();
    }

    private IBinder warmUpAndLaunchUrlWithSession() throws InterruptedException {
        ICustomTabsCallback cb = new CustomTabsTestUtils.DummyCallback();
        return warmUpAndLaunchUrlWithSession(cb);
    }

    /**
     * A helper class to monitor sending status of a {@link PendingIntent}.
     */
    private static class OnFinishedForTest implements PendingIntent.OnFinished {

        private PendingIntent mPi;
        private AtomicBoolean mIsSent = new AtomicBoolean();
        private String mUri;

        /**
         * Create an instance of {@link OnFinishedForTest}, testing the given {@link PendingIntent}.
         */
        public OnFinishedForTest(PendingIntent pi) {
            mPi = pi;
        }

        /**
         * @return Whether {@link PendingIntent#send()} has been successfully triggered and the sent
         *         intent carries the correct Uri as data.
         */
        public boolean isSent() {
            return mIsSent.get() && TEST_PAGE.equals(mUri);
        }

        @Override
        public void onSendFinished(PendingIntent pendingIntent, Intent intent, int resultCode,
                String resultData, Bundle resultExtras) {
            if (pendingIntent.equals(mPi)) {
                mUri = intent.getDataString();
                mIsSent.set(true);
            }
        }
    }
}
