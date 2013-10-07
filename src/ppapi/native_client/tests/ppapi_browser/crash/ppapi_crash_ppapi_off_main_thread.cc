// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Test that we kill the nexe on a CHECK and handle it gracefully on the
// trusted side when untrusted code makes unsupported PPAPI calls
// on other than the main thread.

#include <pthread.h>

#include "native_client/src/shared/platform/nacl_check.h"
#include "ppapi/c/ppb_url_request_info.h"
#include "ppapi/native_client/tests/ppapi_test_lib/get_browser_interface.h"
#include "ppapi/native_client/tests/ppapi_test_lib/test_interface.h"

namespace {

void* CrashOffMainThreadFunction(void* thread_arg) {
  printf("--- CrashPPAPIOffMainThreadFunction\n");
  CRASH;
  return NULL;
}


// This will crash PPP_Messaging::HandleMessage.
void CrashPPAPIOffMainThread() {
  printf("--- CrashPPAPIOffMainThread\n");
  pthread_t tid;
  void* thread_result;
  pthread_create(&tid, NULL /*attr*/, CrashOffMainThreadFunction, NULL);
  pthread_join(tid, &thread_result);  // Wait for the thread to crash.
}

}  // namespace

void SetupTests() {
  RegisterTest("CrashPPAPIOffMainThread", CrashPPAPIOffMainThread);
}

void SetupPluginInterfaces() {
  // none
}
