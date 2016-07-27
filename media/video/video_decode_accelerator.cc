// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/video/video_decode_accelerator.h"

#include <GLES2/gl2.h>
#include "base/logging.h"

namespace media {

void VideoDecodeAccelerator::Client::NotifyCdmAttached(bool success) {
  NOTREACHED() << "By default CDM is not supported.";
}

VideoDecodeAccelerator::~VideoDecodeAccelerator() {}

void VideoDecodeAccelerator::SetCdm(int cdm_id) {
  NOTREACHED() << "By default CDM is not supported.";
}

bool VideoDecodeAccelerator::CanDecodeOnIOThread() {
  // GPU process subclasses must override this.
  LOG(FATAL) << "This should only get called in the GPU process";
  return false;  // not reached
}

GLenum VideoDecodeAccelerator::GetSurfaceInternalFormat() const {
  return GL_RGBA;
}

VideoDecodeAccelerator::SupportedProfile::SupportedProfile()
    : profile(media::VIDEO_CODEC_PROFILE_UNKNOWN) {
}

VideoDecodeAccelerator::SupportedProfile::~SupportedProfile() {
}

} // namespace media

namespace std {

void default_delete<media::VideoDecodeAccelerator>::operator()(
    media::VideoDecodeAccelerator* vda) const {
  vda->Destroy();
}

}  // namespace std
