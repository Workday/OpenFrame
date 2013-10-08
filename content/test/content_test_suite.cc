// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/test/content_test_suite.h"

#include "base/logging.h"
#include "content/public/test/test_content_client_initializer.h"
#include "content/test/test_content_client.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(USE_AURA)
#include "ui/aura/test/test_aura_initializer.h"
#endif

#if defined(OS_MACOSX)
#include "base/mac/scoped_nsautorelease_pool.h"
#endif

namespace {

class TestInitializationListener : public testing::EmptyTestEventListener {
 public:
  TestInitializationListener() : test_content_client_initializer_(NULL) {
  }

  virtual void OnTestStart(const testing::TestInfo& test_info) OVERRIDE {
    test_content_client_initializer_ =
        new content::TestContentClientInitializer();
  }

  virtual void OnTestEnd(const testing::TestInfo& test_info) OVERRIDE {
    delete test_content_client_initializer_;
  }

 private:
  content::TestContentClientInitializer* test_content_client_initializer_;

  DISALLOW_COPY_AND_ASSIGN(TestInitializationListener);
};

}  // namespace

namespace content {

ContentTestSuite::ContentTestSuite(int argc, char** argv)
    : ContentTestSuiteBase(argc, argv) {
#if defined(USE_AURA)
  aura_initializer_.reset(new aura::test::TestAuraInitializer);
#endif
}

ContentTestSuite::~ContentTestSuite() {
}

void ContentTestSuite::Initialize() {
#if defined(OS_MACOSX)
  base::mac::ScopedNSAutoreleasePool autorelease_pool;
#endif

  ContentTestSuiteBase::Initialize();

  testing::TestEventListeners& listeners =
      testing::UnitTest::GetInstance()->listeners();
  listeners.Append(new TestInitializationListener);
}

ContentClient* ContentTestSuite::CreateClientForInitialization() {
  return new TestContentClient();
}

}  // namespace content
