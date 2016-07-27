// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/layout/compositing/CompositedLayerMapping.h"

#include "core/frame/FrameView.h"
#include "core/html/HTMLIFrameElement.h"
#include "core/layout/LayoutBoxModelObject.h"
#include "core/layout/LayoutTestHelper.h"
#include "core/layout/LayoutView.h"
#include "core/loader/EmptyClients.h"
#include "core/paint/PaintLayer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

class CompositedLayerMappingTest : public RenderingTest {
public:
    CompositedLayerMappingTest()
        : RenderingTest(SingleChildFrameLoaderClient::create())
        , m_originalSlimmingPaintSynchronizedPaintingEnabled(RuntimeEnabledFeatures::slimmingPaintSynchronizedPaintingEnabled())
{ }

protected:
    IntRect recomputeInterestRect(const GraphicsLayer* graphicsLayer)
    {
        return static_cast<CompositedLayerMapping*>(graphicsLayer->client())->recomputeInterestRect(graphicsLayer);
    }

    IntRect computeInterestRect(const CompositedLayerMapping* compositedLayerMapping, GraphicsLayer* graphicsLayer, IntRect previousInterestRect)
    {
        return compositedLayerMapping->computeInterestRect(graphicsLayer, previousInterestRect);
    }

    bool interestRectChangedEnoughToRepaint(const IntRect& previousInterestRect, const IntRect& newInterestRect, const IntSize& layerSize)
    {
        return CompositedLayerMapping::interestRectChangedEnoughToRepaint(previousInterestRect, newInterestRect, layerSize);
    }

    IntRect previousInterestRect(const GraphicsLayer* graphicsLayer)
    {
        return graphicsLayer->m_previousInterestRect;
    }

private:
    void SetUp() override
    {
        RuntimeEnabledFeatures::setSlimmingPaintSynchronizedPaintingEnabled(true);

        RenderingTest::SetUp();
        enableCompositing();
        GraphicsLayer::setDrawDebugRedFillForTesting(false);
    }

    void TearDown() override
    {
        GraphicsLayer::setDrawDebugRedFillForTesting(true);
        RuntimeEnabledFeatures::setSlimmingPaintSynchronizedPaintingEnabled(m_originalSlimmingPaintSynchronizedPaintingEnabled);
    }

    bool m_originalSlimmingPaintSynchronizedPaintingEnabled;
};

#define EXPECT_RECT_EQ(expected, actual) \
    do { \
        EXPECT_EQ(expected.x(), actual.x()); \
        EXPECT_EQ(expected.y(), actual.y()); \
        EXPECT_EQ(expected.width(), actual.width()); \
        EXPECT_EQ(expected.height(), actual.height()); \
    } while (false)

TEST_F(CompositedLayerMappingTest, SimpleInterestRect)
{
    setBodyInnerHTML("<div id='target' style='width: 200px; height: 200px; will-change: transform'></div>");

    document().view()->updateAllLifecyclePhases();
    Element* element = document().getElementById("target");
    PaintLayer* paintLayer = toLayoutBoxModelObject(element->layoutObject())->layer();
    ASSERT_TRUE(!!paintLayer->graphicsLayerBacking());
    EXPECT_RECT_EQ(IntRect(0, 0, 200, 200), recomputeInterestRect(paintLayer->graphicsLayerBacking()));
}

TEST_F(CompositedLayerMappingTest, TallLayerInterestRect)
{
    setBodyInnerHTML("<div id='target' style='width: 200px; height: 10000px; will-change: transform'></div>");

    document().view()->updateAllLifecyclePhases();
    Element* element = document().getElementById("target");
    PaintLayer* paintLayer = toLayoutBoxModelObject(element->layoutObject())->layer();
    ASSERT_TRUE(paintLayer->graphicsLayerBacking());
    // Screen-space visible content rect is [8, 8, 200, 600]. Mapping back to local, adding 4000px in all directions, then
    // clipping, yields this rect.
    EXPECT_RECT_EQ(IntRect(0, 0, 200, 4592), recomputeInterestRect(paintLayer->graphicsLayerBacking()));
}

