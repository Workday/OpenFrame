// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_TEST_CONTENT_TEST_SUITE_H_
#define CONTENT_TEST_CONTENT_TEST_SUITE_H_

#include "base/compiler_specific.h"
#include "content/public/test/content_test_suite_base.h"

#if defined(OS_WIN)
#include "base/win/scoped_com_initializer.h"
#endif

#if defined(USE_AURA)
namespace aura {
namespace test {
class TestAuraInitializer;
}  // namespace test
}  // namespace aura
#endif

namespace content {

class ContentTestSuite : public ContentTestSuiteBase {
 public:
  ContentTestSuite(int argc, char** argv);
  virtual ~ContentTestSuite();

 protected:
  virtual void Initialize() OVERRIDE;

  virtual ContentClient* CreateClientForInitialization() OVERRIDE;

 private:
#if defined(OS_WIN)
  base::win::ScopedCOMInitializer com_initializer_;
#endif

#if defined(USE_AURA)
  scoped_ptr<aura::test::TestAuraInitializer> aura_initializer_;
#endif

  DISALLOW_COPY_AND_ASSIGN(ContentTestSuite);
};

}  // namespace content

#endif  // CONTENT_TEST_CONTENT_TEST_SUITE_H_
