// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_VECTOR_MATH_H_
#define MEDIA_BASE_VECTOR_MATH_H_

#include "media/base/media_export.h"

namespace media {
namespace vector_math {

// Required alignment for inputs and outputs to all vector math functions
enum { kRequiredAlignment = 16 };

// Selects runtime specific optimizations such as SSE.  Must be called prior to
// calling FMAC() or FMUL().  Called during media library initialization; most
// users should never have to call this.
MEDIA_EXPORT void Initialize();

// Multiply each element of |src| (up to |len|) by |scale| and add to |dest|.
// |src| and |dest| must be aligned by kRequiredAlignment.
MEDIA_EXPORT void FMAC(const float src[], float scale, int len, float dest[]);

// Multiply each element of |src| by |scale| and store in |dest|.  |src| and
// |dest| must be aligned by kRequiredAlignment.
MEDIA_EXPORT void FMUL(const float src[], float scale, int len, float dest[]);

}  // namespace vector_math
}  // namespace media

#endif  // MEDIA_BASE_VECTOR_MATH_H_