TEST_F(CompositedLayerMappingTest, TallLayerWholeDocumentInterestRect)
{
    setBodyInnerHTML("<div id='target' style='width: 200px; height: 10000px; will-change: transform'></div>");

    document().settings()->setMainFrameClipsContent(false);

    document().view()->updateAllLifecyclePhases();
    Element* element = document().getElementById("target");
    PaintLayer* paintLayer = toLayoutBoxModelObject(element->layoutObject())->layer();
    ASSERT_TRUE(paintLayer->graphicsLayerBacking());
    ASSERT_TRUE(paintLayer->compositedLayerMapping());
    // recomputeInterestRect computes the interest rect; computeInterestRect applies the extra setting to paint everything.
    EXPECT_RECT_EQ(IntRect(0, 0, 200, 4592), recomputeInterestRect(paintLayer->graphicsLayerBacking()));
    EXPECT_RECT_EQ(IntRect(0, 0, 200, 10000), computeInterestRect(paintLayer->compositedLayerMapping(), paintLayer->graphicsLayerBacking(), IntRect()));
}


TEST_F(CompositedLayerMappingTest, RotatedInterestRect)
{
    setBodyInnerHTML(
        "<div id='target' style='width: 200px; height: 200px; will-change: transform; transform: rotateZ(45deg)'></div>");

    document().view()->updateAllLifecyclePhases();
    Element* element = document().getElementById("target");
    PaintLayer* paintLayer = toLayoutBoxModelObject(element->layoutObject())->layer();
    ASSERT_TRUE(!!paintLayer->graphicsLayerBacking());
    EXPECT_RECT_EQ(IntRect(0, 0, 200, 200), recomputeInterestRect(paintLayer->graphicsLayerBacking()));
}

TEST_F(CompositedLayerMappingTest, 3D90DegRotatedTallInterestRect)
{
    // It's rotated 90 degrees about the X axis, which means its visual content rect is empty, and so the interest rect is the
    // default (0, 0, 4000, 4000) intersected with the layer bounds.
    setBodyInnerHTML(
        "<div id='target' style='width: 200px; height: 10000px; will-change: transform; transform: rotateY(90deg)'></div>");

    document().view()->updateAllLifecyclePhases();
    Element* element = document().getElementById("target");
    PaintLayer* paintLayer = toLayoutBoxModelObject(element->layoutObject())->layer();
    ASSERT_TRUE(!!paintLayer->graphicsLayerBacking());
    EXPECT_RECT_EQ(IntRect(0, 0, 200, 4000), recomputeInterestRect(paintLayer->graphicsLayerBacking()));
}

TEST_F(CompositedLayerMappingTest, 3D45DegRotatedTallInterestRect)
{
    setBodyInnerHTML(
        "<div id='target' style='width: 200px; height: 10000px; will-change: transform; transform: rotateY(45deg)'></div>");

    document().view()->updateAllLifecyclePhases();
    Element* element = document().getElementById("target");
    PaintLayer* paintLayer = toLayoutBoxModelObject(element->layoutObject())->layer();
    ASSERT_TRUE(!!paintLayer->graphicsLayerBacking());
    EXPECT_RECT_EQ(IntRect(0, 0, 200, 4592), recomputeInterestRect(paintLayer->graphicsLayerBacking()));
}

TEST_F(CompositedLayerMappingTest, RotatedTallInterestRect)
{
    setBodyInnerHTML(
        "<div id='target' style='width: 200px; height: 10000px; will-change: transform; transform: rotateZ(45deg)'></div>");

    document().view()->updateAllLifecyclePhases();
    Element* element = document().getElementById("target");
    PaintLayer* paintLayer = toLayoutBoxModelObject(element->layoutObject())->layer();
    ASSERT_TRUE(!!paintLayer->graphicsLayerBacking());
    EXPECT_RECT_EQ(IntRect(0, 0, 200, 4000), recomputeInterestRect(paintLayer->graphicsLayerBacking()));
}

TEST_F(CompositedLayerMappingTest, WideLayerInterestRect)
{
    setBodyInnerHTML("<div id='target' style='width: 10000px; height: 200px; will-change: transform'></div>");

    document().view()->updateAllLifecyclePhases();
    Element* element = document().getElementById("target");
    PaintLayer* paintLayer = toLayoutBoxModelObject(element->layoutObject())->layer();
    ASSERT_TRUE(!!paintLayer->graphicsLayerBacking());
    // Screen-space visible content rect is [8, 8, 800, 200] (the screen is 800x600).
    // Mapping back to local, adding 4000px in all directions, then clipping, yields this rect.
    EXPECT_RECT_EQ(IntRect(0, 0, 4792, 200), recomputeInterestRect(paintLayer->graphicsLayerBacking()));
}

