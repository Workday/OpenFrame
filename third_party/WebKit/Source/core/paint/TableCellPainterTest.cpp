// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "core/paint/PaintControllerPaintTest.h"
#include "core/paint/PaintLayerPainter.h"

namespace blink {

using TableCellPainterTest = PaintControllerPaintTest;

TEST_F(TableCellPainterTest, TableCellBackgroundInterestRect)
{
    RuntimeEnabledFeatures::setSlimmingPaintSynchronizedPaintingEnabled(true);

    setBodyInnerHTML(
        "<style>"
        "  td { width: 200px; height: 200px; border: none; }"
        "  tr { background-color: blue; }"
        "  table { border: none; border-spacing: 0; border-collapse: collapse; }"
        "</style>"
        "<table>"
        "  <tr><td id='cell1'></td></tr>"
        "  <tr><td id='cell2'></td></tr>"
        "</table>");

    LayoutView& layoutView = *document().layoutView();
    PaintLayer& rootLayer = *layoutView.layer();
    LayoutObject& cell1 = *document().getElementById("cell1")->layoutObject();
    LayoutObject& cell2 = *document().getElementById("cell2")->layoutObject();

    rootPaintController().invalidateAll();
    updateLifecyclePhasesBeforePaint();
    IntRect interestRect(0, 0, 200, 200);
    paint(&interestRect);
    commit();

    EXPECT_DISPLAY_LIST(rootPaintController().displayItemList(), 4,
        TestDisplayItem(rootLayer, DisplayItem::Subsequence),
        TestDisplayItem(layoutView, DisplayItem::BoxDecorationBackground),
        TestDisplayItem(cell1, DisplayItem::TableCellBackgroundFromRow),
        TestDisplayItem(rootLayer, DisplayItem::EndSubsequence));

    updateLifecyclePhasesBeforePaint();
    interestRect = IntRect(0, 300, 200, 1000);
    paint(&interestRect);
    commit();

    EXPECT_DISPLAY_LIST(rootPaintController().displayItemList(), 4,
        TestDisplayItem(rootLayer, DisplayItem::Subsequence),
        TestDisplayItem(layoutView, DisplayItem::BoxDecorationBackground),
        TestDisplayItem(cell2, DisplayItem::TableCellBackgroundFromRow),
        TestDisplayItem(rootLayer, DisplayItem::EndSubsequence));
}

} // namespace blink
