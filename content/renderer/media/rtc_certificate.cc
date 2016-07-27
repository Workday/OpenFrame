// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/rtc_certificate.h"

#include "content/renderer/media/peer_connection_identity_store.h"
#include "url/gurl.h"

namespace content {

RTCCertificate::RTCCertificate(
    const blink::WebRTCKeyParams& key_params,
    const rtc::scoped_refptr<rtc::RTCCertificate>& certificate)
    : key_params_(key_params), certificate_(certificate) {
  DCHECK(certificate_);
}

RTCCertificate::~RTCCertificate() {
}

RTCCertificate* RTCCertificate::shallowCopy() const {
  return new RTCCertificate(key_params_, certificate_);
}

const blink::WebRTCKeyParams& RTCCertificate::keyParams() const {
  return key_params_;
}

double RTCCertificate::expires() const {
  return static_cast<double>(certificate_->expires_timestamp_ns());
}

const rtc::scoped_refptr<rtc::RTCCertificate>&
RTCCertificate::rtcCertificate() const {
  return certificate_;
}

}  // namespace content