TEST_F(CompositedLayerMappingTest, FixedPositionInterestRect)
{
    setBodyInnerHTML(
        "<div id='target' style='width: 300px; height: 400px; will-change: transform; position: fixed; top: 100px; left: 200px;'></div>");

    document().view()->updateAllLifecyclePhases();
    Element* element = document().getElementById("target");
    PaintLayer* paintLayer = toLayoutBoxModelObject(element->layoutObject())->layer();
    ASSERT_TRUE(!!paintLayer->graphicsLayerBacking());
    EXPECT_RECT_EQ(IntRect(0, 0, 300, 400), recomputeInterestRect(paintLayer->graphicsLayerBacking()));
}

TEST_F(CompositedLayerMappingTest, LayerOffscreenInterestRect)
{
    setBodyInnerHTML(
        "<div id='target' style='width: 200px; height: 200px; will-change: transform; position: absolute; top: 9000px; left: 0px;'>"
        "</div>");

    document().view()->updateAllLifecyclePhases();
    Element* element = document().getElementById("target");
    PaintLayer* paintLayer = toLayoutBoxModelObject(element->layoutObject())->layer();
    ASSERT_TRUE(!!paintLayer->graphicsLayerBacking());
    // Offscreen layers are painted as usual.
    EXPECT_RECT_EQ(IntRect(0, 0, 200, 200), recomputeInterestRect(paintLayer->graphicsLayerBacking()));
}

TEST_F(CompositedLayerMappingTest, ScrollingLayerInterestRect)
{
    setBodyInnerHTML(
        "<style>div::-webkit-scrollbar{ width: 5px; }</style>"
        "<div id='target' style='width: 200px; height: 200px; will-change: transform; overflow: scroll'>"
        "<div style='width: 100px; height: 10000px'></div></div>");

    document().view()->updateAllLifecyclePhases();
    Element* element = document().getElementById("target");
    PaintLayer* paintLayer = toLayoutBoxModelObject(element->layoutObject())->layer();
    ASSERT_TRUE(paintLayer->graphicsLayerBacking());
    // Offscreen layers are painted as usual.
    ASSERT_TRUE(paintLayer->compositedLayerMapping()->scrollingLayer());
    EXPECT_RECT_EQ(IntRect(0, 0, 195, 4592), recomputeInterestRect(paintLayer->graphicsLayerBackingForScrolling()));
}

TEST_F(CompositedLayerMappingTest, ClippedBigLayer)
{
    setBodyInnerHTML(
        "<div style='width: 1px; height: 1px; overflow: hidden'>"
        "<div id='target' style='width: 10000px; height: 10000px; will-change: transform'></div></div>");

    document().view()->updateAllLifecyclePhases();
    Element* element = document().getElementById("target");
    PaintLayer* paintLayer = toLayoutBoxModelObject(element->layoutObject())->layer();
    ASSERT_TRUE(paintLayer->graphicsLayerBacking());
    // Offscreen layers are painted as usual.
    EXPECT_RECT_EQ(IntRect(0, 0, 4001, 4001), recomputeInterestRect(paintLayer->graphicsLayerBacking()));
}

