// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import android.app.Dialog;
import android.test.suitebuilder.annotation.SmallTest;
import android.text.SpannableString;
import android.view.View;
import android.widget.Button;
import android.widget.ListView;

import org.chromium.base.ThreadUtils;
import org.chromium.chrome.R;
import org.chromium.chrome.test.ChromeActivityTestCaseBase;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;
import org.chromium.content.browser.test.util.TouchCommon;
import org.chromium.ui.widget.TextViewWithClickableSpans;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Callable;

/**
 * Tests for the ItemChooserDialog class.
 */
public class ItemChooserDialogTest extends ChromeActivityTestCaseBase<ChromeActivity>
        implements ItemChooserDialog.ItemSelectedCallback {

    ItemChooserDialog mChooserDialog;

    String mLastSelectedId = "None";

    public ItemChooserDialogTest() {
        super(ChromeActivity.class);
    }

    // ChromeActivityTestCaseBase:

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        mChooserDialog = createDialog();
    }

    @Override
    public void startMainActivity() throws InterruptedException {
        startMainActivityOnBlankPage();
    }

    // ItemChooserDialog.ItemSelectedCallback:

    @Override
    public void onItemSelected(String id) {
        mLastSelectedId = id;
    }

    private ItemChooserDialog createDialog() {
        SpannableString title = new SpannableString("title");
        String searching = new String("searching");
        SpannableString noneFound = new SpannableString("noneFound");
        SpannableString status = new SpannableString("status");
        String positiveButton = new String("positiveButton");
        final ItemChooserDialog.ItemChooserLabels labels = new ItemChooserDialog.ItemChooserLabels(
                title, searching, noneFound, status, positiveButton);
        ItemChooserDialog dialog = ThreadUtils.runOnUiThreadBlockingNoException(
                new Callable<ItemChooserDialog>() {
                        @Override
                        public ItemChooserDialog call() {
                            ItemChooserDialog dialog = new ItemChooserDialog(
                                    getActivity(), ItemChooserDialogTest.this, labels);
                            return dialog;
                        }
                });
        return dialog;
    }

    private void selectItem(Dialog dialog, int position, final String expectedItemId)
            throws InterruptedException {
        final ListView items = (ListView) dialog.findViewById(R.id.items);
        final Button button = (Button) dialog.findViewById(R.id.positive);

        CriteriaHelper.pollForUIThreadCriteria(new Criteria() {
            @Override
            public boolean isSatisfied() {
                return items.getChildAt(0) != null;
            }
        });

        // Verify first item selected gets selected.
        // TODO(finnur): Stop using coordinates 10, 10 when crbug.com/532237 is fixed.
        TouchCommon.singleClickView(items.getChildAt(position - 1), 10, 10);

        CriteriaHelper.pollForUIThreadCriteria(new Criteria() {
            @Override
            public boolean isSatisfied() {
                return button.isEnabled();
            }
        });

        // TODO(finnur): Stop using coordinates 10, 10 when crbug.com/532237 is fixed.
        TouchCommon.singleClickView(button, 10, 10);

        CriteriaHelper.pollForUIThreadCriteria(new Criteria() {
            @Override
            public boolean isSatisfied() {
                return mLastSelectedId.equals(expectedItemId);
            }
        });
    }

    @SmallTest
    public void testSimpleItemSelection() throws InterruptedException {
        Dialog dialog = mChooserDialog.getDialogForTesting();
        assertTrue(dialog.isShowing());

        TextViewWithClickableSpans statusView = (TextViewWithClickableSpans)
                dialog.findViewById(R.id.status);
        final ListView items = (ListView) dialog.findViewById(R.id.items);
        final Button button = (Button) dialog.findViewById(R.id.positive);

        // Before we add items to the dialog, the 'searching' message should be
        // showing, the Commit button should be disabled and the list view hidden.
        assertEquals("searching", statusView.getText().toString());
        assertFalse(button.isEnabled());
        assertEquals(View.GONE, items.getVisibility());

        List<ItemChooserDialog.ItemChooserRow> devices =
                new ArrayList<ItemChooserDialog.ItemChooserRow>();
        mChooserDialog.showList(devices);

        // Listview should now be showing empty, with an empty view visible to
        // drive home the point and a status message at the bottom.
        assertEquals(View.VISIBLE, items.getVisibility());
        assertEquals(View.VISIBLE, items.getEmptyView().getVisibility());
        assertEquals("status", statusView.getText().toString());
        assertFalse(button.isEnabled());

        devices = new ArrayList<ItemChooserDialog.ItemChooserRow>();
        devices.add(new ItemChooserDialog.ItemChooserRow("key", "key"));
        devices.add(new ItemChooserDialog.ItemChooserRow("key2", "key2"));
        mChooserDialog.showList(devices);

        // Two items showing, the emty view should be no more and the button
        // should now be enabled.
        assertEquals(View.VISIBLE, items.getVisibility());
        assertEquals(View.GONE, items.getEmptyView().getVisibility());
        assertEquals("status", statusView.getText().toString());
        assertFalse(button.isEnabled());

        // Select the first item and verify it got selected.
        selectItem(dialog, 1, "key");

        mChooserDialog.dismiss();
    }

    @SmallTest
    public void testDisabledSelection() throws InterruptedException {
        Dialog dialog = mChooserDialog.getDialogForTesting();
        assertTrue(dialog.isShowing());

        List<ItemChooserDialog.ItemChooserRow> devices =
                new ArrayList<ItemChooserDialog.ItemChooserRow>();
        devices.add(new ItemChooserDialog.ItemChooserRow("key", "key"));
        devices.add(new ItemChooserDialog.ItemChooserRow("key2", "key2"));
        mChooserDialog.showList(devices);

        // Disable one item and try to select it.
        mChooserDialog.setEnabled("key", false);
        selectItem(dialog, 1, "None");
        // The other is still selectable.
        selectItem(dialog, 2, "key2");

        mChooserDialog.dismiss();
    }
}
