// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "platform/testing/PaintPrinters.h"

#include "platform/graphics/paint/PaintChunk.h"
#include "platform/graphics/paint/PaintChunkProperties.h"
#include <iomanip> // NOLINT
#include <ostream> // NOLINT

namespace {
    class StreamStateSaver : private std::ios {
        WTF_MAKE_NONCOPYABLE(StreamStateSaver);
    public:
        StreamStateSaver(std::ios& other) : std::ios(nullptr), m_other(other) { copyfmt(other); }
        ~StreamStateSaver() { m_other.copyfmt(*this); }
    private:
        std::ios& m_other;
    };
} // unnamed namespace

namespace blink {

// basic_ostream::operator<<(const void*) is drunk.
static void PrintPointer(const void* ptr, std::ostream& os)
{
    StreamStateSaver saver(os);
    uintptr_t intPtr = reinterpret_cast<uintptr_t>(ptr);
    os << "0x" << std::setfill('0') << std::setw(sizeof(uintptr_t) * 2) << std::hex << intPtr;
}

void PrintTo(const ClipPaintPropertyNode& node, std::ostream* os)
{
    *os << "ClipPaintPropertyNode(clip=";
    PrintTo(node.clipRect(), os);
    *os << ", base=";
    PrintPointer(node.base(), *os);
    *os << ", parent=";
    PrintPointer(node.parent(), *os);
    *os << ")";
}

void PrintTo(const PaintChunk& chunk, std::ostream* os)
{
    *os << "PaintChunk(begin=" << chunk.beginIndex
        << ", end=" << chunk.endIndex
        << ", props=";
    PrintTo(chunk.properties, os);
    *os << ")";
}

void PrintTo(const PaintChunkProperties& properties, std::ostream* os)
{
    *os << "PaintChunkProperties(";
    if (properties.transform) {
        *os << "transform=";
        PrintTo(*properties.transform, os);
    }
    *os << ")";
}

void PrintTo(const TransformPaintPropertyNode& transformPaintProperty, std::ostream* os)
{
    *os << "TransformPaintPropertyNode(matrix=";
    PrintTo(transformPaintProperty.matrix(), os);
    *os << ", origin=";
    PrintTo(transformPaintProperty.origin(), os);
    *os << ")";
}

void PrintTo(const EffectPaintPropertyNode& effect, std::ostream* os)
{
    *os << "EffectPaintPropertyNode(opacity=" << effect.opacity() << ")";
}

} // namespace blink