TEST_F(CompositedLayerMappingTest, ClippingMaskLayer)
{
    if (RuntimeEnabledFeatures::slimmingPaintV2Enabled())
        return;

    const AtomicString styleWithoutClipping = "backface-visibility: hidden; width: 200px; height: 200px";
    const AtomicString styleWithBorderRadius = styleWithoutClipping + "; border-radius: 10px";
    const AtomicString styleWithClipPath = styleWithoutClipping + "; -webkit-clip-path: inset(10px)";

    setBodyInnerHTML("<video id='video' src='x' style='" + styleWithoutClipping + "'></video>");

    document().view()->updateAllLifecyclePhases();
    Element* videoElement = document().getElementById("video");
    GraphicsLayer* graphicsLayer = toLayoutBoxModelObject(videoElement->layoutObject())->layer()->graphicsLayerBacking();
    EXPECT_FALSE(graphicsLayer->maskLayer());
    EXPECT_FALSE(graphicsLayer->contentsClippingMaskLayer());

    videoElement->setAttribute(HTMLNames::styleAttr, styleWithBorderRadius);
    document().view()->updateAllLifecyclePhases();
    EXPECT_FALSE(graphicsLayer->maskLayer());
    EXPECT_TRUE(graphicsLayer->contentsClippingMaskLayer());

    videoElement->setAttribute(HTMLNames::styleAttr, styleWithClipPath);
    document().view()->updateAllLifecyclePhases();
    EXPECT_TRUE(graphicsLayer->maskLayer());
    EXPECT_FALSE(graphicsLayer->contentsClippingMaskLayer());

    videoElement->setAttribute(HTMLNames::styleAttr, styleWithoutClipping);
    document().view()->updateAllLifecyclePhases();
    EXPECT_FALSE(graphicsLayer->maskLayer());
    EXPECT_FALSE(graphicsLayer->contentsClippingMaskLayer());
}

TEST_F(CompositedLayerMappingTest, InterestRectChangedEnoughToRepaintEmpty)
{
    IntSize layerSize(1000, 1000);
    // Both empty means there is nothing to do.
    EXPECT_FALSE(interestRectChangedEnoughToRepaint(IntRect(), IntRect(), layerSize));
    // Going from empty to non-empty means we must re-record because it could be the first frame after construction or Clear.
    EXPECT_TRUE(interestRectChangedEnoughToRepaint(IntRect(), IntRect(0, 0, 1, 1), layerSize));
    // Going from non-empty to empty is not special-cased.
    EXPECT_FALSE(interestRectChangedEnoughToRepaint(IntRect(0, 0, 1, 1), IntRect(), layerSize));
}

TEST_F(CompositedLayerMappingTest, InterestRectChangedEnoughToRepaintNotBigEnough)
{
    IntSize layerSize(1000, 1000);
    IntRect previousInterestRect(100, 100, 100, 100);
    EXPECT_FALSE(interestRectChangedEnoughToRepaint(previousInterestRect, IntRect(100, 100, 90, 90), layerSize));
    EXPECT_FALSE(interestRectChangedEnoughToRepaint(previousInterestRect, IntRect(100, 100, 100, 100), layerSize));
    EXPECT_FALSE(interestRectChangedEnoughToRepaint(previousInterestRect, IntRect(1, 1, 200, 200), layerSize));
}

TEST_F(CompositedLayerMappingTest, InterestRectChangedEnoughToRepaintNotBigEnoughButNewAreaTouchesEdge)
{
    IntSize layerSize(500, 500);
    IntRect previousInterestRect(100, 100, 100, 100);
    // Top edge.
    EXPECT_TRUE(interestRectChangedEnoughToRepaint(previousInterestRect, IntRect(100, 0, 100, 200), layerSize));
    // Left edge.
    EXPECT_TRUE(interestRectChangedEnoughToRepaint(previousInterestRect, IntRect(0, 100, 200, 100), layerSize));
    // Bottom edge.
    EXPECT_TRUE(interestRectChangedEnoughToRepaint(previousInterestRect, IntRect(100, 100, 100, 400), layerSize));
    // Right edge.
    EXPECT_TRUE(interestRectChangedEnoughToRepaint(previousInterestRect, IntRect(100, 100, 400, 100), layerSize));
}

// Verifies that having a current viewport that touches a layer edge does not
// force re-recording.
TEST_F(CompositedLayerMappingTest, InterestRectChangedEnoughToRepaintCurrentViewportTouchesEdge)
{
    IntSize layerSize(500, 500);
    IntRect newInterestRect(100, 100, 300, 300);
    // Top edge.
    EXPECT_FALSE(interestRectChangedEnoughToRepaint(IntRect(100, 0, 100, 100), newInterestRect, layerSize));
    // Left edge.
    EXPECT_FALSE(interestRectChangedEnoughToRepaint(IntRect(0, 100, 100, 100), newInterestRect, layerSize));
    // Bottom edge.
    EXPECT_FALSE(interestRectChangedEnoughToRepaint(IntRect(300, 400, 100, 100), newInterestRect, layerSize));
    // Right edge.
    EXPECT_FALSE(interestRectChangedEnoughToRepaint(IntRect(400, 300, 100, 100), newInterestRect, layerSize));
}

