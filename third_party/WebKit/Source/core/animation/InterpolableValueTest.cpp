// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/animation/InterpolableValue.h"

#include "core/animation/Interpolation.h"
#include "core/animation/PropertyHandle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

namespace {

class SampleInterpolation : public Interpolation {
public:
    static PassRefPtr<Interpolation> create(PassOwnPtr<InterpolableValue> start, PassOwnPtr<InterpolableValue> end)
    {
        return adoptRef(new SampleInterpolation(start, end));
    }

    PropertyHandle property() const override
    {
        return PropertyHandle(CSSPropertyBackgroundColor);
    }
private:
    SampleInterpolation(PassOwnPtr<InterpolableValue> start, PassOwnPtr<InterpolableValue> end)
        : Interpolation(start, end)
    {
    }
};

} // namespace

class AnimationInterpolableValueTest : public ::testing::Test {
protected:
    InterpolableValue* interpolationValue(Interpolation& interpolation)
    {
        return interpolation.getCachedValueForTesting();
    }

    double interpolateNumbers(double a, double b, double progress)
    {
        RefPtr<Interpolation> i = SampleInterpolation::create(InterpolableNumber::create(a), InterpolableNumber::create(b));
        i->interpolate(0, progress);
        return toInterpolableNumber(interpolationValue(*i.get()))->value();
    }

    bool interpolateBools(bool a, bool b, double progress)
    {
        RefPtr<Interpolation> i = SampleInterpolation::create(InterpolableBool::create(a), InterpolableBool::create(b));
        i->interpolate(0, progress);
        return toInterpolableBool(interpolationValue(*i.get()))->value();
    }

    void scaleAndAdd(InterpolableValue& base, double scale, const InterpolableValue& add)
    {
        base.scaleAndAdd(scale, add);
    }

    PassRefPtr<Interpolation> interpolateLists(PassOwnPtr<InterpolableList> listA, PassOwnPtr<InterpolableList> listB, double progress)
    {
        RefPtr<Interpolation> i = SampleInterpolation::create(listA, listB);
        i->interpolate(0, progress);
        return i;
    }
};

TEST_F(AnimationInterpolableValueTest, InterpolateNumbers)
{
    EXPECT_FLOAT_EQ(126, interpolateNumbers(42, 0, -2));
    EXPECT_FLOAT_EQ(42, interpolateNumbers(42, 0, 0));
    EXPECT_FLOAT_EQ(29.4f, interpolateNumbers(42, 0, 0.3));
    EXPECT_FLOAT_EQ(21, interpolateNumbers(42, 0, 0.5));
    EXPECT_FLOAT_EQ(0, interpolateNumbers(42, 0, 1));
    EXPECT_FLOAT_EQ(-21, interpolateNumbers(42, 0, 1.5));
}

TEST_F(AnimationInterpolableValueTest, InterpolateBools)
{
    EXPECT_FALSE(interpolateBools(false, true, -1));
    EXPECT_FALSE(interpolateBools(false, true, 0));
    EXPECT_FALSE(interpolateBools(false, true, 0.3));
    EXPECT_TRUE(interpolateBools(false, true, 0.5));
    EXPECT_TRUE(interpolateBools(false, true, 1));
    EXPECT_TRUE(interpolateBools(false, true, 2));
}

TEST_F(AnimationInterpolableValueTest, SimpleList)
{
    OwnPtr<InterpolableList> listA = InterpolableList::create(3);
    listA->set(0, InterpolableNumber::create(0));
    listA->set(1, InterpolableNumber::create(42));
    listA->set(2, InterpolableNumber::create(20.5));

    OwnPtr<InterpolableList> listB = InterpolableList::create(3);
    listB->set(0, InterpolableNumber::create(100));
    listB->set(1, InterpolableNumber::create(-200));
    listB->set(2, InterpolableNumber::create(300));

    RefPtr<Interpolation> i = interpolateLists(listA.release(), listB.release(), 0.3);
    InterpolableList* outList = toInterpolableList(interpolationValue(*i.get()));
    EXPECT_FLOAT_EQ(30, toInterpolableNumber(outList->get(0))->value());
    EXPECT_FLOAT_EQ(-30.6f, toInterpolableNumber(outList->get(1))->value());
    EXPECT_FLOAT_EQ(104.35f, toInterpolableNumber(outList->get(2))->value());
}

TEST_F(AnimationInterpolableValueTest, NestedList)
{
    OwnPtr<InterpolableList> listA = InterpolableList::create(3);
    listA->set(0, InterpolableNumber::create(0));
    OwnPtr<InterpolableList> subListA = InterpolableList::create(1);
    subListA->set(0, InterpolableNumber::create(100));
    listA->set(1, subListA.release());
    listA->set(2, InterpolableBool::create(false));

    OwnPtr<InterpolableList> listB = InterpolableList::create(3);
    listB->set(0, InterpolableNumber::create(100));
    OwnPtr<InterpolableList> subListB = InterpolableList::create(1);
    subListB->set(0, InterpolableNumber::create(50));
    listB->set(1, subListB.release());
    listB->set(2, InterpolableBool::create(true));

    RefPtr<Interpolation> i = interpolateLists(listA.release(), listB.release(), 0.5);
    InterpolableList* outList = toInterpolableList(interpolationValue(*i.get()));
    EXPECT_FLOAT_EQ(50, toInterpolableNumber(outList->get(0))->value());
    EXPECT_FLOAT_EQ(75, toInterpolableNumber(toInterpolableList(outList->get(1))->get(0))->value());
    EXPECT_TRUE(toInterpolableBool(outList->get(2))->value());
}

TEST_F(AnimationInterpolableValueTest, ScaleAndAddNumbers)
{
    OwnPtr<InterpolableNumber> base = InterpolableNumber::create(10);
    scaleAndAdd(*base, 2, *InterpolableNumber::create(1));
    EXPECT_FLOAT_EQ(21, base->value());

    base = InterpolableNumber::create(10);
    scaleAndAdd(*base, 0, *InterpolableNumber::create(5));
    EXPECT_FLOAT_EQ(5, base->value());

    base = InterpolableNumber::create(10);
    scaleAndAdd(*base, -1, *InterpolableNumber::create(8));
    EXPECT_FLOAT_EQ(-2, base->value());
}

TEST_F(AnimationInterpolableValueTest, ScaleAndAddLists)
{
    OwnPtr<InterpolableList> baseList = InterpolableList::create(3);
    baseList->set(0, InterpolableNumber::create(5));
    baseList->set(1, InterpolableNumber::create(10));
    baseList->set(2, InterpolableNumber::create(15));
    OwnPtr<InterpolableList> addList = InterpolableList::create(3);
    addList->set(0, InterpolableNumber::create(1));
    addList->set(1, InterpolableNumber::create(2));
    addList->set(2, InterpolableNumber::create(3));
    scaleAndAdd(*baseList, 2, *addList);
    EXPECT_FLOAT_EQ(11, toInterpolableNumber(baseList->get(0))->value());
    EXPECT_FLOAT_EQ(22, toInterpolableNumber(baseList->get(1))->value());
    EXPECT_FLOAT_EQ(33, toInterpolableNumber(baseList->get(2))->value());
}

}
