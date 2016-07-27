// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.datausage;

import android.app.Activity;
import android.content.Context;
import android.content.DialogInterface;
import android.content.DialogInterface.OnClickListener;
import android.preference.PreferenceManager;
import android.support.v7.app.AlertDialog;
import android.text.TextUtils;
import android.view.View;
import android.widget.CheckBox;

import org.chromium.base.metrics.RecordHistogram;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.EmbedContentViewActivity;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.sessions.SessionTabHelper;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.common.Referrer;

/**
 * Entry point to manage all UI details for measuring data use within a Tab.
 */
public class DataUseTabUIManager {

    private static final String SHARED_PREF_DATA_USE_DIALOG_OPT_OUT = "data_use_dialog_opt_out";

    /**
     * Represents the possible user actions with the data use snackbars and dialog. This must
     * remain in sync with DataUse.UIAction in tools/metrics/histograms/histograms.xml.
     */
    public static class DataUseUIActions {
        public static final int STARTED_SNACKBAR_SHOWN = 0;
        public static final int STARTED_SNACKBAR_MORE_CLICKED = 1;
        public static final int ENDED_SNACKBAR_SHOWN = 2;
        public static final int ENDED_SNACKBAR_MORE_CLICKED = 3;
        public static final int DIALOG_SHOWN = 4;
        public static final int DIALOG_CONTINUE_CLICKED = 5;
        public static final int DIALOG_CANCEL_CLICKED = 6;
        public static final int DIALOG_LEARN_MORE_CLICKED = 7;
        public static final int DIALOG_OPTED_OUT = 8;
        public static final int INDEX_BOUNDARY = 9;
    }

    /**
     * Returns true if data use tracking has started within a Tab. When data use tracking has
     * started, returns true only once to signify the started event.
     *
     * @param tab The tab that may have started tracking data use.
     * @return true If data use tracking has indeed started.
     */
    public static boolean checkDataUseTrackingStarted(Tab tab) {
        return nativeCheckDataUseTrackingStarted(
                SessionTabHelper.sessionIdForTab(tab.getWebContents()), tab.getProfile());
    }

    /**
     * Returns true if data use tracking has ended within a Tab. When data use tracking has
     * ended, returns true only once to signify the ended event.
     *
     * @param tab The tab that may have ended tracking data use.
     * @return true If data use tracking has indeed ended.
     */
    public static boolean checkDataUseTrackingEnded(Tab tab) {
        return nativeCheckDataUseTrackingEnded(
                SessionTabHelper.sessionIdForTab(tab.getWebContents()), tab.getProfile());
    }

    /**
     * Tells native code that a custom tab is navigating to a url from the given client app package.
     *
     * @param tab The custom tab that is navigating.
     * @param packageName The client app package for the custom tab loading a url.
     * @param url URL that is being loaded in the custom tab.
     */
    public static void onCustomTabInitialNavigation(Tab tab, String packageName, String url) {
        nativeOnCustomTabInitialNavigation(SessionTabHelper.sessionIdForTab(tab.getWebContents()),
                packageName, url, tab.getProfile());
    }

    /**
     * Returns whether a navigation should be paused to show a dialog telling the user that data use
     * tracking has ended within a Tab. If the navigation should be paused, shows a dialog with the
     * option to cancel the navigation or continue.
     *
     * @param activity Current activity.
     * @param tab The tab to see if tracking has ended in.
     * @param url URL that is pending.
     * @param pageTransitionType The type of transition. see
     *            {@link org.chromium.content.browser.PageTransition} for valid values.
     * @param referrerUrl URL for the referrer.
     * @return true If the URL loading should be overriden.
     */
    public static boolean shouldOverrideUrlLoading(Activity activity,
            final Tab tab, final String url, final int pageTransitionType,
            final String referrerUrl) {
        if (!getOptedOutOfDataUseDialog(activity) && checkDataUseTrackingEnded(tab)) {
            startDataUseDialog(activity, tab, url, pageTransitionType, referrerUrl);
            return true;
        }
        return false;
    }

