// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/test/launcher/unit_test_launcher.h"
#include "base/test/test_suite.h"
#include "ui/compositor/test/test_suite.h"

namespace {

#if !defined(USE_AURA)
class NoAtExitBaseTestSuite : public base::TestSuite {
 public:
  NoAtExitBaseTestSuite(int argc, char** argv)
      : base::TestSuite(argc, argv, false) {
  }
};

int RunTestSuite(int argc, char** argv) {
  return NoAtExitBaseTestSuite(argc, argv).Run();
}
#endif  // !defined(USE_AURA)

}  // namespace

int main(int argc, char** argv) {
#if defined(USE_AURA)
  ui::test::CompositorTestSuite test_suite(argc, argv);

  return base::LaunchUnitTests(
      argc, argv, base::Bind(&ui::test::CompositorTestSuite::Run,
                             base::Unretained(&test_suite)));
#else

#if !defined(OS_ANDROID)
  base::AtExitManager at_exit;
#endif
  return base::LaunchUnitTests(argc,
                               argv,
                               base::Bind(&RunTestSuite, argc, argv));
#endif
}
