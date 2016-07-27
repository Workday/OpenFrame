// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "web/TextFinder.h"

#include "bindings/core/v8/ExceptionStatePlaceholder.h"
#include "core/dom/Document.h"
#include "core/dom/NodeList.h"
#include "core/dom/Range.h"
#include "core/dom/shadow/ShadowRoot.h"
#include "core/frame/FrameHost.h"
#include "core/html/HTMLElement.h"
#include "core/layout/TextAutosizer.h"
#include "core/page/Page.h"
#include "platform/testing/TestingPlatformSupport.h"
#include "platform/testing/UnitTestHelpers.h"
#include "public/platform/Platform.h"
#include "public/web/WebDocument.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "web/FindInPageCoordinates.h"
#include "web/WebLocalFrameImpl.h"
#include "web/tests/FrameTestHelpers.h"
#include "wtf/OwnPtr.h"

using blink::testing::runPendingTasks;

namespace blink {

class TextFinderTest : public ::testing::Test {
protected:
    TextFinderTest()
    {
        m_webViewHelper.initialize();
        WebLocalFrameImpl& frameImpl = *m_webViewHelper.webViewImpl()->mainFrameImpl();
        frameImpl.viewImpl()->resize(WebSize(640, 480));
        frameImpl.viewImpl()->updateAllLifecyclePhases();
        m_document = PassRefPtrWillBeRawPtr<Document>(frameImpl.document());
        m_textFinder = &frameImpl.ensureTextFinder();
    }

    Document& document() const;
    TextFinder& textFinder() const;