TEST_F(CompositedLayerMappingTest, InterestRectChangedEnoughToRepaintScrollScenarios)
{
    IntSize layerSize(1000, 1000);
    IntRect previousInterestRect(100, 100, 100, 100);
    IntRect newInterestRect(previousInterestRect);
    newInterestRect.move(512, 0);
    EXPECT_FALSE(interestRectChangedEnoughToRepaint(previousInterestRect, newInterestRect, layerSize));
    newInterestRect.move(0, 512);
    EXPECT_FALSE(interestRectChangedEnoughToRepaint(previousInterestRect, newInterestRect, layerSize));
    newInterestRect.move(1, 0);
    EXPECT_TRUE(interestRectChangedEnoughToRepaint(previousInterestRect, newInterestRect, layerSize));
    newInterestRect.move(-1, 1);
    EXPECT_TRUE(interestRectChangedEnoughToRepaint(previousInterestRect, newInterestRect, layerSize));
}

TEST_F(CompositedLayerMappingTest, InterestRectChangeOnViewportScroll)
{
    if (document().frame()->settings()->rootLayerScrolls())
        return;

    setBodyInnerHTML(
        "<style>"
        "  ::-webkit-scrollbar { width: 0; height: 0; }"
        "  body { margin: 0; }"
        "</style>"
        "<div id='div' style='width: 100px; height: 10000px'>Text</div>");

    document().view()->updateAllLifecyclePhases();
    GraphicsLayer* rootScrollingLayer = document().layoutView()->layer()->graphicsLayerBackingForScrolling();
    EXPECT_RECT_EQ(IntRect(0, 0, 800, 4600), previousInterestRect(rootScrollingLayer));

    document().view()->setScrollPosition(IntPoint(0, 300), ProgrammaticScroll);
    document().view()->updateAllLifecyclePhases();
    // Still use the previous interest rect because the recomputed rect hasn't changed enough.
    EXPECT_RECT_EQ(IntRect(0, 0, 800, 4900), recomputeInterestRect(rootScrollingLayer));
    EXPECT_RECT_EQ(IntRect(0, 0, 800, 4600), previousInterestRect(rootScrollingLayer));

    document().view()->setScrollPosition(IntPoint(0, 600), ProgrammaticScroll);
    document().view()->updateAllLifecyclePhases();
    // Use recomputed interest rect because it changed enough.
    EXPECT_RECT_EQ(IntRect(0, 0, 800, 5200), recomputeInterestRect(rootScrollingLayer));
    EXPECT_RECT_EQ(IntRect(0, 0, 800, 5200), previousInterestRect(rootScrollingLayer));

    document().view()->setScrollPosition(IntPoint(0, 5400), ProgrammaticScroll);
    document().view()->updateAllLifecyclePhases();
    EXPECT_RECT_EQ(IntRect(0, 1400, 800, 8600), recomputeInterestRect(rootScrollingLayer));
    EXPECT_RECT_EQ(IntRect(0, 1400, 800, 8600), previousInterestRect(rootScrollingLayer));

    document().view()->setScrollPosition(IntPoint(0, 9000), ProgrammaticScroll);
    document().view()->updateAllLifecyclePhases();
    // Still use the previous interest rect because it contains the recomputed interest rect.
    EXPECT_RECT_EQ(IntRect(0, 5000, 800, 5000), recomputeInterestRect(rootScrollingLayer));
    EXPECT_RECT_EQ(IntRect(0, 1400, 800, 8600), previousInterestRect(rootScrollingLayer));

    document().view()->setScrollPosition(IntPoint(0, 2000), ProgrammaticScroll);
    // Use recomputed interest rect because it changed enough.
    document().view()->updateAllLifecyclePhases();
    EXPECT_RECT_EQ(IntRect(0, 0, 800, 6600), recomputeInterestRect(rootScrollingLayer));
    EXPECT_RECT_EQ(IntRect(0, 0, 800, 6600), previousInterestRect(rootScrollingLayer));
}

