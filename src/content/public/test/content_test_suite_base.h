// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_TEST_CONTENT_TEST_SUITE_BASE_H_
#define CONTENT_PUBLIC_TEST_CONTENT_TEST_SUITE_BASE_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/test/test_suite.h"

namespace content {

class ContentClient;

// A basis upon which test suites that use content can be built.  This suite
// initializes bits and pieces of content; see the implementation of Initialize
// for details.
class ContentTestSuiteBase : public base::TestSuite {
 protected:
  ContentTestSuiteBase(int argc, char** argv);

  virtual void Initialize() OVERRIDE;

  // Creates a ContentClient for use during test suite initialization.
  virtual ContentClient* CreateClientForInitialization() = 0;

  // If set to false, prevents Initialize() to load external libraries
  // to the process. By default loading is enabled.
  void set_external_libraries_enabled(bool val) {
    external_libraries_enabled_ = val;
  }

 private:
  bool external_libraries_enabled_;
  DISALLOW_COPY_AND_ASSIGN(ContentTestSuiteBase);
};

} //  namespace content

#endif  // CONTENT_PUBLIC_TEST_CONTENT_TEST_SUITE_BASE_H_