    static WebFloatRect findInPageRect(Node* startContainer, int startOffset, Node* endContainer, int endOffset);

private:
    FrameTestHelpers::WebViewHelper m_webViewHelper;
    RefPtrWillBePersistent<Document> m_document;
    RawPtrWillBePersistent<TextFinder> m_textFinder;
};

Document& TextFinderTest::document() const
{
    return *m_document;
}

TextFinder& TextFinderTest::textFinder() const
{
    return *m_textFinder;
}

WebFloatRect TextFinderTest::findInPageRect(Node* startContainer, int startOffset, Node* endContainer, int endOffset)
{
    RefPtrWillBeRawPtr<Range> range = Range::create(startContainer->document(), startContainer, startOffset, endContainer, endOffset);
    return WebFloatRect(findInPageRectFromRange(range.get()));
}

TEST_F(TextFinderTest, FindTextSimple)
{
    document().body()->setInnerHTML("XXXXFindMeYYYYfindmeZZZZ", ASSERT_NO_EXCEPTION);
    Node* textNode = document().body()->firstChild();

    int identifier = 0;
    WebString searchText(String("FindMe"));
    WebFindOptions findOptions; // Default.
    bool wrapWithinFrame = true;
    WebRect* selectionRect = nullptr;

    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    Range* activeMatch = textFinder().activeMatch();
    ASSERT_TRUE(activeMatch);
    EXPECT_EQ(textNode, activeMatch->startContainer());
    EXPECT_EQ(4, activeMatch->startOffset());
    EXPECT_EQ(textNode, activeMatch->endContainer());
    EXPECT_EQ(10, activeMatch->endOffset());

    findOptions.findNext = true;
    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    activeMatch = textFinder().activeMatch();
    ASSERT_TRUE(activeMatch);
    EXPECT_EQ(textNode, activeMatch->startContainer());
    EXPECT_EQ(14, activeMatch->startOffset());
    EXPECT_EQ(textNode, activeMatch->endContainer());
    EXPECT_EQ(20, activeMatch->endOffset());

    // Should wrap to the first match.
    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    activeMatch = textFinder().activeMatch();
    ASSERT_TRUE(activeMatch);
    EXPECT_EQ(textNode, activeMatch->startContainer());
    EXPECT_EQ(4, activeMatch->startOffset());
    EXPECT_EQ(textNode, activeMatch->endContainer());
    EXPECT_EQ(10, activeMatch->endOffset());

    // Search in the reverse order.
    identifier = 1;
    findOptions = WebFindOptions();
    findOptions.forward = false;

    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    activeMatch = textFinder().activeMatch();
    ASSERT_TRUE(activeMatch);
    EXPECT_EQ(textNode, activeMatch->startContainer());
    EXPECT_EQ(14, activeMatch->startOffset());
    EXPECT_EQ(textNode, activeMatch->endContainer());
    EXPECT_EQ(20, activeMatch->endOffset());

    findOptions.findNext = true;
    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    activeMatch = textFinder().activeMatch();
    ASSERT_TRUE(activeMatch);
    EXPECT_EQ(textNode, activeMatch->startContainer());
    EXPECT_EQ(4, activeMatch->startOffset());
    EXPECT_EQ(textNode, activeMatch->endContainer());
    EXPECT_EQ(10, activeMatch->endOffset());

    // Wrap to the first match (last occurence in the document).
    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    activeMatch = textFinder().activeMatch();
    ASSERT_TRUE(activeMatch);
    EXPECT_EQ(textNode, activeMatch->startContainer());
    EXPECT_EQ(14, activeMatch->startOffset());
    EXPECT_EQ(textNode, activeMatch->endContainer());
    EXPECT_EQ(20, activeMatch->endOffset());
}

TEST_F(TextFinderTest, FindTextAutosizing)
{
    document().body()->setInnerHTML("XXXXFindMeYYYYfindmeZZZZ", ASSERT_NO_EXCEPTION);

    int identifier = 0;
    WebString searchText(String("FindMe"));
    WebFindOptions findOptions; // Default.
    bool wrapWithinFrame = true;
    WebRect* selectionRect = nullptr;

    // Set viewport scale to 20 in order to simulate zoom-in
    VisualViewport& visualViewport = document().page()->frameHost().visualViewport();
    visualViewport.setScale(20);

    // Enforce autosizing
    document().settings()->setTextAutosizingEnabled(true);
    document().settings()->setTextAutosizingWindowSizeOverride(IntSize(20, 20));
    document().textAutosizer()->updatePageInfo();

    // In case of autosizing, scale _should_ change
    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    ASSERT_TRUE(textFinder().activeMatch());
    ASSERT_EQ(1, visualViewport.scale()); // in this case to 1

    // Disable autosizing and reset scale to 20
    visualViewport.setScale(20);
    document().settings()->setTextAutosizingEnabled(false);
    document().textAutosizer()->updatePageInfo();

    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    ASSERT_TRUE(textFinder().activeMatch());
    ASSERT_EQ(20, visualViewport.scale());
}

TEST_F(TextFinderTest, FindTextNotFound)
{
    document().body()->setInnerHTML("XXXXFindMeYYYYfindmeZZZZ", ASSERT_NO_EXCEPTION);

    int identifier = 0;
    WebString searchText(String("Boo"));
    WebFindOptions findOptions; // Default.
    bool wrapWithinFrame = true;
    WebRect* selectionRect = nullptr;

    EXPECT_FALSE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    EXPECT_FALSE(textFinder().activeMatch());
}

TEST_F(TextFinderTest, FindTextInShadowDOM)
{
    document().body()->setInnerHTML("<b>FOO</b><i>foo</i>", ASSERT_NO_EXCEPTION);
    RefPtrWillBeRawPtr<ShadowRoot> shadowRoot = document().body()->createShadowRootInternal(ShadowRootType::V0, ASSERT_NO_EXCEPTION);
    shadowRoot->setInnerHTML("<content select=\"i\"></content><u>Foo</u><content></content>", ASSERT_NO_EXCEPTION);
    Node* textInBElement = document().body()->firstChild()->firstChild();
    Node* textInIElement = document().body()->lastChild()->firstChild();
    Node* textInUElement = shadowRoot->childNodes()->item(1)->firstChild();

    int identifier = 0;
    WebString searchText(String("foo"));
    WebFindOptions findOptions; // Default.
    bool wrapWithinFrame = true;
    WebRect* selectionRect = nullptr;

    // TextIterator currently returns the matches in the composed treeorder, so
    // in this case the matches will be returned in the order of
    // <i> -> <u> -> <b>.
    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    Range* activeMatch = textFinder().activeMatch();
    ASSERT_TRUE(activeMatch);
    EXPECT_EQ(textInIElement, activeMatch->startContainer());
    EXPECT_EQ(0, activeMatch->startOffset());
    EXPECT_EQ(textInIElement, activeMatch->endContainer());
    EXPECT_EQ(3, activeMatch->endOffset());

    findOptions.findNext = true;
    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    activeMatch = textFinder().activeMatch();
    ASSERT_TRUE(activeMatch);
    EXPECT_EQ(textInUElement, activeMatch->startContainer());
    EXPECT_EQ(0, activeMatch->startOffset());
    EXPECT_EQ(textInUElement, activeMatch->endContainer());
    EXPECT_EQ(3, activeMatch->endOffset());

    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    activeMatch = textFinder().activeMatch();
    ASSERT_TRUE(activeMatch);
    EXPECT_EQ(textInBElement, activeMatch->startContainer());
    EXPECT_EQ(0, activeMatch->startOffset());
    EXPECT_EQ(textInBElement, activeMatch->endContainer());
    EXPECT_EQ(3, activeMatch->endOffset());

    // Should wrap to the first match.
    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    activeMatch = textFinder().activeMatch();
    ASSERT_TRUE(activeMatch);
    EXPECT_EQ(textInIElement, activeMatch->startContainer());
    EXPECT_EQ(0, activeMatch->startOffset());
    EXPECT_EQ(textInIElement, activeMatch->endContainer());
    EXPECT_EQ(3, activeMatch->endOffset());

    // Fresh search in the reverse order.
    identifier = 1;
    findOptions = WebFindOptions();
    findOptions.forward = false;

    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    activeMatch = textFinder().activeMatch();
    ASSERT_TRUE(activeMatch);
    EXPECT_EQ(textInBElement, activeMatch->startContainer());
    EXPECT_EQ(0, activeMatch->startOffset());
    EXPECT_EQ(textInBElement, activeMatch->endContainer());
    EXPECT_EQ(3, activeMatch->endOffset());

    findOptions.findNext = true;
    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    activeMatch = textFinder().activeMatch();
    ASSERT_TRUE(activeMatch);
    EXPECT_EQ(textInUElement, activeMatch->startContainer());
    EXPECT_EQ(0, activeMatch->startOffset());
    EXPECT_EQ(textInUElement, activeMatch->endContainer());
    EXPECT_EQ(3, activeMatch->endOffset());

    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    activeMatch = textFinder().activeMatch();
    ASSERT_TRUE(activeMatch);
    EXPECT_EQ(textInIElement, activeMatch->startContainer());
    EXPECT_EQ(0, activeMatch->startOffset());
    EXPECT_EQ(textInIElement, activeMatch->endContainer());
    EXPECT_EQ(3, activeMatch->endOffset());

    // And wrap.
    ASSERT_TRUE(textFinder().find(identifier, searchText, findOptions, wrapWithinFrame, selectionRect));
    activeMatch = textFinder().activeMatch();
    ASSERT_TRUE(activeMatch);
    EXPECT_EQ(textInBElement, activeMatch->startContainer());
    EXPECT_EQ(0, activeMatch->startOffset());
    EXPECT_EQ(textInBElement, activeMatch->endContainer());
    EXPECT_EQ(3, activeMatch->endOffset());
}

TEST_F(TextFinderTest, ScopeTextMatchesSimple)
{
    document().body()->setInnerHTML("XXXXFindMeYYYYfindmeZZZZ", ASSERT_NO_EXCEPTION);
    Node* textNode = document().body()->firstChild();

    int identifier = 0;
    WebString searchText(String("FindMe"));
    WebFindOptions findOptions; // Default.

    textFinder().resetMatchCount();
    textFinder().scopeStringMatches(identifier, searchText, findOptions, true);
    while (textFinder().scopingInProgress())
        runPendingTasks();

    EXPECT_EQ(2, textFinder().totalMatchCount());
    WebVector<WebFloatRect> matchRects;
    textFinder().findMatchRects(matchRects);
    ASSERT_EQ(2u, matchRects.size());
    EXPECT_EQ(findInPageRect(textNode, 4, textNode, 10), matchRects[0]);
    EXPECT_EQ(findInPageRect(textNode, 14, textNode, 20), matchRects[1]);
}

TEST_F(TextFinderTest, ScopeTextMatchesWithShadowDOM)
{
    document().body()->setInnerHTML("<b>FOO</b><i>foo</i>", ASSERT_NO_EXCEPTION);
    RefPtrWillBeRawPtr<ShadowRoot> shadowRoot = document().body()->createShadowRootInternal(ShadowRootType::V0, ASSERT_NO_EXCEPTION);
    shadowRoot->setInnerHTML("<content select=\"i\"></content><u>Foo</u><content></content>", ASSERT_NO_EXCEPTION);
    Node* textInBElement = document().body()->firstChild()->firstChild();
    Node* textInIElement = document().body()->lastChild()->firstChild();
    Node* textInUElement = shadowRoot->childNodes()->item(1)->firstChild();

    int identifier = 0;
    WebString searchText(String("fOO"));
    WebFindOptions findOptions; // Default.

    textFinder().resetMatchCount();
    textFinder().scopeStringMatches(identifier, searchText, findOptions, true);
    while (textFinder().scopingInProgress())
        runPendingTasks();

    // TextIterator currently returns the matches in the composed tree order,
    // so in this case the matches will be returned in the order of
    // <i> -> <u> -> <b>.
    EXPECT_EQ(3, textFinder().totalMatchCount());
    WebVector<WebFloatRect> matchRects;
    textFinder().findMatchRects(matchRects);
    ASSERT_EQ(3u, matchRects.size());
    EXPECT_EQ(findInPageRect(textInIElement, 0, textInIElement, 3), matchRects[0]);
    EXPECT_EQ(findInPageRect(textInUElement, 0, textInUElement, 3), matchRects[1]);
    EXPECT_EQ(findInPageRect(textInBElement, 0, textInBElement, 3), matchRects[2]);
}

TEST_F(TextFinderTest, ScopeRepeatPatternTextMatches)
{
    document().body()->setInnerHTML("ab ab ab ab ab", ASSERT_NO_EXCEPTION);
    Node* textNode = document().body()->firstChild();

    int identifier = 0;
    WebString searchText(String("ab ab"));
    WebFindOptions findOptions; // Default.

    textFinder().resetMatchCount();
    textFinder().scopeStringMatches(identifier, searchText, findOptions, true);
    while (textFinder().scopingInProgress())
        runPendingTasks();

    EXPECT_EQ(2, textFinder().totalMatchCount());
    WebVector<WebFloatRect> matchRects;
    textFinder().findMatchRects(matchRects);
    ASSERT_EQ(2u, matchRects.size());
    EXPECT_EQ(findInPageRect(textNode, 0, textNode, 5), matchRects[0]);
    EXPECT_EQ(findInPageRect(textNode, 6, textNode, 11), matchRects[1]);
}

TEST_F(TextFinderTest, OverlappingMatches)
{
    document().body()->setInnerHTML("aababaa", ASSERT_NO_EXCEPTION);
    Node* textNode = document().body()->firstChild();

    int identifier = 0;
    WebString searchText(String("aba"));
    WebFindOptions findOptions; // Default.

    textFinder().resetMatchCount();
    textFinder().scopeStringMatches(identifier, searchText, findOptions, true);
    while (textFinder().scopingInProgress())
        runPendingTasks();

    // We shouldn't find overlapped matches.
    EXPECT_EQ(1, textFinder().totalMatchCount());
    WebVector<WebFloatRect> matchRects;
    textFinder().findMatchRects(matchRects);
    ASSERT_EQ(1u, matchRects.size());
    EXPECT_EQ(findInPageRect(textNode, 1, textNode, 4), matchRects[0]);
}

TEST_F(TextFinderTest, SequentialMatches)
{
    document().body()->setInnerHTML("ababab", ASSERT_NO_EXCEPTION);
    Node* textNode = document().body()->firstChild();

    int identifier = 0;
    WebString searchText(String("ab"));
    WebFindOptions findOptions; // Default.

    textFinder().resetMatchCount();
    textFinder().scopeStringMatches(identifier, searchText, findOptions, true);
    while (textFinder().scopingInProgress())
        runPendingTasks();

    EXPECT_EQ(3, textFinder().totalMatchCount());
    WebVector<WebFloatRect> matchRects;
    textFinder().findMatchRects(matchRects);
    ASSERT_EQ(3u, matchRects.size());
    EXPECT_EQ(findInPageRect(textNode, 0, textNode, 2), matchRects[0]);
    EXPECT_EQ(findInPageRect(textNode, 2, textNode, 4), matchRects[1]);
    EXPECT_EQ(findInPageRect(textNode, 4, textNode, 6), matchRects[2]);
}

class TextFinderFakeTimerTest : public TextFinderTest {
protected:
    // A simple platform that mocks out the clock.
    class TimeProxyPlatform : public TestingPlatformSupport {
    public:
        TimeProxyPlatform()
            : m_timeCounter(m_oldPlatform->currentTimeSeconds())
        {
        }

