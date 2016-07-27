// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "platform/fonts/OrientationIterator.h"

#include "platform/Logging.h"
#include "testing/gtest/include/gtest/gtest.h"
#include <string>

namespace blink {

struct TestRun {
    std::string text;
    OrientationIterator::RenderOrientation code;
};

struct ExpectedRun {
    unsigned limit;
    OrientationIterator::RenderOrientation renderOrientation;

    ExpectedRun(unsigned theLimit, OrientationIterator::RenderOrientation theRenderOrientation)
        : limit(theLimit)
        , renderOrientation(theRenderOrientation)
    {
    }
};

class OrientationIteratorTest : public testing::Test {
protected:
#if !LOG_DISABLED
    static void SetUpTestCase()
    {
        LogFonts = { WTFLogChannelOn };
    }
#endif

    void CheckRuns(const Vector<TestRun>& runs)
    {
        String text(String::make16BitFrom8BitSource(0, 0));
        Vector<ExpectedRun> expect;
        for (auto& run : runs) {
            text.append(String::fromUTF8(run.text.c_str()));
            expect.append(ExpectedRun(text.length(), run.code));
        }
        OrientationIterator orientationIterator(text.characters16(), text.length(), FontOrientation::VerticalMixed);
        VerifyRuns(&orientationIterator, expect);
    }

    void VerifyRuns(OrientationIterator* orientationIterator,
        const Vector<ExpectedRun>& expect)
    {
        unsigned limit;
        OrientationIterator::RenderOrientation renderOrientation;
        unsigned long runCount = 0;
        while (orientationIterator->consume(&limit, &renderOrientation)) {
            ASSERT_LT(runCount, expect.size());
            ASSERT_EQ(expect[runCount].limit, limit);
            ASSERT_EQ(expect[runCount].renderOrientation, renderOrientation);
            ++runCount;
        }
        WTF_LOG(Fonts, "Expected %zu runs, got %lu ", expect.size(), runCount);
        ASSERT_EQ(expect.size(), runCount);
    }
};

// TODO(esprehn): WTF::Vector should allow initialization from a literal.
#define CHECK_RUNS(...) \
    static const TestRun runsArray[] = __VA_ARGS__; \
    Vector<TestRun> runs; \
    runs.append(runsArray, sizeof(runsArray) / sizeof(*runsArray)); \
    CheckRuns(runs);

TEST_F(OrientationIteratorTest, Empty)
{
    String empty(String::make16BitFrom8BitSource(0, 0));
    OrientationIterator orientationIterator(empty.characters16(), empty.length(), FontOrientation::VerticalMixed);
    unsigned limit = 0;
    OrientationIterator::RenderOrientation orientation = OrientationIterator::OrientationInvalid;
    ASSERT(!orientationIterator.consume(&limit, &orientation));
    ASSERT_EQ(limit, 0u);
    ASSERT_EQ(orientation, OrientationIterator::OrientationInvalid);
}

TEST_F(OrientationIteratorTest, OneCharLatin)
{
    CHECK_RUNS({ { "A", OrientationIterator::OrientationRotateSideways } });
}

TEST_F(OrientationIteratorTest, OneAceOfSpades)
{
    CHECK_RUNS({ { "🂡", OrientationIterator::OrientationKeep } });
}

TEST_F(OrientationIteratorTest, OneEthiopicSyllable)
{
    CHECK_RUNS({ { "ጀ", OrientationIterator::OrientationRotateSideways } });
}

TEST_F(OrientationIteratorTest, JapaneseLetterlikeEnd)
{
    CHECK_RUNS({ { "いろは", OrientationIterator::OrientationKeep },
        { "ℐℒℐℒℐℒℐℒℐℒℐℒℐℒ", OrientationIterator::OrientationRotateSideways } });
}

TEST_F(OrientationIteratorTest, LetterlikeJapaneseEnd)
{
    CHECK_RUNS({ { "ℐ", OrientationIterator::OrientationRotateSideways },
        { "いろは", OrientationIterator::OrientationKeep } });
}

TEST_F(OrientationIteratorTest, OneCharJapanese)
{
    CHECK_RUNS({ { "い", OrientationIterator::OrientationKeep } });
}

TEST_F(OrientationIteratorTest, Japanese)
{
    CHECK_RUNS({ { "いろはにほへと", OrientationIterator::OrientationKeep } });
}

TEST_F(OrientationIteratorTest, JapaneseLatinMixedInside)
{
    CHECK_RUNS({ { "いろはに", OrientationIterator::OrientationKeep },
        { "Abc", OrientationIterator::OrientationRotateSideways },
        { "ほへと", OrientationIterator::OrientationKeep } });
}

TEST_F(OrientationIteratorTest, PunctuationJapanese)
{
    CHECK_RUNS({ { ".…¡", OrientationIterator::OrientationRotateSideways },
        { "ほへと", OrientationIterator::OrientationKeep } });
}

TEST_F(OrientationIteratorTest, JapaneseLatinMixedOutside)
{
    CHECK_RUNS({ { "Abc", OrientationIterator::OrientationRotateSideways },
        { "ほへと", OrientationIterator::OrientationKeep },
        { "Xyz", OrientationIterator::OrientationRotateSideways } });
}

TEST_F(OrientationIteratorTest, JapaneseMahjonggMixed)
{
    CHECK_RUNS({ { "いろはに🀤ほへと", OrientationIterator::OrientationKeep } });
}

} // namespace blink