TEST_F(CompositedLayerMappingTest, InterestRectChangeOnScroll)
{
    document().frame()->settings()->setPreferCompositingToLCDTextEnabled(true);

    setBodyInnerHTML(
        "<style>"
        "  ::-webkit-scrollbar { width: 0; height: 0; }"
        "  body { margin: 0; }"
        "</style>"
        "<div id='scroller' style='width: 400px; height: 400px; overflow: scroll'>"
        "  <div id='content' style='width: 100px; height: 10000px'>Text</div>"
        "</div");

    document().view()->updateAllLifecyclePhases();
    Element* scroller = document().getElementById("scroller");
    GraphicsLayer* scrollingLayer = scroller->layoutBox()->layer()->graphicsLayerBackingForScrolling();
    EXPECT_RECT_EQ(IntRect(0, 0, 400, 4600), previousInterestRect(scrollingLayer));

    scroller->setScrollTop(300);
    document().view()->updateAllLifecyclePhases();
    // Still use the previous interest rect because the recomputed rect hasn't changed enough.
    EXPECT_RECT_EQ(IntRect(0, 0, 400, 4900), recomputeInterestRect(scrollingLayer));
    EXPECT_RECT_EQ(IntRect(0, 0, 400, 4600), previousInterestRect(scrollingLayer));

    scroller->setScrollTop(600);
    document().view()->updateAllLifecyclePhases();
    // Use recomputed interest rect because it changed enough.
    EXPECT_RECT_EQ(IntRect(0, 0, 400, 5200), recomputeInterestRect(scrollingLayer));
    EXPECT_RECT_EQ(IntRect(0, 0, 400, 5200), previousInterestRect(scrollingLayer));

    scroller->setScrollTop(5400);
    document().view()->updateAllLifecyclePhases();
    EXPECT_RECT_EQ(IntRect(0, 1400, 400, 8600), recomputeInterestRect(scrollingLayer));
    EXPECT_RECT_EQ(IntRect(0, 1400, 400, 8600), previousInterestRect(scrollingLayer));

    scroller->setScrollTop(9000);
    document().view()->updateAllLifecyclePhases();
    // Still use the previous interest rect because it contains the recomputed interest rect.
    EXPECT_RECT_EQ(IntRect(0, 5000, 400, 5000), recomputeInterestRect(scrollingLayer));
    EXPECT_RECT_EQ(IntRect(0, 1400, 400, 8600), previousInterestRect(scrollingLayer));

    scroller->setScrollTop(2000);
    // Use recomputed interest rect because it changed enough.
    document().view()->updateAllLifecyclePhases();
    EXPECT_RECT_EQ(IntRect(0, 0, 400, 6600), recomputeInterestRect(scrollingLayer));
    EXPECT_RECT_EQ(IntRect(0, 0, 400, 6600), previousInterestRect(scrollingLayer));
}

TEST_F(CompositedLayerMappingTest, InterestRectShouldNotChangeOnPaintInvalidation)
{
    document().frame()->settings()->setPreferCompositingToLCDTextEnabled(true);

    setBodyInnerHTML(
        "<style>"
        "  ::-webkit-scrollbar { width: 0; height: 0; }"
        "  body { margin: 0; }"
        "</style>"
        "<div id='scroller' style='width: 400px; height: 400px; overflow: scroll'>"
        "  <div id='content' style='width: 100px; height: 10000px'>Text</div>"
        "</div");

    document().view()->updateAllLifecyclePhases();
    Element* scroller = document().getElementById("scroller");
    GraphicsLayer* scrollingLayer = scroller->layoutBox()->layer()->graphicsLayerBackingForScrolling();

    scroller->setScrollTop(5400);
    document().view()->updateAllLifecyclePhases();
    scroller->setScrollTop(9400);
    // The above code creates an interest rect bigger than the interest rect if recomputed now.
    document().view()->updateAllLifecyclePhases();
    EXPECT_RECT_EQ(IntRect(0, 5400, 400, 4600), recomputeInterestRect(scrollingLayer));
    EXPECT_RECT_EQ(IntRect(0, 1400, 400, 8600), previousInterestRect(scrollingLayer));

    // Paint invalidation and repaint should not change previous paint interest rect.
    document().getElementById("content")->setTextContent("Change");
    document().view()->updateAllLifecyclePhases();
    EXPECT_RECT_EQ(IntRect(0, 5400, 400, 4600), recomputeInterestRect(scrollingLayer));
    EXPECT_RECT_EQ(IntRect(0, 1400, 400, 8600), previousInterestRect(scrollingLayer));
}

