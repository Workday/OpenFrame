// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/layout/LayoutTestHelper.h"

#include "core/frame/FrameHost.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/GraphicsLayerFactory.h"

namespace blink {

class FakeGraphicsLayerFactory : public GraphicsLayerFactory {
public:
    PassOwnPtr<GraphicsLayer> createGraphicsLayer(GraphicsLayerClient* client) override
    {
        return adoptPtr(new GraphicsLayer(client));
    }
};

class FakeChromeClient : public EmptyChromeClient {
public:
    static PassOwnPtrWillBeRawPtr<FakeChromeClient> create() { return adoptPtrWillBeNoop(new FakeChromeClient); }

    virtual GraphicsLayerFactory* graphicsLayerFactory() const
    {
        static FakeGraphicsLayerFactory* factory = adoptPtr(new FakeGraphicsLayerFactory).leakPtr();
        return factory;
    }
};

RenderingTest::RenderingTest(PassOwnPtrWillBeRawPtr<FrameLoaderClient> frameLoaderClient)
{
    Page::PageClients pageClients;
    fillWithEmptyClients(pageClients);
    DEFINE_STATIC_LOCAL(OwnPtrWillBePersistent<FakeChromeClient>, chromeClient, (FakeChromeClient::create()));
    pageClients.chromeClient = chromeClient.get();
    m_pageHolder = DummyPageHolder::create(IntSize(800, 600), &pageClients, frameLoaderClient, settingOverrider());
}

void RenderingTest::SetUp()
{
    // This ensures that the minimal DOM tree gets attached
    // correctly for tests that don't call setBodyInnerHTML.
    document().view()->updateAllLifecyclePhases();
}

void RenderingTest::TearDown()
{
    // We need to destroy most of the Blink structure here because derived tests may restore
    // RuntimeEnabledFeatures setting during teardown, which happens before our destructor
    // getting invoked, breaking the assumption that REF can't change during Blink lifetime.
    m_pageHolder = nullptr;
}

} // namespace blink
