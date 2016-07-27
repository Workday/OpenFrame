// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.dom_distiller;

import android.content.Context;
import android.test.InstrumentationTestCase;
import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.base.test.util.Feature;
import org.chromium.chrome.browser.compositor.bottombar.OverlayPanel;
import org.chromium.chrome.browser.compositor.bottombar.OverlayPanel.StateChangeReason;
import org.chromium.chrome.browser.compositor.bottombar.OverlayPanelContent;
import org.chromium.chrome.browser.compositor.bottombar.OverlayPanelManager;
import org.chromium.chrome.browser.compositor.bottombar.readermode.ReaderModeBarControl;
import org.chromium.chrome.browser.compositor.bottombar.readermode.ReaderModePanel;
import org.chromium.chrome.browser.compositor.scene_layer.ContextualSearchSceneLayer;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.test.util.browser.tabmodel.MockTabModel.MockTabModelDelegate;
import org.chromium.chrome.test.util.browser.tabmodel.MockTabModelSelector;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.browser.WebContentsObserver;

/**
 * Tests logic in the ReaderModeManager.
 */
public class ReaderModeManagerTest extends InstrumentationTestCase {

    MockOverlayPanelManager mPanelManager;
    ReaderModeManagerWrapper mReaderManager;
    MockReaderModePanel mPanel;

    /**
     * A mock TabModelSelector for creating tabs.
     */
    private static class ReaderModeMockTabModelSelector extends MockTabModelSelector {
        public ReaderModeMockTabModelSelector() {
            super(2, 0,
                    new MockTabModelDelegate() {
                        @Override
                        public Tab createTab(int id, boolean incognito) {
                            return new Tab(id, incognito, null);
                        }
                    });
        }
    }

    /**
     * Mock OverlayPanelManager for recording but not actually performing events. This will also
     * detect calls to show/hide panel that do not pass through the ReaderModeManager's methods.
     */
    private static class MockOverlayPanelManager extends OverlayPanelManager {
        private int mRequestPanelShowCount;
        private int mPanelHideCount;

        @Override
        public void requestPanelShow(OverlayPanel p, StateChangeReason r) {
            mRequestPanelShowCount++;
        }

        @Override
        public void notifyPanelClosed(OverlayPanel p, StateChangeReason r) {
            mPanelHideCount++;
        }

        public int getRequestPanelShowCount() {
            return mRequestPanelShowCount;
        }

        public int getPanelHideCount() {
            return mPanelHideCount;
        }
    }

    /**
     * A wrapper for the ReaderModeManager; this is used for recording and triggering events
     * manually.
     */
    private static class ReaderModeManagerWrapper extends ReaderModeManager {
        public ReaderModeManagerWrapper() {
            super(new ReaderModeMockTabModelSelector(), null);
        }

        @Override
        protected void requestReaderPanelShow(StateChangeReason reason) {
            // Skip tab checks and request the panel be shown.
            mReaderModePanel.requestPanelShow(reason);
        }

        @Override
        public WebContentsObserver createWebContentsObserver(WebContents webContents) {
            // Do not attempt to create or attach a WebContentsObserver.
            return null;
        }
    }

    /**
     * Mock ReaderModePanel.
     */
    private static class MockReaderModePanel extends ReaderModePanel {
        public MockReaderModePanel(Context context, OverlayPanelManager manager) {
            super(context, null, manager, null);
        }

        @Override
        public ContextualSearchSceneLayer createNewReaderModeSceneLayer() {
            return null;
        }

        @Override
        protected ReaderModeBarControl getReaderModeBarControl() {
            return new MockReaderModeBarControl();
        }

        /**
         * This class is overridden to be completely inert; it would otherwise call many native
         * methods.
         */
        private static class MockReaderModeBarControl extends ReaderModeBarControl {
            public MockReaderModeBarControl() {
                super(null, null, null, null);
            }

            @Override
            public void setBarText(int stringId) {}

            @Override
            public void inflate() {}

            @Override
            public void invalidate() {}
        }

        /**
         * Override creation and destruction of the ContentViewCore as they rely on native methods.
         */
        private static class MockOverlayPanelContent extends OverlayPanelContent {
            public MockOverlayPanelContent() {
                super(null, null, null);
            }

            @Override
            public void removeLastHistoryEntry(String url, long timeInMs) {}
        }
    }

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        mPanelManager = new MockOverlayPanelManager();
        mReaderManager = new ReaderModeManagerWrapper();
        mPanel = new MockReaderModePanel(getInstrumentation().getTargetContext(), mPanelManager);
        mReaderManager.setReaderModePanel(mPanel);
    }

    // Start ReaderModeManager test suite.

    /**
     * Tests that the panel behaves appropriately with infobar events.
     */
    @SmallTest
    @Feature({"ReaderModeManager"})
    public void testInfoBarEvents() {
        mReaderManager.onAddInfoBar(null, null, true);
        assertEquals(0, mPanelManager.getRequestPanelShowCount());
        assertEquals(1, mPanelManager.getPanelHideCount());

        mReaderManager.onRemoveInfoBar(null, null, true);
        assertEquals(1, mPanelManager.getRequestPanelShowCount());
        assertEquals(1, mPanelManager.getPanelHideCount());
    }

    /**
     * Tests that the panel behaves appropriately with fullscreen events.
     */
    @SmallTest
    @Feature({"ReaderModeManager"})
    public void testFullscreenEvents() {
        mReaderManager.onToggleFullscreenMode(null, true);
        assertEquals(0, mPanelManager.getRequestPanelShowCount());
        assertEquals(1, mPanelManager.getPanelHideCount());

        mReaderManager.onToggleFullscreenMode(null, false);
        assertEquals(1, mPanelManager.getRequestPanelShowCount());
        assertEquals(1, mPanelManager.getPanelHideCount());
    }

    // TODO(mdjones): Test add/remove infobar while fullscreen is enabled.
    // TODO(mdjones): Test onclosebuttonpressed disables Reader Mode for a particular tab.
    // TODO(mdjones): Test onorientationchanged closes and re-opens panel.
    // TODO(mdjones): Test tab events.
    // TODO(mdjones): Test distillability callback.
}