    private:
        Platform& ensureFallback()
        {
            ASSERT(m_oldPlatform);
            return *m_oldPlatform;
        }

        // From blink::Platform:
        double currentTimeSeconds() override
        {
            return ++m_timeCounter;
        }

        // These two methods allow timers to work correctly.
        double monotonicallyIncreasingTimeSeconds() override
        {
            return ensureFallback().monotonicallyIncreasingTimeSeconds();
        }

        WebUnitTestSupport* unitTestSupport() override { return ensureFallback().unitTestSupport(); }
        WebString defaultLocale() override { return ensureFallback().defaultLocale(); }
        WebCompositorSupport* compositorSupport() override { return ensureFallback().compositorSupport(); }

        double m_timeCounter;
    };

    TimeProxyPlatform m_proxyTimePlatform;
};

TEST_F(TextFinderFakeTimerTest, ScopeWithTimeouts)
{
    // Make a long string.
    String text(Vector<UChar>(100));
    text.fill('a');
    String searchPattern("abc");
    // Make 4 substrings "abc" in text.
    text.insert(searchPattern, 1);
    text.insert(searchPattern, 10);
    text.insert(searchPattern, 50);
    text.insert(searchPattern, 90);

    document().body()->setInnerHTML(text, ASSERT_NO_EXCEPTION);

    int identifier = 0;
    WebFindOptions findOptions; // Default.

    textFinder().resetMatchCount();

    // There will be only one iteration before timeout, because increment
    // of the TimeProxyPlatform timer is greater than timeout threshold.
    textFinder().scopeStringMatches(identifier, searchPattern, findOptions, true);
    while (textFinder().scopingInProgress())
        runPendingTasks();

    EXPECT_EQ(4, textFinder().totalMatchCount());
}

} // namespace blink
