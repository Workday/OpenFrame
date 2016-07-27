// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/editing/FrameSelection.h"

#include "bindings/core/v8/ExceptionStatePlaceholder.h"
#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/dom/Text.h"
#include "core/editing/EditingTestBase.h"
#include "core/frame/FrameView.h"
#include "core/html/HTMLBodyElement.h"
#include "core/html/HTMLDocument.h"
#include "core/layout/LayoutView.h"
#include "core/paint/PaintInfo.h"
#include "core/testing/DummyPageHolder.h"
#include "platform/graphics/paint/DrawingRecorder.h"
#include "platform/graphics/paint/PaintController.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "wtf/OwnPtr.h"
#include "wtf/PassRefPtr.h"
#include "wtf/RefPtr.h"
#include "wtf/StdLibExtras.h"

namespace blink {

class FrameSelectionTest : public EditingTestBase {
protected:
    void setSelection(const VisibleSelection&);
    FrameSelection& selection() const;
    const VisibleSelection& visibleSelectionInDOMTree() const { return selection().selection(); }
    const VisibleSelectionInComposedTree& visibleSelectionInComposedTree() const { return selection().selectionInComposedTree(); }

    PassRefPtrWillBeRawPtr<Text> appendTextNode(const String& data);
    int layoutCount() const { return dummyPageHolder().frameView().layoutCount(); }

private:
    RefPtrWillBePersistent<Text> m_textNode;
};

void FrameSelectionTest::setSelection(const VisibleSelection& newSelection)
{
    dummyPageHolder().frame().selection().setSelection(newSelection);
}

FrameSelection& FrameSelectionTest::selection() const
{
    return dummyPageHolder().frame().selection();
}

PassRefPtrWillBeRawPtr<Text> FrameSelectionTest::appendTextNode(const String& data)
{
    RefPtrWillBeRawPtr<Text> text = document().createTextNode(data);
    document().body()->appendChild(text);
    return text.release();
}

TEST_F(FrameSelectionTest, SetValidSelection)
{
    RefPtrWillBeRawPtr<Text> text = appendTextNode("Hello, World!");
    VisibleSelection validSelection(Position(text, 0), Position(text, 5));
    EXPECT_FALSE(validSelection.isNone());
    setSelection(validSelection);
    EXPECT_FALSE(selection().isNone());
}

TEST_F(FrameSelectionTest, SetInvalidSelection)
{
    // Create a new document without frame by using DOMImplementation.
    DocumentInit dummy;
    RefPtrWillBeRawPtr<Document> documentWithoutFrame = Document::create();
    RefPtrWillBeRawPtr<Element> body = documentWithoutFrame->createElement(HTMLNames::bodyTag, false);
    documentWithoutFrame->appendChild(body);
    RefPtrWillBeRawPtr<Text> anotherText = documentWithoutFrame->createTextNode("Hello, another world");
    body->appendChild(anotherText);

    // Create a new VisibleSelection for the new document without frame and
    // update FrameSelection with the selection.
    VisibleSelection invalidSelection;
    invalidSelection.setWithoutValidation(Position(anotherText, 0), Position(anotherText, 5));
    setSelection(invalidSelection);

    EXPECT_TRUE(selection().isNone());
}

TEST_F(FrameSelectionTest, InvalidateCaretRect)
{
    RefPtrWillBeRawPtr<Text> text = appendTextNode("Hello, World!");
    document().view()->updateAllLifecyclePhases();

    VisibleSelection validSelection(Position(text, 0), Position(text, 0));
    setSelection(validSelection);
    selection().setCaretRectNeedsUpdate();
    EXPECT_TRUE(selection().isCaretBoundsDirty());
    selection().invalidateCaretRect();
    EXPECT_FALSE(selection().isCaretBoundsDirty());

    document().body()->removeChild(text);
    document().updateLayoutIgnorePendingStylesheets();
    selection().setCaretRectNeedsUpdate();
    EXPECT_TRUE(selection().isCaretBoundsDirty());
    selection().invalidateCaretRect();
    EXPECT_FALSE(selection().isCaretBoundsDirty());
}

TEST_F(FrameSelectionTest, PaintCaretShouldNotLayout)
{
    RefPtrWillBeRawPtr<Text> text = appendTextNode("Hello, World!");
    document().view()->updateAllLifecyclePhases();

    document().body()->setContentEditable("true", ASSERT_NO_EXCEPTION);
    document().body()->focus();
    EXPECT_TRUE(document().body()->focused());

    VisibleSelection validSelection(Position(text, 0), Position(text, 0));
    selection().setCaretVisible(true);
    setSelection(validSelection);
    EXPECT_TRUE(selection().isCaret());
    EXPECT_TRUE(selection().ShouldPaintCaretForTesting());

    int startCount = layoutCount();
    {
        // To force layout in next updateLayout calling, widen view.
        FrameView& frameView = dummyPageHolder().frameView();
        IntRect frameRect = frameView.frameRect();
        frameRect.setWidth(frameRect.width() + 1);
        frameRect.setHeight(frameRect.height() + 1);
        dummyPageHolder().frameView().setFrameRect(frameRect);
    }
    OwnPtr<PaintController> paintController = PaintController::create();
    GraphicsContext context(*paintController);
    DrawingRecorder drawingRecorder(context, *dummyPageHolder().frameView().layoutView(), DisplayItem::Caret, LayoutRect::infiniteIntRect());
    selection().paintCaret(&context, LayoutPoint());
    EXPECT_EQ(startCount, layoutCount());
}

#define EXPECT_EQ_SELECTED_TEXT(text) \
    EXPECT_EQ(text, WebString(selection().selectedText()).utf8())

TEST_F(FrameSelectionTest, SelectWordAroundPosition)
{
    // "Foo Bar  Baz,"
    RefPtrWillBeRawPtr<Text> text = appendTextNode("Foo Bar&nbsp;&nbsp;Baz,");
    // "Fo|o Bar  Baz,"
    EXPECT_TRUE(selection().selectWordAroundPosition(createVisiblePosition(Position(text, 2))));
    EXPECT_EQ_SELECTED_TEXT("Foo");
    // "Foo| Bar  Baz,"
    EXPECT_TRUE(selection().selectWordAroundPosition(createVisiblePosition(Position(text, 3))));
    EXPECT_EQ_SELECTED_TEXT("Foo");
    // "Foo Bar | Baz,"
    EXPECT_FALSE(selection().selectWordAroundPosition(createVisiblePosition(Position(text, 13))));
    // "Foo Bar  Baz|,"
    EXPECT_TRUE(selection().selectWordAroundPosition(createVisiblePosition(Position(text, 22))));
    EXPECT_EQ_SELECTED_TEXT("Baz");
}

TEST_F(FrameSelectionTest, MoveRangeSelectionTest)
{
    // "Foo Bar Baz,"
    RefPtrWillBeRawPtr<Text> text = appendTextNode("Foo Bar Baz,");
    // Itinitializes with "Foo B|a>r Baz," (| means start and > means end).
    selection().setSelection(VisibleSelection(Position(text, 5), Position(text, 6)));
    EXPECT_EQ_SELECTED_TEXT("a");

    // "Foo B|ar B>az," with the Character granularity.
    selection().moveRangeSelection(createVisiblePosition(Position(text, 5)), createVisiblePosition(Position(text, 9)), CharacterGranularity);
    EXPECT_EQ_SELECTED_TEXT("ar B");
    // "Foo B|ar B>az," with the Word granularity.
    selection().moveRangeSelection(createVisiblePosition(Position(text, 5)), createVisiblePosition(Position(text, 9)), WordGranularity);
    EXPECT_EQ_SELECTED_TEXT("Bar Baz");
    // "Fo<o B|ar Baz," with the Character granularity.
    selection().moveRangeSelection(createVisiblePosition(Position(text, 5)), createVisiblePosition(Position(text, 2)), CharacterGranularity);
    EXPECT_EQ_SELECTED_TEXT("o B");
    // "Fo<o B|ar Baz," with the Word granularity.
    selection().moveRangeSelection(createVisiblePosition(Position(text, 5)), createVisiblePosition(Position(text, 2)), WordGranularity);
    EXPECT_EQ_SELECTED_TEXT("Foo Bar");
}

TEST_F(FrameSelectionTest, setNonDirectionalSelectionIfNeeded)
{
    const char* bodyContent = "<span id=top>top</span><span id=host></span>";
    const char* shadowContent = "<span id=bottom>bottom</span>";
    setBodyContent(bodyContent);
    RefPtrWillBeRawPtr<ShadowRoot> shadowRoot = setShadowContent(shadowContent, "host");
    updateLayoutAndStyleForPainting();

    Node* top = document().getElementById("top")->firstChild();
    Node* bottom = shadowRoot->getElementById("bottom")->firstChild();
    Node* host = document().getElementById("host");

    // top to bottom
    selection().setNonDirectionalSelectionIfNeeded(VisibleSelectionInComposedTree(PositionInComposedTree(top, 1), PositionInComposedTree(bottom, 3)), CharacterGranularity);
    EXPECT_EQ(Position(top, 1), visibleSelectionInDOMTree().base());
    EXPECT_EQ(Position::beforeNode(host), visibleSelectionInDOMTree().extent());
    EXPECT_EQ(Position(top, 1), visibleSelectionInDOMTree().start());
    EXPECT_EQ(Position(top, 3), visibleSelectionInDOMTree().end());

    EXPECT_EQ(PositionInComposedTree(top, 1), visibleSelectionInComposedTree().base());
    EXPECT_EQ(PositionInComposedTree(bottom, 3), visibleSelectionInComposedTree().extent());
    EXPECT_EQ(PositionInComposedTree(top, 1), visibleSelectionInComposedTree().start());
    EXPECT_EQ(PositionInComposedTree(bottom, 3), visibleSelectionInComposedTree().end());

    // bottom to top
    selection().setNonDirectionalSelectionIfNeeded(VisibleSelectionInComposedTree(PositionInComposedTree(bottom, 3), PositionInComposedTree(top, 1)), CharacterGranularity);
    EXPECT_EQ(Position(bottom, 3), visibleSelectionInDOMTree().base());
    EXPECT_EQ(Position::beforeNode(bottom->parentNode()), visibleSelectionInDOMTree().extent());
    EXPECT_EQ(Position(bottom, 0), visibleSelectionInDOMTree().start());
    EXPECT_EQ(Position(bottom, 3), visibleSelectionInDOMTree().end());

    EXPECT_EQ(PositionInComposedTree(bottom, 3), visibleSelectionInComposedTree().base());
    EXPECT_EQ(PositionInComposedTree(top, 1), visibleSelectionInComposedTree().extent());
    EXPECT_EQ(PositionInComposedTree(top, 1), visibleSelectionInComposedTree().start());
    EXPECT_EQ(PositionInComposedTree(bottom, 3), visibleSelectionInComposedTree().end());
}

} // namespace blink
