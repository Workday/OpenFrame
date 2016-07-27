/*
 * Copyright (C) 2004, 2006, 2009, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2010 Torch Mobile (Beijing) Co. Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef HTMLCanvasElement_h
#define HTMLCanvasElement_h

#include "bindings/core/v8/ScriptValue.h"
#include "bindings/core/v8/UnionTypesCore.h"
#include "core/CoreExport.h"
#include "core/dom/DOMTypedArray.h"
#include "core/dom/Document.h"
#include "core/dom/DocumentVisibilityObserver.h"
#include "core/fileapi/FileCallback.h"
#include "core/html/HTMLElement.h"
#include "core/html/canvas/CanvasImageSource.h"
#include "core/imagebitmap/ImageBitmapSource.h"
#include "platform/geometry/FloatRect.h"
#include "platform/geometry/IntSize.h"
#include "platform/graphics/GraphicsTypes.h"
#include "platform/graphics/GraphicsTypes3D.h"
#include "platform/graphics/ImageBufferClient.h"
#include "platform/heap/Handle.h"

#define CanvasDefaultInterpolationQuality InterpolationLow

namespace blink {

class AffineTransform;
class CanvasContextCreationAttributes;
class CanvasRenderingContext;
class CanvasRenderingContextFactory;
class GraphicsContext;
class HTMLCanvasElement;
class Image;
class ImageBuffer;
class ImageBufferSurface;
class ImageData;
class IntSize;

class CORE_EXPORT HTMLCanvasElement final : public HTMLElement, public DocumentVisibilityObserver, public CanvasImageSource, public ImageBufferClient, public ImageBitmapSource {
    DEFINE_WRAPPERTYPEINFO();
    WILL_BE_USING_GARBAGE_COLLECTED_MIXIN(HTMLCanvasElement);
public:
    DECLARE_NODE_FACTORY(HTMLCanvasElement);
    ~HTMLCanvasElement() override;

    // Attributes and functions exposed to script
    int width() const { return size().width(); }
    int height() const { return size().height(); }

    const IntSize& size() const { return m_size; }

    void setWidth(int);
    void setHeight(int);

    void setSize(const IntSize& newSize)
    {
        if (newSize == size())
            return;
        m_ignoreReset = true;
        setWidth(newSize.width());
        setHeight(newSize.height());
        m_ignoreReset = false;
        reset();
    }

    // Called by HTMLCanvasElement's V8 bindings.
    ScriptValue getContext(ScriptState*, const String&, const CanvasContextCreationAttributes&);
    // Called by Document::getCSSCanvasContext as well as above getContext().
    CanvasRenderingContext* getCanvasRenderingContext(const String&, const CanvasContextCreationAttributes&);

    bool isPaintable() const;

    static String toEncodingMimeType(const String& mimeType);
    String toDataURL(const String& mimeType, const ScriptValue& qualityArgument, ExceptionState&) const;
    String toDataURL(const String& mimeType, ExceptionState& exceptionState) const { return toDataURL(mimeType, ScriptValue(), exceptionState); }

    void toBlob(FileCallback*, const String& mimeType, const ScriptValue& qualityArgument, ExceptionState&);
    void toBlob(FileCallback* callback, const String& mimeType, ExceptionState& exceptionState) { return toBlob(callback, mimeType, ScriptValue(), exceptionState); }

    // Used for rendering
    void didDraw(const FloatRect&);

    void paint(GraphicsContext*, const LayoutRect&);

    SkCanvas* drawingCanvas() const;
    void disableDeferral() const;
    SkCanvas* existingDrawingCanvas() const;

    void setRenderingContext(PassOwnPtrWillBeRawPtr<CanvasRenderingContext>);
    CanvasRenderingContext* renderingContext() const { return m_context.get(); }

    void ensureUnacceleratedImageBuffer();
    ImageBuffer* buffer() const;
    PassRefPtr<Image> copiedImage(SourceDrawingBuffer, AccelerationHint) const;
    void clearCopiedImage();

    SecurityOrigin* securityOrigin() const;
    bool originClean() const;
    void setOriginTainted() { m_originClean = false; }

    AffineTransform baseTransform() const;

    bool is3D() const;
    bool isAnimated2D() const;

    bool hasImageBuffer() const { return m_imageBuffer; }
    void discardImageBuffer();

    bool shouldAccelerate(const IntSize&) const;

    bool shouldBeDirectComposited() const;

    void prepareSurfaceForPaintingIfNeeded() const;

    const AtomicString imageSourceURL() const override;

    InsertionNotificationRequest insertedInto(ContainerNode*) override;

    // DocumentVisibilityObserver implementation
    void didChangeVisibilityState(PageVisibilityState) override;

    // CanvasImageSource implementation
    PassRefPtr<Image> getSourceImageForCanvas(SourceImageStatus*, AccelerationHint) const override;
    bool wouldTaintOrigin(SecurityOrigin*) const override;
    FloatSize elementSize() const override;
    bool isCanvasElement() const override { return true; }
    bool isOpaque() const override;

    // ImageBufferClient implementation
    void notifySurfaceInvalid() override;
    bool isDirty() override { return !m_dirtyRect.isEmpty(); }
    void didFinalizeFrame() override;
    void restoreCanvasMatrixClipStack(SkCanvas*) const override;

    void doDeferredPaintInvalidation();

    // ImageBitmapSource implementation
    IntSize bitmapSourceSize() const override;
    ScriptPromise createImageBitmap(ScriptState*, EventTarget&, int sx, int sy, int sw, int sh, ExceptionState&) override;

    DECLARE_VIRTUAL_TRACE();

    void createImageBufferUsingSurfaceForTesting(PassOwnPtr<ImageBufferSurface>);

    static void registerRenderingContextFactory(PassOwnPtr<CanvasRenderingContextFactory>);
    void updateExternallyAllocatedMemory() const;

    void styleDidChange(const ComputedStyle* oldStyle, const ComputedStyle& newStyle);

protected:
    void didMoveToNewDocument(Document& oldDocument) override;

private:
    explicit HTMLCanvasElement(Document&);

    using ContextFactoryVector = Vector<OwnPtr<CanvasRenderingContextFactory>>;
    static ContextFactoryVector& renderingContextFactories();
    static CanvasRenderingContextFactory* getRenderingContextFactory(int);

    void parseAttribute(const QualifiedName&, const AtomicString&, const AtomicString&) override;
    LayoutObject* createLayoutObject(const ComputedStyle&) override;
    void didRecalcStyle(StyleRecalcChange) override;
    bool areAuthorShadowsAllowed() const override { return false; }

    void reset();

    PassOwnPtr<ImageBufferSurface> createImageBufferSurface(const IntSize& deviceSize, int* msaaSampleCount);
    void createImageBuffer();
    void createImageBufferInternal(PassOwnPtr<ImageBufferSurface> externalSurface);
    bool shouldUseDisplayList(const IntSize& deviceSize);

    void setSurfaceSize(const IntSize&);

    bool paintsIntoCanvasBuffer() const;

    ImageData* toImageData(SourceDrawingBuffer) const;
    String toDataURLInternal(const String& mimeType, const double& quality, SourceDrawingBuffer) const;

    IntSize m_size;

    OwnPtrWillBeMember<CanvasRenderingContext> m_context;

    bool m_ignoreReset;
    FloatRect m_dirtyRect;

    mutable intptr_t m_externallyAllocatedMemory;

    bool m_originClean;

    // It prevents HTMLCanvasElement::buffer() from continuously re-attempting to allocate an imageBuffer
    // after the first attempt failed.
    mutable bool m_didFailToCreateImageBuffer;
    bool m_imageBufferIsClear;
    OwnPtr<ImageBuffer> m_imageBuffer;

    mutable RefPtr<Image> m_copiedImage; // FIXME: This is temporary for platforms that have to copy the image buffer to render (and for CSSCanvasValue).
};

} // namespace blink

#endif // HTMLCanvasElement_h
