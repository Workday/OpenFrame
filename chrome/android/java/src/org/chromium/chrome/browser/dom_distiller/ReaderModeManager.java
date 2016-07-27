// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.dom_distiller;

import android.content.Context;
import android.text.TextUtils;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.CommandLine;
import org.chromium.base.SysUtils;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.compositor.bottombar.OverlayPanel.StateChangeReason;
import org.chromium.chrome.browser.compositor.bottombar.readermode.ReaderModePanel;
import org.chromium.chrome.browser.infobar.InfoBar;
import org.chromium.chrome.browser.infobar.InfoBarContainer;
import org.chromium.chrome.browser.infobar.InfoBarContainer.InfoBarContainerObserver;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabObserver;
import org.chromium.components.dom_distiller.content.DistillablePageUtils;
import org.chromium.components.dom_distiller.core.DomDistillerUrlUtils;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.browser.WebContentsObserver;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.base.PageTransition;

import java.util.HashMap;
import java.util.Map;

/**
 * Manages UI effects for reader mode including hiding and showing the
 * reader mode and reader mode preferences toolbar icon and hiding the
 * top controls when a reader mode page has finished loading.
 */
public class ReaderModeManager extends TabModelSelectorTabObserver
        implements InfoBarContainerObserver, ReaderModeManagerDelegate {

    /**
     * POSSIBLE means reader mode can be entered.
     */
    public static final int POSSIBLE = 0;

    /**
     * NOT_POSSIBLE means reader mode cannot be entered.
     */
    public static final int NOT_POSSIBLE = 1;

    /**
     * STARTED means reader mode is currently in reader mode.
     */
    public static final int STARTED = 2;

    // The url of the last page visited if the last page was reader mode page.  Otherwise null.
    private String mReaderModePageUrl;

    // Whether the fact that the current web page was distillable or not has been recorded.
    private boolean mIsUmaRecorded;

    // The per-tab state of distillation.
    private Map<Integer, ReaderModeTabInfo> mTabStatusMap;

    // The current tab ID. This will change as the user switches between tabs.
    private int mTabId;

    // The ReaderModePanel that this class is managing.
    protected ReaderModePanel mReaderModePanel;

    // The ChromeActivity that this panel exists in.
    private ChromeActivity mChromeActivity;

    // The primary means of getting the currently active tab.
    private TabModelSelector mTabModelSelector;

    private final int mHeaderBackgroundColor;
    private boolean mIsFullscreenModeEntered;
    private boolean mIsInfobarContainerShown;

    public ReaderModeManager(TabModelSelector selector, ChromeActivity activity) {
        super(selector);
        mTabId = Tab.INVALID_TAB_ID;
        mTabModelSelector = selector;
        mChromeActivity = activity;
        mTabStatusMap = new HashMap<>();
        mHeaderBackgroundColor = activity != null
                ? ApiCompatibilityUtils.getColor(
                        activity.getResources(), R.color.reader_mode_header_bg)
                : 0;
        DomDistillerUIUtils.setReaderModeManagerDelegate(this);
    }

    /**
     * Clear the status map and references to other objects.
     */
    public void destroy() {
        for (Map.Entry<Integer, ReaderModeTabInfo> e : mTabStatusMap.entrySet()) {
            if (e.getValue().getWebContentsObserver() != null) {
                e.getValue().getWebContentsObserver().destroy();
            }
        }
        mTabStatusMap.clear();

        DomDistillerUIUtils.destroy();

        mChromeActivity = null;
        mReaderModePanel = null;
        mTabModelSelector = null;
    }

    // TabModelSelectorTabObserver:

    @Override
    public void onShown(Tab shownTab) {
        int shownTabId = shownTab.getId();

        Tab previousTab = mTabModelSelector.getTabById(mTabId);
        mTabId = shownTabId;

        // If the reader panel was dismissed, stop here.
        if (mTabStatusMap.containsKey(shownTabId)
                && mTabStatusMap.get(shownTabId).isDismissed()) {
            return;
        }

        // Remove the infobar observer from the previous tab and attach it to the current one.
        if (previousTab != null && previousTab.getInfoBarContainer() != null) {
            previousTab.getInfoBarContainer().removeObserver(this);
        }

        if (shownTab.getInfoBarContainer() != null) {
            shownTab.getInfoBarContainer().addObserver(this);
        }

        // If there is no state info for this tab, create it.
        ReaderModeTabInfo tabInfo = mTabStatusMap.get(shownTabId);
        if (tabInfo == null) {
            tabInfo = new ReaderModeTabInfo();
            tabInfo.setStatus(NOT_POSSIBLE);
            tabInfo.setUrl(shownTab.getUrl());
            mTabStatusMap.put(shownTabId, tabInfo);
        }

        // Make sure there is a WebContentsObserver on this tab's WebContents.
        if (tabInfo.getWebContentsObserver() == null) {
            tabInfo.setWebContentsObserver(createWebContentsObserver(shownTab.getWebContents()));
        }

        // Make sure there is a distillability delegate set on the WebContents.
        setDistillabilityCallback();

        requestReaderPanelShow(StateChangeReason.UNKNOWN);
    }

    @Override
    public void onHidden(Tab tab) {
        closeReaderPanel(StateChangeReason.UNKNOWN, false);
    }

    @Override
    public void onDestroyed(Tab tab) {
        if (tab.getInfoBarContainer() != null) {
            tab.getInfoBarContainer().removeObserver(this);
        }
        removeTabState(tab.getId());
    }

    /**
     * Clean up the state associated with a tab.
     * @param tabId The target tab ID.
     */
    private void removeTabState(int tabId) {
        if (!mTabStatusMap.containsKey(tabId)) return;
        ReaderModeTabInfo tabInfo = mTabStatusMap.get(tabId);
        if (tabInfo.getWebContentsObserver() != null) {
            tabInfo.getWebContentsObserver().destroy();
        }
        mTabStatusMap.remove(tabId);
    }

    @Override
    public void onContentChanged(Tab tab) {
        // Only listen to events on the currently active tab.
        if (tab.getId() != mTabId) return;

        if (mTabStatusMap.containsKey(mTabId)) {
            // If the panel was closed using the "x" icon, don't show it again for this tab.
            if (mTabStatusMap.get(mTabId).isDismissed()) return;
            removeTabState(mTabId);
        }

        ReaderModeTabInfo tabInfo = new ReaderModeTabInfo();
        tabInfo.setStatus(NOT_POSSIBLE);
        tabInfo.setUrl(tab.getUrl());
        mTabStatusMap.put(tab.getId(), tabInfo);

        if (tab.getWebContents() != null) {
            tabInfo.setWebContentsObserver(createWebContentsObserver(tab.getWebContents()));
            if (DomDistillerUrlUtils.isDistilledPage(tab.getUrl())) {
                tabInfo.setStatus(STARTED);
                mReaderModePageUrl = tab.getUrl();
                closeReaderPanel(StateChangeReason.CONTENT_CHANGED, true);
            }
            // Make sure there is a distillability delegate set on the WebContents.
            setDistillabilityCallback();
        }

        if (tab.getInfoBarContainer() != null) tab.getInfoBarContainer().addObserver(this);
    }

    @Override
    public void onToggleFullscreenMode(Tab tab, boolean enable) {
        // Temporarily hide the reader mode panel while fullscreen is enabled.
        if (enable) {
            mIsFullscreenModeEntered = true;
            closeReaderPanel(StateChangeReason.FULLSCREEN_ENTERED, false);
        } else {
            mIsFullscreenModeEntered = false;
            requestReaderPanelShow(StateChangeReason.FULLSCREEN_EXITED);
        }
    }

    // InfoBarContainerObserver:

    @Override
    public void onAddInfoBar(InfoBarContainer container, InfoBar infoBar, boolean isFirst) {
        // Temporarily hides the reader mode button while the infobars are shown.
        if (isFirst) {
            mIsInfobarContainerShown = true;
            closeReaderPanel(StateChangeReason.INFOBAR_SHOWN, false);
        }
    }

    @Override
    public void onRemoveInfoBar(InfoBarContainer container, InfoBar infoBar, boolean isLast) {
        // Re-shows the reader mode button if necessary once the infobars are dismissed.
        if (isLast) {
            mIsInfobarContainerShown = false;
            requestReaderPanelShow(StateChangeReason.INFOBAR_HIDDEN);
        }
    }

    // ReaderModeManagerDelegate:

    @Override
    public int getReaderModeHeaderBackgroundColor() {
        return mHeaderBackgroundColor;
    }

    @Override
    public int getReaderModeStatus() {
        int currentTabId = mTabModelSelector.getCurrentTabId();
        if (currentTabId == Tab.INVALID_TAB_ID) return NOT_POSSIBLE;

        if (!mTabStatusMap.containsKey(currentTabId)) return NOT_POSSIBLE;
        return mTabStatusMap.get(currentTabId).getStatus();
    }

    @Override
    public void setReaderModePanel(ReaderModePanel panel) {
        mReaderModePanel = panel;
    }

    @Override
    public ChromeActivity getChromeActivity() {
        return mChromeActivity;
    }

    @Override
    public void onCloseButtonPressed() {
        int currentTabId = mTabModelSelector.getCurrentTabId();
        if (currentTabId == Tab.INVALID_TAB_ID) return;

        if (!mTabStatusMap.containsKey(currentTabId)) return;
        ReaderModeTabInfo tabInfo = mTabStatusMap.get(currentTabId);
        tabInfo.setIsDismissed(true);
        if (tabInfo.getWebContentsObserver() != null) {
            tabInfo.getWebContentsObserver().destroy();
        }
    }

    @Override
    public WebContents getBasePageWebContents() {
        Tab tab = mTabModelSelector.getCurrentTab();
        if (tab == null) return null;

        return tab.getWebContents();
    }

    /**
     * This is a proxy method for those with access to the ReaderModeManagerDelegate to close the
     * panel.
     */
    @Override
    public void closePanel(StateChangeReason reason, boolean animate) {
        if (mReaderModePanel == null) return;
        mReaderModePanel.closePanel(reason, animate);
    }

    protected WebContentsObserver createWebContentsObserver(WebContents webContents) {
        final int readerTabId = mTabModelSelector.getCurrentTabId();
        if (readerTabId == Tab.INVALID_TAB_ID) return null;

        return new WebContentsObserver(webContents) {
            @Override
            public void didStartProvisionalLoadForFrame(long frameId, long parentFrameId,
                    boolean isMainFrame, String validatedUrl, boolean isErrorPage,
                    boolean isIframeSrcdoc) {
                if (!isMainFrame) return;

                // Make sure the tab was not destroyed.
                ReaderModeTabInfo tabInfo = mTabStatusMap.get(readerTabId);
                if (tabInfo == null) return;

                tabInfo.setUrl(validatedUrl);
                if (DomDistillerUrlUtils.isDistilledPage(validatedUrl)) {
                    tabInfo.setStatus(STARTED);
                    closeReaderPanel(StateChangeReason.UNKNOWN, true);
                    mReaderModePageUrl = validatedUrl;
                }
            }

            @Override
            public void didNavigateMainFrame(String url, String baseUrl,
                    boolean isNavigationToDifferentPage, boolean isNavigationInPage,
                    int statusCode) {
                // TODO(cjhopman): This should possibly ignore navigations that replace the entry
                // (like those from history.replaceState()).
                if (isNavigationInPage) return;
                if (DomDistillerUrlUtils.isDistilledPage(url)) return;

                // Make sure the tab was not destroyed.
                ReaderModeTabInfo tabInfo = mTabStatusMap.get(readerTabId);
                if (tabInfo == null) return;

                tabInfo.setStatus(POSSIBLE);
                if (!TextUtils.equals(url,
                        DomDistillerUrlUtils.getOriginalUrlFromDistillerUrl(
                                mReaderModePageUrl))) {
                    tabInfo.setStatus(NOT_POSSIBLE);
                    mIsUmaRecorded = false;
                }
                mReaderModePageUrl = null;

                if (tabInfo.getStatus() != POSSIBLE) {
                    closeReaderPanel(StateChangeReason.UNKNOWN, false);
                } else {
                    requestReaderPanelShow(StateChangeReason.UNKNOWN);
                }
            }
        };
    }

    /**
     * This is a wrapper for "requestPanelShow" that checks if reader mode is possible before
     * showing.
     * @param reason The reason the panel is requesting to be shown.
     */
    protected void requestReaderPanelShow(StateChangeReason reason) {
        int currentTabId = mTabModelSelector.getCurrentTabId();
        if (currentTabId == Tab.INVALID_TAB_ID) return;

        if (mReaderModePanel == null || !mTabStatusMap.containsKey(currentTabId)
                || mTabStatusMap.get(currentTabId).getStatus() != POSSIBLE
                || mTabStatusMap.get(currentTabId).isDismissed()
                || mIsInfobarContainerShown
                || mIsFullscreenModeEntered) {
            return;
        }
        mReaderModePanel.requestPanelShow(reason);
    }

    /**
     * A wrapper for the close method of the Reader Mode panel that checks for null and can be
     * overridden for testing.
     * @param reason The reason the panel is closing.
     * @param animate True if the panel should animate closed.
     */
    protected void closeReaderPanel(StateChangeReason reason, boolean animate) {
        if (mReaderModePanel == null) return;
        mReaderModePanel.closePanel(reason, animate);
    }

    /**
     * Orientation change event handler. Simply close the panel.
     */
    public void onOrientationChange() {
        // Close to reset the panel then immediately show again.
        closeReaderPanel(StateChangeReason.UNKNOWN, false);
        requestReaderPanelShow(StateChangeReason.UNKNOWN);
    }

    /**
     * Open a link from the panel in a new tab.
     * @param url The URL to load.
     */
    public void createNewTab(String url) {
        if (mChromeActivity == null) return;

        Tab currentTab = mTabModelSelector.getCurrentTab();
        if (currentTab == null) return;

        TabCreatorManager.TabCreator tabCreator =
                mChromeActivity.getTabCreator(currentTab.isIncognito());
        if (tabCreator == null) return;

        tabCreator.createNewTab(new LoadUrlParams(url, PageTransition.LINK),
                TabModel.TabLaunchType.FROM_LINK, mChromeActivity.getActivityTab());
    }

    /**
     * @return Whether the Reader Mode panel is opened (state is EXPANDED or MAXIMIZED).
     */
    public boolean isPanelOpened() {
        if (mReaderModePanel == null) return false;
        return mReaderModePanel.isPanelOpened();
    }

    // Set the callback for updating reader mode status based on whether or not the page should
    // be viewed in reader mode.
    private void setDistillabilityCallback() {
        Tab currentTab = mTabModelSelector.getCurrentTab();
        if (currentTab == null || currentTab.getWebContents() == null
                || currentTab.getContentViewCore() == null) {
            return;
        }

        final int readerTabId = currentTab.getId();
        if (mTabStatusMap.get(readerTabId).isCallbackSet()) {
            return;
        }

        DistillablePageUtils.setDelegate(currentTab.getWebContents(),
                new DistillablePageUtils.PageDistillableDelegate() {
                    @Override
                    public void onIsPageDistillableResult(boolean isDistillable, boolean isLast) {
                        ReaderModeTabInfo tabInfo = mTabStatusMap.get(readerTabId);
                        Tab readerTab = mTabModelSelector.getTabById(readerTabId);

                        // It is possible that the tab was destroyed before this callback happens.
                        // TODO(wychen/mdjones): Remove the callback when a Tab/WebContents is
                        // destroyed so that this never happens.
                        if (readerTab == null || tabInfo == null) return;

                        // Make sure the page didn't navigate while waiting for a response.
                        if (!readerTab.getUrl().equals(tabInfo.getUrl())) return;

                        if (isDistillable) {
                            tabInfo.setStatus(POSSIBLE);
                            // The user may have changed tabs.
                            if (readerTabId == mTabModelSelector.getCurrentTabId()) {
                                // TODO(mdjones): Add reason DISTILLER_STATE_CHANGE.
                                requestReaderPanelShow(StateChangeReason.UNKNOWN);
                            }
                        } else {
                            tabInfo.setStatus(NOT_POSSIBLE);
                        }
                        if (!mIsUmaRecorded && (tabInfo.getStatus() == POSSIBLE || isLast)) {
                            mIsUmaRecorded = true;
                            RecordHistogram.recordBooleanHistogram(
                                    "DomDistiller.PageDistillable",
                                    tabInfo.getStatus() == POSSIBLE);
                        }
                    }
                });
        mTabStatusMap.get(readerTabId).setIsCallbackSet(true);
    }

    /**
     * @return Whether Reader mode and its new UI are enabled.
     * @param context A context
     */
    public static boolean isEnabled(Context context) {
        if (context == null) return false;

        boolean enabled = CommandLine.getInstance().hasSwitch(ChromeSwitches.ENABLE_DOM_DISTILLER)
                && !CommandLine.getInstance().hasSwitch(
                        ChromeSwitches.DISABLE_READER_MODE_BOTTOM_BAR)
                && !DeviceFormFactor.isTablet(context)
                && DomDistillerTabUtils.isDistillerHeuristicsEnabled()
                && !SysUtils.isLowEndDevice();
        return enabled;
    }
}
