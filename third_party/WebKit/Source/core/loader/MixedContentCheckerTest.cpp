// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/loader/MixedContentChecker.h"

#include "core/loader/EmptyClients.h"
#include "core/testing/DummyPageHolder.h"
#include "platform/network/ResourceResponse.h"
#include "platform/weborigin/KURL.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "testing/gmock/include/gmock/gmock-generated-function-mockers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "wtf/RefPtr.h"
#include <base/macros.h>

namespace blink {

TEST(MixedContentCheckerTest, IsMixedContent)
{
    struct TestCase {
        const char* origin;
        const char* target;
        bool expectation;
    } cases[] = {
        {"http://example.com/foo", "http://example.com/foo", false},
        {"http://example.com/foo", "https://example.com/foo", false},
        {"https://example.com/foo", "https://example.com/foo", false},
        {"https://example.com/foo", "wss://example.com/foo", false},
        {"https://example.com/foo", "http://example.com/foo", true},
        {"https://example.com/foo", "http://google.com/foo", true},
        {"https://example.com/foo", "ws://example.com/foo", true},
        {"https://example.com/foo", "ws://google.com/foo", true},
    };

    for (size_t i = 0; i < arraysize(cases); ++i) {
        const char* origin = cases[i].origin;
        const char* target = cases[i].target;
        bool expectation = cases[i].expectation;

        KURL originUrl(KURL(), origin);
        RefPtr<SecurityOrigin> securityOrigin(SecurityOrigin::create(originUrl));
        KURL targetUrl(KURL(), target);
        EXPECT_EQ(expectation, MixedContentChecker::isMixedContent(securityOrigin.get(), targetUrl)) << "Origin: " << origin << ", Target: " << target << ", Expectation: " << expectation;
    }
}

TEST(MixedContentCheckerTest, ContextTypeForInspector)
{
    OwnPtr<DummyPageHolder> dummyPageHolder = DummyPageHolder::create(IntSize(1, 1));
    dummyPageHolder->frame().document()->setSecurityOrigin(SecurityOrigin::createFromString("http://example.test"));

    ResourceRequest notMixedContent("https://example.test/foo.jpg");
    notMixedContent.setFrameType(WebURLRequest::FrameTypeAuxiliary);
    notMixedContent.setRequestContext(WebURLRequest::RequestContextScript);
    EXPECT_EQ(MixedContentChecker::ContextTypeNotMixedContent, MixedContentChecker::contextTypeForInspector(&dummyPageHolder->frame(), notMixedContent));

    dummyPageHolder->frame().document()->setSecurityOrigin(SecurityOrigin::createFromString("https://example.test"));
    EXPECT_EQ(MixedContentChecker::ContextTypeNotMixedContent, MixedContentChecker::contextTypeForInspector(&dummyPageHolder->frame(), notMixedContent));

    ResourceRequest blockableMixedContent("http://example.test/foo.jpg");
    blockableMixedContent.setFrameType(WebURLRequest::FrameTypeAuxiliary);
    blockableMixedContent.setRequestContext(WebURLRequest::RequestContextScript);
    EXPECT_EQ(MixedContentChecker::ContextTypeBlockable, MixedContentChecker::contextTypeForInspector(&dummyPageHolder->frame(), blockableMixedContent));

    ResourceRequest optionallyBlockableMixedContent("http://example.test/foo.jpg");
    blockableMixedContent.setFrameType(WebURLRequest::FrameTypeAuxiliary);
    blockableMixedContent.setRequestContext(WebURLRequest::RequestContextImage);
    EXPECT_EQ(MixedContentChecker::ContextTypeOptionallyBlockable, MixedContentChecker::contextTypeForInspector(&dummyPageHolder->frame(), blockableMixedContent));
}

namespace {

    class MockFrameLoaderClient : public EmptyFrameLoaderClient {
    public:
        MockFrameLoaderClient()
            : EmptyFrameLoaderClient()
        {
        }
        MOCK_METHOD4(didDisplayContentWithCertificateErrors, void(const KURL&, const CString&, const WebURL&, const CString&));
        MOCK_METHOD4(didRunContentWithCertificateErrors, void(const KURL&, const CString&, const WebURL&, const CString&));
    };

} // namespace

TEST(MixedContentCheckerTest, HandleCertificateError)
{
    MockFrameLoaderClient* client = new MockFrameLoaderClient;
    OwnPtr<DummyPageHolder> dummyPageHolder = DummyPageHolder::create(IntSize(1, 1), nullptr, adoptPtrWillBeNoop(client));

    KURL mainResourceUrl(KURL(), "https://example.test");
    KURL displayedUrl(KURL(), "https://example-displayed.test");
    KURL ranUrl(KURL(), "https://example-ran.test");

    dummyPageHolder->frame().document()->setURL(mainResourceUrl);
    ResourceRequest request1(ranUrl);
    request1.setRequestContext(WebURLRequest::RequestContextScript);
    request1.setFrameType(WebURLRequest::FrameTypeNone);
    ResourceResponse response1;
    response1.setURL(ranUrl);
    response1.setSecurityInfo("security info1");
    EXPECT_CALL(*client, didRunContentWithCertificateErrors(ranUrl, response1.getSecurityInfo(), WebURL(mainResourceUrl), CString()));
    MixedContentChecker::handleCertificateError(&dummyPageHolder->frame(), request1, response1);

    ResourceRequest request2(displayedUrl);
    request2.setRequestContext(WebURLRequest::RequestContextImage);
    request2.setFrameType(WebURLRequest::FrameTypeNone);
    ResourceResponse response2;
    ASSERT_EQ(MixedContentChecker::ContextTypeOptionallyBlockable, MixedContentChecker::contextTypeFromContext(request2.requestContext(), &dummyPageHolder->frame()));
    response2.setURL(displayedUrl);
    response2.setSecurityInfo("security info2");
    EXPECT_CALL(*client, didDisplayContentWithCertificateErrors(displayedUrl, response2.getSecurityInfo(), WebURL(mainResourceUrl), CString()));
    MixedContentChecker::handleCertificateError(&dummyPageHolder->frame(), request2, response2);
}

} // namespace blink
