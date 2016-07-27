// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WebRTCCertificate_h
#define WebRTCCertificate_h

#include "public/platform/WebRTCKeyParams.h"

namespace blink {

// WebRTCCertificate is an interface defining what Blink needs to know about certificates,
// hiding Chromium and WebRTC layer implementation details. It is possible to create
// shallow copies of the WebRTCCertificate. When all copies are destroyed, the
// implementation specific data must be freed. WebRTCCertificate objects thus act as
// references to the reference counted internal data.
class WebRTCCertificate {
public:
    WebRTCCertificate() = default;
    virtual ~WebRTCCertificate() = default;

    // Copies the WebRTCCertificate object without copying the underlying implementation
    // specific (WebRTC layer) certificate. When all copies are destroyed the underlying
    // data is freed.
    virtual WebRTCCertificate* shallowCopy() const = 0;

    virtual const WebRTCKeyParams& keyParams() const = 0;

    // The date and time after which the certificate should be considered invalid.
    // Expressed in time since 1970-01-01T00:00:00Z in milliseconds.
    virtual double expires() const = 0;

private:
    WebRTCCertificate(const WebRTCCertificate&) = delete;
    WebRTCCertificate& operator=(const WebRTCCertificate&) = delete;
};

} // namespace blink

#endif // WebRTCCertificate_h
