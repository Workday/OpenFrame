// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/style/GridResolvedPosition.h"

#include "core/layout/LayoutBox.h"
#include "core/style/GridCoordinate.h"

namespace blink {

static const NamedGridLinesMap& gridLinesForSide(const ComputedStyle& style, GridPositionSide side)
{
    return (side == ColumnStartSide || side == ColumnEndSide) ? style.namedGridColumnLines() : style.namedGridRowLines();
}

static inline String implicitNamedGridLineForSide(const String& lineName, GridPositionSide side)
{
    return lineName + ((side == ColumnStartSide || side == RowStartSide) ? "-start" : "-end");
}

bool GridResolvedPosition::isValidNamedLineOrArea(const String& lineName, const ComputedStyle& style, GridPositionSide side)
{
    const NamedGridLinesMap& gridLineNames = gridLinesForSide(style, side);

    return gridLineNames.contains(implicitNamedGridLineForSide(lineName, side)) || gridLineNames.contains(lineName);
}

GridPositionSide GridResolvedPosition::initialPositionSide(GridTrackSizingDirection direction)
{
    return (direction == ForColumns) ? ColumnStartSide : RowStartSide;
}

GridPositionSide GridResolvedPosition::finalPositionSide(GridTrackSizingDirection direction)
{
    return (direction == ForColumns) ? ColumnEndSide : RowEndSide;
}

static void initialAndFinalPositionsFromStyle(const ComputedStyle& gridContainerStyle, const LayoutBox& gridItem, GridTrackSizingDirection direction, GridPosition& initialPosition, GridPosition& finalPosition)
{
    initialPosition = (direction == ForColumns) ? gridItem.style()->gridColumnStart() : gridItem.style()->gridRowStart();
    finalPosition = (direction == ForColumns) ? gridItem.style()->gridColumnEnd() : gridItem.style()->gridRowEnd();

    // We must handle the placement error handling code here instead of in the StyleAdjuster because we don't want to
    // overwrite the specified values.
    if (initialPosition.isSpan() && finalPosition.isSpan())
        finalPosition.setAutoPosition();

    // Try to early detect the case of non existing named grid lines. This way we could assume later that
    // GridResolvedPosition::resolveGrisPositionFromStyle() always return a valid resolved position.
    if (initialPosition.isNamedGridArea() && !GridResolvedPosition::isValidNamedLineOrArea(initialPosition.namedGridLine(), gridContainerStyle, GridResolvedPosition::initialPositionSide(direction)))
        initialPosition.setAutoPosition();

    if (finalPosition.isNamedGridArea() && !GridResolvedPosition::isValidNamedLineOrArea(finalPosition.namedGridLine(), gridContainerStyle, GridResolvedPosition::finalPositionSide(direction)))
        finalPosition.setAutoPosition();

    // If the grid item has an automatic position and a grid span for a named line in a given dimension, instead treat the grid span as one.
    if (initialPosition.isAuto() && finalPosition.isSpan() && !finalPosition.namedGridLine().isNull())
        finalPosition.setSpanPosition(1, String());
    if (finalPosition.isAuto() && initialPosition.isSpan() && !initialPosition.namedGridLine().isNull())
        initialPosition.setSpanPosition(1, String());
}

static GridSpan definiteGridSpanWithInitialNamedSpanAgainstOpposite(const GridResolvedPosition& resolvedOppositePosition, const GridPosition& position, const Vector<size_t>& gridLines)
{
    if (resolvedOppositePosition == 0)
        return GridSpan::definiteGridSpan(resolvedOppositePosition, resolvedOppositePosition.next());

    size_t firstLineBeforeOppositePositionIndex = 0;
    const size_t* firstLineBeforeOppositePosition = std::lower_bound(gridLines.begin(), gridLines.end(), resolvedOppositePosition.toInt());
    if (firstLineBeforeOppositePosition != gridLines.end())
        firstLineBeforeOppositePositionIndex = firstLineBeforeOppositePosition - gridLines.begin();
    size_t gridLineIndex = std::max<int>(0, firstLineBeforeOppositePositionIndex - position.spanPosition());
    GridResolvedPosition resolvedGridLinePosition = GridResolvedPosition(gridLines[gridLineIndex]);
    if (resolvedGridLinePosition >= resolvedOppositePosition)
        resolvedGridLinePosition = resolvedOppositePosition.prev();
    return GridSpan::definiteGridSpan(resolvedGridLinePosition, resolvedOppositePosition);
}

static GridSpan definiteGridSpanWithFinalNamedSpanAgainstOpposite(const GridResolvedPosition& resolvedOppositePosition, const GridPosition& position, const Vector<size_t>& gridLines)
{
    size_t firstLineAfterOppositePositionIndex = gridLines.size() - 1;
    const size_t* firstLineAfterOppositePosition = std::upper_bound(gridLines.begin(), gridLines.end(), resolvedOppositePosition.toInt());
    if (firstLineAfterOppositePosition != gridLines.end())
        firstLineAfterOppositePositionIndex = firstLineAfterOppositePosition - gridLines.begin();
    size_t gridLineIndex = std::min(gridLines.size() - 1, firstLineAfterOppositePositionIndex + position.spanPosition() - 1);
    GridResolvedPosition resolvedGridLinePosition = gridLines[gridLineIndex];
    if (resolvedGridLinePosition <= resolvedOppositePosition)
        resolvedGridLinePosition = resolvedOppositePosition.next();

    return GridSpan::definiteGridSpan(resolvedOppositePosition, resolvedGridLinePosition);
}

static GridSpan definiteGridSpanWithNamedSpanAgainstOpposite(const GridResolvedPosition& resolvedOppositePosition, const GridPosition& position, GridPositionSide side, const Vector<size_t>& gridLines)
{
    if (side == RowStartSide || side == ColumnStartSide)
        return definiteGridSpanWithInitialNamedSpanAgainstOpposite(resolvedOppositePosition, position, gridLines);

    return definiteGridSpanWithFinalNamedSpanAgainstOpposite(resolvedOppositePosition, position, gridLines);
}

static GridSpan resolveNamedGridLinePositionAgainstOppositePosition(const ComputedStyle& gridContainerStyle, const GridResolvedPosition& resolvedOppositePosition, const GridPosition& position, GridPositionSide side)
{
    ASSERT(position.isSpan());
    ASSERT(!position.namedGridLine().isNull());
    // Negative positions are not allowed per the specification and should have been handled during parsing.
    ASSERT(position.spanPosition() > 0);

    const NamedGridLinesMap& gridLinesNames = gridLinesForSide(gridContainerStyle, side);
    NamedGridLinesMap::const_iterator it = gridLinesNames.find(position.namedGridLine());

    // If there is no named grid line of that name, we resolve the position to 'auto' (which is equivalent to 'span 1' in this case).
    // See http://lists.w3.org/Archives/Public/www-style/2013Jun/0394.html.
    if (it == gridLinesNames.end()) {
        if ((side == ColumnStartSide || side == RowStartSide) && resolvedOppositePosition.toInt())
            return GridSpan::definiteGridSpan(resolvedOppositePosition.prev(), resolvedOppositePosition);
        return GridSpan::definiteGridSpan(resolvedOppositePosition, resolvedOppositePosition.next());
    }

    return definiteGridSpanWithNamedSpanAgainstOpposite(resolvedOppositePosition, position, side, it->value);
}

static GridSpan definiteGridSpanWithSpanAgainstOpposite(const GridResolvedPosition& resolvedOppositePosition, const GridPosition& position, GridPositionSide side)
{
    size_t positionOffset = position.spanPosition();
    if (side == ColumnStartSide || side == RowStartSide) {
        if (resolvedOppositePosition == 0)
            return GridSpan::definiteGridSpan(resolvedOppositePosition, resolvedOppositePosition.next());

        GridResolvedPosition initialResolvedPosition = GridResolvedPosition(std::max<int>(0, resolvedOppositePosition.toInt() - positionOffset));
        return GridSpan::definiteGridSpan(initialResolvedPosition, resolvedOppositePosition);
    }

    return GridSpan::definiteGridSpan(resolvedOppositePosition, GridResolvedPosition(resolvedOppositePosition.toInt() + positionOffset));
}

static GridSpan resolveGridPositionAgainstOppositePosition(const ComputedStyle& gridContainerStyle, const GridResolvedPosition& resolvedOppositePosition, const GridPosition& position, GridPositionSide side)
{
    if (position.isAuto()) {
        if ((side == ColumnStartSide || side == RowStartSide) && resolvedOppositePosition.toInt())
            return GridSpan::definiteGridSpan(resolvedOppositePosition.prev(), resolvedOppositePosition);
        return GridSpan::definiteGridSpan(resolvedOppositePosition, resolvedOppositePosition.next());
    }

    ASSERT(position.isSpan());
    ASSERT(position.spanPosition() > 0);

    if (!position.namedGridLine().isNull()) {
        // span 2 'c' -> we need to find the appropriate grid line before / after our opposite position.
        return resolveNamedGridLinePositionAgainstOppositePosition(gridContainerStyle, resolvedOppositePosition, position, side);
    }

    return definiteGridSpanWithSpanAgainstOpposite(resolvedOppositePosition, position, side);
}

GridSpan GridResolvedPosition::resolveGridPositionsFromAutoPlacementPosition(const ComputedStyle& gridContainerStyle, const LayoutBox& gridItem, GridTrackSizingDirection direction, const GridResolvedPosition& resolvedInitialPosition)
{
    GridPosition initialPosition, finalPosition;
    initialAndFinalPositionsFromStyle(gridContainerStyle, gridItem, direction, initialPosition, finalPosition);

    GridPositionSide finalSide = finalPositionSide(direction);

    // This method will only be used when both positions need to be resolved against the opposite one.
    ASSERT(initialPosition.shouldBeResolvedAgainstOppositePosition() && finalPosition.shouldBeResolvedAgainstOppositePosition());

    GridResolvedPosition resolvedFinalPosition = resolvedInitialPosition.next();

    if (initialPosition.isSpan())
        return resolveGridPositionAgainstOppositePosition(gridContainerStyle, resolvedInitialPosition, initialPosition, finalSide);
    if (finalPosition.isSpan())
        return resolveGridPositionAgainstOppositePosition(gridContainerStyle, resolvedInitialPosition, finalPosition, finalSide);

    return GridSpan::definiteGridSpan(resolvedInitialPosition, resolvedFinalPosition);
}

size_t GridResolvedPosition::explicitGridColumnCount(const ComputedStyle& gridContainerStyle)
{
    return std::min(gridContainerStyle.gridTemplateColumns().size(), kGridMaxTracks);
}

size_t GridResolvedPosition::explicitGridRowCount(const ComputedStyle& gridContainerStyle)
{
    return std::min(gridContainerStyle.gridTemplateRows().size(), kGridMaxTracks);
}

static size_t explicitGridSizeForSide(const ComputedStyle& gridContainerStyle, GridPositionSide side)
{
    return (side == ColumnStartSide || side == ColumnEndSide) ? GridResolvedPosition::explicitGridColumnCount(gridContainerStyle) : GridResolvedPosition::explicitGridRowCount(gridContainerStyle);
}

static GridResolvedPosition resolveNamedGridLinePositionFromStyle(const ComputedStyle& gridContainerStyle, const GridPosition& position, GridPositionSide side)
{
    ASSERT(!position.namedGridLine().isNull());

    const NamedGridLinesMap& gridLinesNames = gridLinesForSide(gridContainerStyle, side);
    NamedGridLinesMap::const_iterator it = gridLinesNames.find(position.namedGridLine());
    if (it == gridLinesNames.end()) {
        if (position.isPositive())
            return GridResolvedPosition(0);
        const size_t lastLine = explicitGridSizeForSide(gridContainerStyle, side);
        return lastLine;
    }

    size_t namedGridLineIndex;
    if (position.isPositive())
        namedGridLineIndex = std::min<size_t>(position.integerPosition(), it->value.size()) - 1;
    else
        namedGridLineIndex = std::max<int>(it->value.size() - abs(position.integerPosition()), 0);
    return it->value[namedGridLineIndex];
}

static GridResolvedPosition resolveGridPositionFromStyle(const ComputedStyle& gridContainerStyle, const GridPosition& position, GridPositionSide side)
{
    switch (position.type()) {
    case ExplicitPosition: {
        ASSERT(position.integerPosition());

        if (!position.namedGridLine().isNull())
            return resolveNamedGridLinePositionFromStyle(gridContainerStyle, position, side);

        // Handle <integer> explicit position.
        if (position.isPositive())
            return position.integerPosition() - 1;

        size_t resolvedPosition = abs(position.integerPosition()) - 1;
        const size_t endOfTrack = explicitGridSizeForSide(gridContainerStyle, side);

        // Per http://lists.w3.org/Archives/Public/www-style/2013Mar/0589.html, we clamp negative value to the first line.
        if (endOfTrack < resolvedPosition)
            return GridResolvedPosition(0);

        return endOfTrack - resolvedPosition;
    }
    case NamedGridAreaPosition:
    {
        // First attempt to match the grid area's edge to a named grid area: if there is a named line with the name
        // ''<custom-ident>-start (for grid-*-start) / <custom-ident>-end'' (for grid-*-end), contributes the first such
        // line to the grid item's placement.
        String namedGridLine = position.namedGridLine();
        ASSERT(GridResolvedPosition::isValidNamedLineOrArea(namedGridLine, gridContainerStyle, side));

        const NamedGridLinesMap& gridLineNames = gridLinesForSide(gridContainerStyle, side);
        NamedGridLinesMap::const_iterator implicitLineIter = gridLineNames.find(implicitNamedGridLineForSide(namedGridLine, side));
        if (implicitLineIter != gridLineNames.end())
            return implicitLineIter->value[0];

        // Otherwise, if there is a named line with the specified name, contributes the first such line to the grid
        // item's placement.
        NamedGridLinesMap::const_iterator explicitLineIter = gridLineNames.find(namedGridLine);
        if (explicitLineIter != gridLineNames.end())
            return explicitLineIter->value[0];

        // If none of the above works specs mandate us to treat it as auto BUT we should have detected it before calling
        // this function in GridResolvedPosition::resolveGridPositionsFromStyle(). We should be also covered by the
        // ASSERT at the beginning of this block.
        ASSERT_NOT_REACHED();
        return GridResolvedPosition(0);
    }
    case AutoPosition:
    case SpanPosition:
        // 'auto' and span depend on the opposite position for resolution (e.g. grid-row: auto / 1 or grid-column: span 3 / "myHeader").
        ASSERT_NOT_REACHED();
        return GridResolvedPosition(0);
    }
    ASSERT_NOT_REACHED();
    return GridResolvedPosition(0);
}

GridSpan GridResolvedPosition::resolveGridPositionsFromStyle(const ComputedStyle& gridContainerStyle, const LayoutBox& gridItem, GridTrackSizingDirection direction)
{
    GridPosition initialPosition, finalPosition;
    initialAndFinalPositionsFromStyle(gridContainerStyle, gridItem, direction, initialPosition, finalPosition);

    GridPositionSide initialSide = initialPositionSide(direction);
    GridPositionSide finalSide = finalPositionSide(direction);

    if (initialPosition.shouldBeResolvedAgainstOppositePosition() && finalPosition.shouldBeResolvedAgainstOppositePosition()) {
        // We can't get our grid positions without running the auto placement algorithm.
        return GridSpan::indefiniteGridSpan();
    }

    if (initialPosition.shouldBeResolvedAgainstOppositePosition()) {
        // Infer the position from the final position ('auto / 1' or 'span 2 / 3' case).
        GridResolvedPosition finalResolvedPosition = resolveGridPositionFromStyle(gridContainerStyle, finalPosition, finalSide);
        return resolveGridPositionAgainstOppositePosition(gridContainerStyle, finalResolvedPosition, initialPosition, initialSide);
    }

    if (finalPosition.shouldBeResolvedAgainstOppositePosition()) {
        // Infer our position from the initial position ('1 / auto' or '3 / span 2' case).
        GridResolvedPosition initialResolvedPosition = resolveGridPositionFromStyle(gridContainerStyle, initialPosition, initialSide);
        return resolveGridPositionAgainstOppositePosition(gridContainerStyle, initialResolvedPosition, finalPosition, finalSide);
    }

    GridResolvedPosition resolvedInitialPosition = resolveGridPositionFromStyle(gridContainerStyle, initialPosition, initialSide);
    GridResolvedPosition resolvedFinalPosition = resolveGridPositionFromStyle(gridContainerStyle, finalPosition, finalSide);

    if (resolvedFinalPosition < resolvedInitialPosition)
        std::swap(resolvedFinalPosition, resolvedInitialPosition);
    else if (resolvedFinalPosition == resolvedInitialPosition)
        resolvedFinalPosition = resolvedInitialPosition.next();

    return GridSpan::definiteGridSpan(resolvedInitialPosition, resolvedFinalPosition);
}

} // namespace blink
