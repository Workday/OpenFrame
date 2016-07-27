// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/frame/FrameView.h"
#include "core/html/HTMLIFrameElement.h"
#include "platform/testing/UnitTestHelpers.h"
#include "public/web/WebHitTestResult.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "web/tests/sim/SimCompositor.h"
#include "web/tests/sim/SimDisplayItemList.h"
#include "web/tests/sim/SimRequest.h"
#include "web/tests/sim/SimTest.h"

namespace blink {

using namespace HTMLNames;

// NOTE: This test uses <iframe sandbox> to create cross origin iframes.

class FrameThrottlingTest : public SimTest {
protected:
    FrameThrottlingTest()
    {
        webView().resize(WebSize(640, 480));
    }

    SimDisplayItemList compositeFrame()
    {
        SimDisplayItemList displayItems = compositor().beginFrame();
        // Ensure intersection observer notifications get delivered.
        testing::runPendingTasks();
        return displayItems;
    }
};

TEST_F(FrameThrottlingTest, ThrottleInvisibleFrames)
{
    SimRequest mainResource("https://example.com/", "text/html");

    loadURL("https://example.com/");
    mainResource.complete("<iframe sandbox id=frame></iframe>");

    auto* frameElement = toHTMLIFrameElement(document().getElementById("frame"));
    auto* frameDocument = frameElement->contentDocument();

    // Initially both frames are visible.
    EXPECT_FALSE(document().view()->isHiddenForThrottling());
    EXPECT_FALSE(frameDocument->view()->isHiddenForThrottling());

    // Moving the child fully outside the parent makes it invisible.
    frameElement->setAttribute(styleAttr, "transform: translateY(480px)");
    compositeFrame();
    EXPECT_FALSE(document().view()->isHiddenForThrottling());
    EXPECT_TRUE(frameDocument->view()->isHiddenForThrottling());

    // A partially visible child is considered visible.
    frameElement->setAttribute(styleAttr, "transform: translate(-50px, 0px, 0px)");
    compositeFrame();
    EXPECT_FALSE(document().view()->isHiddenForThrottling());
    EXPECT_FALSE(frameDocument->view()->isHiddenForThrottling());
}

TEST_F(FrameThrottlingTest, ViewportVisibilityFullyClipped)
{
    SimRequest mainResource("https://example.com/", "text/html");

    loadURL("https://example.com/");
    mainResource.complete("<iframe sandbox id=frame></iframe>");

    // A child which is fully clipped away by its ancestor should become invisible.
    webView().resize(WebSize(0, 0));
    compositeFrame();

    EXPECT_TRUE(document().view()->isHiddenForThrottling());

    auto* frameElement = toHTMLIFrameElement(document().getElementById("frame"));
    auto* frameDocument = frameElement->contentDocument();
    EXPECT_TRUE(frameDocument->view()->isHiddenForThrottling());
}

TEST_F(FrameThrottlingTest, HiddenSameOriginFramesAreNotThrottled)
{
    SimRequest mainResource("https://example.com/", "text/html");
    SimRequest frameResource("https://example.com/iframe.html", "text/html");

    loadURL("https://example.com/");
    mainResource.complete("<iframe id=frame src=iframe.html></iframe>");
    frameResource.complete("<iframe id=innerFrame></iframe>");

    auto* frameElement = toHTMLIFrameElement(document().getElementById("frame"));
    auto* frameDocument = frameElement->contentDocument();

    HTMLIFrameElement* innerFrameElement = toHTMLIFrameElement(frameDocument->getElementById("innerFrame"));
    auto* innerFrameDocument = innerFrameElement->contentDocument();

    EXPECT_FALSE(document().view()->canThrottleRendering());
    EXPECT_FALSE(frameDocument->view()->canThrottleRendering());
    EXPECT_FALSE(innerFrameDocument->view()->canThrottleRendering());

    // Hidden same origin frames are not throttled.
    frameElement->setAttribute(styleAttr, "transform: translateY(480px)");
    compositeFrame();
    EXPECT_FALSE(document().view()->canThrottleRendering());
    EXPECT_FALSE(frameDocument->view()->canThrottleRendering());
    EXPECT_FALSE(innerFrameDocument->view()->canThrottleRendering());
}

TEST_F(FrameThrottlingTest, HiddenCrossOriginFramesAreThrottled)
{
    // Create a document with doubly nested iframes.
    SimRequest mainResource("https://example.com/", "text/html");
    SimRequest frameResource("https://example.com/iframe.html", "text/html");

    loadURL("https://example.com/");
    mainResource.complete("<iframe id=frame src=iframe.html></iframe>");
    frameResource.complete("<iframe id=innerFrame sandbox></iframe>");

    auto* frameElement = toHTMLIFrameElement(document().getElementById("frame"));
    auto* frameDocument = frameElement->contentDocument();

    auto* innerFrameElement = toHTMLIFrameElement(frameDocument->getElementById("innerFrame"));
    auto* innerFrameDocument = innerFrameElement->contentDocument();

    EXPECT_FALSE(document().view()->canThrottleRendering());
    EXPECT_FALSE(frameDocument->view()->canThrottleRendering());
    EXPECT_FALSE(innerFrameDocument->view()->canThrottleRendering());

    // Hidden cross origin frames are throttled.
    frameElement->setAttribute(styleAttr, "transform: translateY(480px)");
    compositeFrame();
    EXPECT_FALSE(document().view()->canThrottleRendering());
    EXPECT_FALSE(frameDocument->view()->canThrottleRendering());
    EXPECT_TRUE(innerFrameDocument->view()->canThrottleRendering());
}

TEST_F(FrameThrottlingTest, ThrottledLifecycleUpdate)
{
    SimRequest mainResource("https://example.com/", "text/html");

    loadURL("https://example.com/");
    mainResource.complete("<iframe sandbox id=frame></iframe>");

    auto* frameElement = toHTMLIFrameElement(document().getElementById("frame"));
    auto* frameDocument = frameElement->contentDocument();

    // Enable throttling for the child frame.
    frameElement->setAttribute(styleAttr, "transform: translateY(480px)");
    compositeFrame();
    EXPECT_TRUE(frameDocument->view()->canThrottleRendering());
    if (RuntimeEnabledFeatures::slimmingPaintSynchronizedPaintingEnabled())
        EXPECT_EQ(DocumentLifecycle::PaintClean, frameDocument->lifecycle().state());
    else
        EXPECT_EQ(DocumentLifecycle::PaintInvalidationClean, frameDocument->lifecycle().state());

    // Mutating the throttled frame followed by a beginFrame will not result in
    // a complete lifecycle update.
    frameElement->setAttribute(widthAttr, "50");
    compositeFrame();
    EXPECT_EQ(DocumentLifecycle::StyleClean, frameDocument->lifecycle().state());

    // A hit test will force a complete lifecycle update.
    webView().hitTestResultAt(WebPoint(0, 0));
    EXPECT_EQ(DocumentLifecycle::CompositingClean, frameDocument->lifecycle().state());
}

TEST_F(FrameThrottlingTest, UnthrottlingFrameSchedulesAnimation)
{
    SimRequest mainResource("https://example.com/", "text/html");

    loadURL("https://example.com/");
    mainResource.complete("<iframe sandbox id=frame></iframe>");

    auto* frameElement = toHTMLIFrameElement(document().getElementById("frame"));

    // First make the child hidden to enable throttling.
    frameElement->setAttribute(styleAttr, "transform: translateY(480px)");
    compositeFrame();
    EXPECT_TRUE(frameElement->contentDocument()->view()->canThrottleRendering());
    EXPECT_FALSE(compositor().needsAnimate());

    // Then bring it back on-screen. This should schedule an animation update.
    frameElement->setAttribute(styleAttr, "");
    compositeFrame();
    EXPECT_TRUE(compositor().needsAnimate());
}

TEST_F(FrameThrottlingTest, MutatingThrottledFrameDoesNotCauseAnimation)
{
    SimRequest mainResource("https://example.com/", "text/html");
    SimRequest frameResource("https://example.com/iframe.html", "text/html");

    loadURL("https://example.com/");
    mainResource.complete("<iframe id=frame sandbox src=iframe.html></iframe>");
    frameResource.complete("<style> html { background: red; } </style>");

    // Check that the frame initially shows up.
    auto displayItems1 = compositeFrame();
    EXPECT_TRUE(displayItems1.contains(SimCanvas::Rect, "red"));

    auto* frameElement = toHTMLIFrameElement(document().getElementById("frame"));

    // Move the frame offscreen to throttle it.
    frameElement->setAttribute(styleAttr, "transform: translateY(480px)");
    compositeFrame();
    EXPECT_TRUE(frameElement->contentDocument()->view()->canThrottleRendering());

    // Mutating the throttled frame should not cause an animation to be scheduled.
    frameElement->contentDocument()->documentElement()->setAttribute(styleAttr, "background: green");
    EXPECT_FALSE(compositor().needsAnimate());

    // Moving the frame back on screen to unthrottle it.
    frameElement->setAttribute(styleAttr, "");
    EXPECT_TRUE(compositor().needsAnimate());

    // The first frame we composite after unthrottling won't contain the
    // frame's new contents because unthrottling happens at the end of the
    // lifecycle update. We need to do another composite to refresh the frame's
    // contents.
    auto displayItems2 = compositeFrame();
    EXPECT_FALSE(displayItems2.contains(SimCanvas::Rect, "green"));
    EXPECT_TRUE(compositor().needsAnimate());

    auto displayItems3 = compositeFrame();
    EXPECT_TRUE(displayItems3.contains(SimCanvas::Rect, "green"));
}

TEST_F(FrameThrottlingTest, SynchronousLayoutInThrottledFrame)
{
    // Create a hidden frame which is throttled.
    SimRequest mainResource("https://example.com/", "text/html");
    SimRequest frameResource("https://example.com/iframe.html", "text/html");

    loadURL("https://example.com/");
    mainResource.complete("<iframe id=frame sandbox src=iframe.html></iframe>");
    frameResource.complete("<div id=div></div>");

    auto* frameElement = toHTMLIFrameElement(document().getElementById("frame"));

    frameElement->setAttribute(styleAttr, "transform: translateY(480px)");
    compositeFrame();

    // Change the size of a div in the throttled frame.
    auto* divElement = frameElement->contentDocument()->getElementById("div");
    divElement->setAttribute(styleAttr, "width: 50px");

    // Querying the width of the div should do a synchronous layout update even
    // though the frame is being throttled.
    EXPECT_EQ(50, divElement->clientWidth());
}

} // namespace blink
