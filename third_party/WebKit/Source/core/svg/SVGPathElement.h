/*
 * Copyright (C) 2004, 2005, 2006, 2008 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2006, 2007 Rob Buis <buis@kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef SVGPathElement_h
#define SVGPathElement_h

#include "core/SVGNames.h"
#include "core/svg/SVGAnimatedNumber.h"
#include "core/svg/SVGAnimatedPath.h"
#include "core/svg/SVGGeometryElement.h"
#include "platform/heap/Handle.h"

namespace blink {

class SVGPathElement final : public SVGGeometryElement {
    DEFINE_WRAPPERTYPEINFO();
public:
    DECLARE_NODE_FACTORY(SVGPathElement);

    Path asPath() const override;

    float getTotalLength();
    PassRefPtrWillBeRawPtr<SVGPointTearOff> getPointAtLength(float distance);
    unsigned getPathSegAtLength(float distance);

    SVGAnimatedPath* path() { return m_path.get(); }
    SVGAnimatedNumber* pathLength() { return m_pathLength.get(); }

    DECLARE_VIRTUAL_TRACE();

private:
    explicit SVGPathElement(Document&);

    void svgAttributeChanged(const QualifiedName&) override;

    Node::InsertionNotificationRequest insertedInto(ContainerNode*) override;
    void removedFrom(ContainerNode*) override;

    void invalidateMPathDependencies();

    RefPtrWillBeMember<SVGAnimatedNumber> m_pathLength;
    RefPtrWillBeMember<SVGAnimatedPath> m_path;
};

} // namespace blink

#endif // SVGPathElement_h
