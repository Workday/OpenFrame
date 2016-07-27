// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/paint/SVGImagePainter.h"

#include "core/layout/ImageQualityController.h"
#include "core/layout/LayoutImageResource.h"
#include "core/layout/svg/LayoutSVGImage.h"
#include "core/layout/svg/SVGLayoutSupport.h"
#include "core/paint/LayoutObjectDrawingRecorder.h"
#include "core/paint/ObjectPainter.h"
#include "core/paint/PaintInfo.h"
#include "core/paint/SVGPaintContext.h"
#include "core/paint/TransformRecorder.h"
#include "core/svg/SVGImageElement.h"
#include "platform/graphics/GraphicsContext.h"
#include "third_party/skia/include/core/SkPicture.h"

namespace blink {

void SVGImagePainter::paint(const PaintInfo& paintInfo)
{
    if (paintInfo.phase != PaintPhaseForeground
        || m_layoutSVGImage.style()->visibility() == HIDDEN
        || !m_layoutSVGImage.imageResource()->hasImage())
        return;

    FloatRect boundingBox = m_layoutSVGImage.paintInvalidationRectInLocalCoordinates();
    if (!paintInfo.cullRect().intersectsCullRect(m_layoutSVGImage.localToParentTransform(), boundingBox))
        return;

    PaintInfo paintInfoBeforeFiltering(paintInfo);
    // Images cannot have children so do not call updateCullRect.
    TransformRecorder transformRecorder(*paintInfoBeforeFiltering.context, m_layoutSVGImage, m_layoutSVGImage.localToParentTransform());
    {
        SVGPaintContext paintContext(m_layoutSVGImage, paintInfoBeforeFiltering);
        if (paintContext.applyClipMaskAndFilterIfNecessary() && !LayoutObjectDrawingRecorder::useCachedDrawingIfPossible(*paintContext.paintInfo().context, m_layoutSVGImage, paintContext.paintInfo().phase, LayoutPoint())) {
            LayoutObjectDrawingRecorder recorder(*paintContext.paintInfo().context, m_layoutSVGImage, paintContext.paintInfo().phase, boundingBox, LayoutPoint());
            paintForeground(paintContext.paintInfo());
        }
    }

    if (m_layoutSVGImage.style()->outlineWidth()) {
        PaintInfo outlinePaintInfo(paintInfoBeforeFiltering);
        outlinePaintInfo.phase = PaintPhaseSelfOutline;
        ObjectPainter(m_layoutSVGImage).paintOutline(outlinePaintInfo, LayoutPoint(boundingBox.location()));
    }
}

void SVGImagePainter::paintForeground(const PaintInfo& paintInfo)
{
    const LayoutImageResource* imageResource = m_layoutSVGImage.imageResource();
    IntSize imageViewportSize = expandedIntSize(computeImageViewportSize());
    if (imageViewportSize.isEmpty())
        return;

    RefPtr<Image> image = imageResource->image(imageViewportSize, m_layoutSVGImage.style()->effectiveZoom());
    FloatRect destRect = m_layoutSVGImage.objectBoundingBox();
    FloatRect srcRect(0, 0, image->width(), image->height());

    SVGImageElement* imageElement = toSVGImageElement(m_layoutSVGImage.element());
    imageElement->preserveAspectRatio()->currentValue()->transformRect(destRect, srcRect);

    InterpolationQuality interpolationQuality = InterpolationDefault;
    interpolationQuality = ImageQualityController::imageQualityController()->chooseInterpolationQuality(paintInfo.context, &m_layoutSVGImage, image.get(), image.get(), LayoutSize(destRect.size()));

    InterpolationQuality previousInterpolationQuality = paintInfo.context->imageInterpolationQuality();
    paintInfo.context->setImageInterpolationQuality(interpolationQuality);
    paintInfo.context->drawImage(image.get(), destRect, srcRect, SkXfermode::kSrcOver_Mode);
    paintInfo.context->setImageInterpolationQuality(previousInterpolationQuality);
}

FloatSize SVGImagePainter::computeImageViewportSize() const
{
    ASSERT(m_layoutSVGImage.imageResource()->hasImage());

    if (toSVGImageElement(m_layoutSVGImage.element())->preserveAspectRatio()->currentValue()->align() != SVGPreserveAspectRatio::SVG_PRESERVEASPECTRATIO_NONE)
        return m_layoutSVGImage.objectBoundingBox().size();

    ImageResource* cachedImage = m_layoutSVGImage.imageResource()->cachedImage();

    // Images with preserveAspectRatio=none should force non-uniform
    // scaling. This can be achieved by setting the image's container size to
    // its viewport size (i.e. if a viewBox is available - use that - else use intrinsic size.)
    // See: http://www.w3.org/TR/SVG/single-page.html, 7.8 The 'preserveAspectRatio' attribute.
    Length intrinsicWidth;
    Length intrinsicHeight;
    FloatSize intrinsicRatio;
    cachedImage->computeIntrinsicDimensions(intrinsicWidth, intrinsicHeight, intrinsicRatio);
    return intrinsicRatio;
}

} // namespace blink
