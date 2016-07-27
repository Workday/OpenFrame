// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/at_exit.h"
#include "base/bind.h"
#include "base/test/launcher/unit_test_launcher.h"
#include "base/test/test_suite.h"
#include "third_party/mojo/src/mojo/edk/embedder/embedder.h"

#if defined(OS_ANDROID)
#include "base/android/jni_android.h"
#include "base/test/test_file_util.h"
#endif

int main(int argc, char** argv) {
#if defined(OS_ANDROID)
  JNIEnv* env = base::android::AttachCurrentThread();
  base::RegisterContentUriTestUtils(env);
#endif
  base::TestSuite test_suite(argc, argv);
  mojo::embedder::Init();
  return base::LaunchUnitTestsSerially(
      argc, argv,
      base::Bind(&base::TestSuite::Run, base::Unretained(&test_suite)));
}
