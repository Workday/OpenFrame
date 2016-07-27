// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define ENABLE_ASSERT 1

#include "config.h"
#include "wtf/Assertions.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "wtf/text/StringBuilder.h"
#include <stdio.h>

namespace WTF {

static const int kPrinterBufferSize = 256;
static char gBuffer[kPrinterBufferSize];
static StringBuilder gBuilder;

static void vprint(const char* format, va_list args)
{
    int written = vsnprintf(gBuffer, kPrinterBufferSize, format, args);
    if (written > 0 && written < kPrinterBufferSize)
        gBuilder.append(gBuffer);
}

class AssertionsTest : public testing::Test {
protected:
    AssertionsTest()
    {
        ScopedLogger::setPrintFuncForTests(vprint);
    }
};

TEST_F(AssertionsTest, ScopedLogger)
{
    {
        WTF_CREATE_SCOPED_LOGGER(a, "a1");
        {
            WTF_CREATE_SCOPED_LOGGER_IF(b, false, "b1");
            {
                WTF_CREATE_SCOPED_LOGGER(c, "c");
                {
                    WTF_CREATE_SCOPED_LOGGER(d, "d %d %s", -1, "hello");
                }
            }
            WTF_APPEND_SCOPED_LOGGER(b, "b2");
        }
        WTF_APPEND_SCOPED_LOGGER(a, "a2 %.1f", 0.5);
    }

    EXPECT_EQ(
        "( a1\n"
        "  ( c\n"
        "    ( d -1 hello )\n"
        "  )\n"
        "  a2 0.5\n"
        ")\n", gBuilder.toString());
};


} // namespace WTF
