// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLIMP_CLIENT_ANDROID_BLIMP_LIBRARY_LOADER_H_
#define BLIMP_CLIENT_ANDROID_BLIMP_LIBRARY_LOADER_H_

#include <jni.h>

namespace blimp {

bool RegisterBlimpLibraryLoaderJni(JNIEnv* env);

}  // namespace blimp

#endif  // BLIMP_CLIENT_ANDROID_BLIMP_LIBRARY_LOADER_H_
