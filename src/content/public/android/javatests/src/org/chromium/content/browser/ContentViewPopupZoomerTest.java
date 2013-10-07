// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import android.test.suitebuilder.annotation.MediumTest;
import android.view.View;

import org.chromium.base.test.util.DisabledTest;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.UrlUtils;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;
import org.chromium.content.browser.test.util.DOMUtils;
import org.chromium.content.browser.test.util.TestCallbackHelperContainer;
import org.chromium.content.browser.test.util.TestCallbackHelperContainer.OnPageFinishedHelper;
import org.chromium.content_shell_apk.ContentShellTestBase;

import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

public class ContentViewPopupZoomerTest extends ContentShellTestBase {
    private static final int WAIT_TIMEOUT_SECONDS = 2;

    private static PopupZoomer findPopupZoomer(ContentView view) {
        assert view != null;
        for (int i = 0; i < view.getChildCount(); i++) {
            View child = view.getChildAt(i);
            if (child instanceof PopupZoomer) return (PopupZoomer) child;
        }
        return null;
    }

    private static class PopupShowingCriteria implements Criteria {
        private final ContentView mView;
        private final boolean mShouldBeShown;
        public PopupShowingCriteria(ContentView view, boolean shouldBeShown) {
            mView = view;
            mShouldBeShown = shouldBeShown;
        }
        @Override
        public boolean isSatisfied() {
            PopupZoomer popup = findPopupZoomer(mView);
            boolean isVisibilitySet = popup == null ? false : popup.getVisibility() == View.VISIBLE;
            return isVisibilitySet ? mShouldBeShown : !mShouldBeShown;
        }
    }

    private static class PopupHasNonZeroDimensionsCriteria implements Criteria {
        private final ContentView mView;
        public PopupHasNonZeroDimensionsCriteria(ContentView view) {
            mView = view;
        }
        @Override
        public boolean isSatisfied() {
            PopupZoomer popup = findPopupZoomer(mView);
            if (popup == null) return false;
            return popup.getWidth() != 0 && popup.getHeight() != 0;
        }
    }

    private String generateTestUrl(int totalUrls, int targetIdAt, String targetId) {
        StringBuilder testUrl = new StringBuilder();
        testUrl.append("<html><body>");
        for (int i = 0; i < totalUrls; i++) {
            boolean isTargeted = i == targetIdAt;
            testUrl.append("<a href=\"data:text/html;utf-8,<html><head><script>" +
                    "function doesItWork() { return 'yes'; }</script></head></html>\"" +
                    (isTargeted ? (" id=\"" + targetId + "\"") : "") + ">" +
                    "<small><sup>" +
                    (isTargeted ? "<b>" : "") + i + (isTargeted ? "</b>" : "") +
                    "</sup></small></a>");
        }
        testUrl.append("</small></div></body></html>");
        return UrlUtils.encodeHtmlDataUri(testUrl.toString());
    }

    public ContentViewPopupZoomerTest() {
    }

    /**
     * Tests that shows a zoomer popup and makes sure it has valid dimensions.
     */
    //@MediumTest
    //@Feature({"Browser"})
    @DisabledTest // crbug.com/167045
    public void testPopupZoomerShowsUp() throws InterruptedException, TimeoutException {
        launchContentShellWithUrl(generateTestUrl(100, 15, "clickme"));
        assertTrue("Page failed to load", waitForActiveShellToBeDoneLoading());

        final ContentView view = getActivity().getActiveContentView();
        final TestCallbackHelperContainer viewClient =
                new TestCallbackHelperContainer(view);

        // The popup should be hidden before the click.
        assertTrue("The zoomer popup is shown after load.",
                CriteriaHelper.pollForCriteria(new PopupShowingCriteria(view, false)));

        // Once clicked, the popup should show up.
        DOMUtils.clickNode(this, view, viewClient, "clickme");
        assertTrue("The zoomer popup did not show up on click.",
                CriteriaHelper.pollForCriteria(new PopupShowingCriteria(view, true)));

        // The shown popup should have valid dimensions eventually.
        assertTrue("The zoomer popup has zero dimensions.",
                CriteriaHelper.pollForCriteria(new PopupHasNonZeroDimensionsCriteria(view)));
    }
}
