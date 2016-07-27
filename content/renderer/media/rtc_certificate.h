// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_MEDIA_RTC_CERTIFICATE_H_
#define CONTENT_RENDERER_MEDIA_RTC_CERTIFICATE_H_

#include "base/macros.h"
#include "third_party/WebKit/public/platform/WebRTCCertificate.h"
#include "third_party/WebKit/public/platform/WebRTCKeyParams.h"
#include "third_party/webrtc/base/rtccertificate.h"
#include "third_party/webrtc/base/scoped_ref_ptr.h"

namespace content {

// Chromium's WebRTCCertificate implementation; wraps a rtc::scoped_refptr to an
// rtc::RTCCertificate. This abstraction layer is necessary because blink does
// not have direct access to WebRTC.
class RTCCertificate : public blink::WebRTCCertificate {
 public:
  RTCCertificate(const blink::WebRTCKeyParams& key_params,
                 const rtc::scoped_refptr<rtc::RTCCertificate>& certificate);
  ~RTCCertificate() override;

  // blink::WebRTCCertificate implementation.
  RTCCertificate* shallowCopy() const override;
  const blink::WebRTCKeyParams& keyParams() const override;
  double expires() const override;

  const rtc::scoped_refptr<rtc::RTCCertificate>& rtcCertificate() const;

 private:
  blink::WebRTCKeyParams key_params_;
  rtc::scoped_refptr<rtc::RTCCertificate> certificate_;

  DISALLOW_COPY_AND_ASSIGN(RTCCertificate);
};

}  // namespace content

#endif  // CONTENT_RENDERER_MEDIA_RTC_CERTIFICATE_H_
