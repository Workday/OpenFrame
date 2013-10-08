// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/test/content_test_suite_base.h"

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/test/test_suite.h"
#include "base/threading/sequenced_worker_pool.h"
#include "content/browser/browser_thread_impl.h"
#include "content/common/url_schemes.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_paths.h"
#include "media/base/media.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/ui_base_paths.h"

#if defined(OS_ANDROID)
#include "base/android/jni_android.h"
#include "content/browser/android/browser_jni_registrar.h"
#include "content/common/android/common_jni_registrar.h"
#include "media/base/android/media_jni_registrar.h"
#include "net/android/net_jni_registrar.h"
#include "ui/android/ui_jni_registrar.h"
#include "ui/gl/android/gl_jni_registrar.h"
#include "ui/shell_dialogs/android/shell_dialogs_jni_registrar.h"
#endif

namespace content {

class ContentTestSuiteBaseListener : public testing::EmptyTestEventListener {
 public:
  ContentTestSuiteBaseListener() {
  }
  virtual void OnTestEnd(const testing::TestInfo& test_info) OVERRIDE {
    BrowserThreadImpl::FlushThreadPoolHelper();
  }
 private:
  DISALLOW_COPY_AND_ASSIGN(ContentTestSuiteBaseListener);
};

ContentTestSuiteBase::ContentTestSuiteBase(int argc, char** argv)
    : base::TestSuite(argc, argv),
      external_libraries_enabled_(true) {
}

void ContentTestSuiteBase::Initialize() {
  base::TestSuite::Initialize();

#if defined(OS_ANDROID)
  // Register JNI bindings for android.
  JNIEnv* env = base::android::AttachCurrentThread();
  content::android::RegisterCommonJni(env);
  content::android::RegisterBrowserJni(env);
  media::RegisterJni(env);
  net::android::RegisterJni(env);
  ui::android::RegisterJni(env);
  ui::shell_dialogs::RegisterJni(env);
#endif

  if (external_libraries_enabled_)
    media::InitializeMediaLibraryForTesting();

  scoped_ptr<ContentClient> client_for_init(CreateClientForInitialization());
  SetContentClient(client_for_init.get());
  RegisterContentSchemes(false);
  SetContentClient(NULL);

  RegisterPathProvider();
  ui::RegisterPathProvider();

  testing::UnitTest::GetInstance()->listeners().Append(
      new ContentTestSuiteBaseListener);
}

}  // namespace content