TEST_F(CompositedLayerMappingTest, InterestRectOfSquashingLayerWithNegativeOverflow)
{
    setBodyInnerHTML(
        "<style>body { margin: 0 }</style>"
        "<div style='position: absolute; top: -500px; width: 200px; height: 700px; will-change: transform'></div>"
        "<div id='squashed' style='position: absolute; top: 190px'>"
        "  <div style='width: 100px; height: 100px; text-indent: -10000px'>text</div>"
        "</div>");

    CompositedLayerMapping* groupedMapping = document().getElementById("squashed")->layoutBox()->layer()->groupedMapping();
    // The squashing layer is at (-10000, 190, 10100, 100) in viewport coordinates.
    // The following rect is at (-4000, 190, 4100, 100) in viewport coordinates.
    EXPECT_RECT_EQ(IntRect(6000, 0, 4100, 100), groupedMapping->computeInterestRect(groupedMapping->squashingLayer(), IntRect()));
}

TEST_F(CompositedLayerMappingTest, InterestRectOfSquashingLayerWithAncestorClip)
{
    setBodyInnerHTML(
        "<style>body { margin: 0; }</style>"
        "<div style='overflow: hidden; width: 400px; height: 400px'>"
        "  <div style='position: relative; backface-visibility: hidden'>"
        "    <div style='position: absolute; top: -500px; width: 200px; height: 700px; backface-visibility: hidden'></div>"
        // Above overflow:hidden div and two composited layers make the squashing layer a child of an ancestor clipping layer.
        "    <div id='squashed' style='height: 1000px; width: 10000px; right: 0; position: absolute'></div>"
        "  </div>"
        "</div>");

    CompositedLayerMapping* groupedMapping = document().getElementById("squashed")->layoutBox()->layer()->groupedMapping();
    // The squashing layer is at (-9600, 0, 10000, 1000) in viewport coordinates.
    // The following rect is at (-4000, 0, 4400, 1000) in viewport coordinates.
    EXPECT_RECT_EQ(IntRect(5600, 0, 4400, 1000), groupedMapping->computeInterestRect(groupedMapping->squashingLayer(), IntRect()));
}

TEST_F(CompositedLayerMappingTest, InterestRectOfScrolledIframe)
{
    document().setBaseURLOverride(KURL(ParsedURLString, "http://test.com"));
    setBodyInnerHTML(
        "<style>body { margin: 0; }</style>"
        "<div style='width: 200; height: 8000px'></div>"
        "<iframe id=frame src='http://test.com' width='500' height='500' frameBorder='0'>"
        "</iframe>");

    HTMLIFrameElement& iframe = *toHTMLIFrameElement(document().getElementById("frame"));
    OwnPtrWillBeRawPtr<FrameLoaderClient> frameLoaderClient = FrameLoaderClientWithParent::create(document().frame());
    RefPtrWillBePersistent<LocalFrame> subframe = LocalFrame::create(frameLoaderClient.get(), document().frame()->host(), &iframe);
    subframe->setView(FrameView::create(subframe.get(), IntSize(500, 500)));
    subframe->init();
    static_cast<SingleChildFrameLoaderClient*>(document().frame()->client())->setChild(subframe.get());
    document().frame()->host()->incrementSubframeCount();
    Document& frameDocument = *iframe.contentDocument();

    frameDocument.setBaseURLOverride(KURL(ParsedURLString, "http://test.com"));
    frameDocument.body()->setInnerHTML("<style>body { margin: 0; } #target { width: 200px; height: 200px; will-change: transform}</style><div id=target></div>",
        ASSERT_NO_EXCEPTION);

    // Scroll 8000 pixels down to move the iframe into view.
    document().view()->setScrollPosition(DoublePoint(0.0, 8000.0), ProgrammaticScroll);
    document().view()->updateAllLifecyclePhases();

    Element* target = frameDocument.getElementById("target");
    ASSERT_TRUE(target);

    EXPECT_RECT_EQ(IntRect(0, 0, 200, 200), recomputeInterestRect(target->layoutObject()->enclosingLayer()->graphicsLayerBacking()));

    subframe->detach(FrameDetachType::Remove);
    static_cast<SingleChildFrameLoaderClient*>(document().frame()->client())->setChild(nullptr);
    document().frame()->host()->decrementSubframeCount();
}

} // namespace blink