    /**
     * Shows a dialog with the option to cancel the navigation or continue. Also allows the user to
     * opt out of seeing this dialog again.
     *
     * @param activity Current activity.
     * @param tab The tab loading the url.
     * @param url URL that is pending.
     * @param pageTransitionType The type of transition. see
     *            {@link org.chromium.content.browser.PageTransition} for valid values.
     * @param referrerUrl URL for the referrer.
     */
    private static void startDataUseDialog(final Activity activity, final Tab tab,
            final String url, final int pageTransitionType, final String referrerUrl) {
        View dataUseDialogView = View.inflate(activity, R.layout.data_use_dialog, null);
        final CheckBox checkBox = (CheckBox) dataUseDialogView.findViewById(R.id.data_use_checkbox);
        View learnMore = dataUseDialogView.findViewById(R.id.learn_more);
        learnMore.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(View v) {
                EmbedContentViewActivity.show(activity, R.string.data_use_learn_more_title,
                        R.string.data_use_learn_more_link_url);
                recordDataUseUIAction(DataUseUIActions.DIALOG_LEARN_MORE_CLICKED);
            }
        });
        new AlertDialog.Builder(activity, R.style.AlertDialogTheme)
                .setTitle(R.string.data_use_tracking_ended_title)
                .setView(dataUseDialogView)
                .setPositiveButton(R.string.data_use_tracking_ended_continue,
                        new OnClickListener() {
                            @Override
                            public void onClick(DialogInterface dialog, int which) {
                                setOptedOutOfDataUseDialog(activity, checkBox.isChecked());
                                LoadUrlParams loadUrlParams = new LoadUrlParams(url,
                                        pageTransitionType);
                                if (!TextUtils.isEmpty(referrerUrl)) {
                                    Referrer referrer = new Referrer(referrerUrl,
                                            Referrer.REFERRER_POLICY_ALWAYS);
                                    loadUrlParams.setReferrer(referrer);
                                }
                                tab.loadUrl(loadUrlParams);
                                recordDataUseUIAction(DataUseUIActions.DIALOG_CONTINUE_CLICKED);
                            }
                        })
                .setNegativeButton(R.string.cancel, new OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        setOptedOutOfDataUseDialog(activity, checkBox.isChecked());
                        recordDataUseUIAction(DataUseUIActions.DIALOG_CANCEL_CLICKED);
                    }
                })
                .show();
        recordDataUseUIAction(DataUseUIActions.DIALOG_SHOWN);
    }

    /**
     * Returns true if the user has opted out of seeing the data use dialog.
     *
     * @param context An Android context.
     * @return true If the user has opted out of seeing the data use dialog.
     */
    public static boolean getOptedOutOfDataUseDialog(Context context) {
        return PreferenceManager.getDefaultSharedPreferences(context).getBoolean(
                SHARED_PREF_DATA_USE_DIALOG_OPT_OUT, false);
    }

    /**
     * Sets whether the user has opted out of seeing the data use dialog.
     *
     * @param context An Android context.
     * @param optedOut Whether the user has opted out of seeing the data use dialog.
     */
    private static void setOptedOutOfDataUseDialog(Context context, boolean optedOut) {
        PreferenceManager.getDefaultSharedPreferences(context).edit()
                .putBoolean(SHARED_PREF_DATA_USE_DIALOG_OPT_OUT, optedOut)
                .apply();
        if (optedOut) {
            recordDataUseUIAction(DataUseUIActions.DIALOG_OPTED_OUT);
        }
    }

    /**
     * Record the DataUse.UIAction histogram.
     * @param action Action with the data use tracking snackbar or dialog.
     */
    public static void recordDataUseUIAction(int action) {
        assert action >= 0 && action < DataUseUIActions.INDEX_BOUNDARY;
        RecordHistogram.recordEnumeratedHistogram(
                "DataReductionProxy.UIAction", action,
                DataUseUIActions.INDEX_BOUNDARY);
    }

    private static native boolean nativeCheckDataUseTrackingStarted(int tabId, Profile profile);
    private static native boolean nativeCheckDataUseTrackingEnded(int tabId, Profile profile);
    private static native void nativeOnCustomTabInitialNavigation(int tabID, String packageName,
            String url, Profile profile);
}
