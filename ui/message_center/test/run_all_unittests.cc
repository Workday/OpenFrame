// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/test/test_suite.h"
#include "ui/views/view.h"

class MessageCenterTestSuite : public ui::test::UITestSuite {
 public:
  MessageCenterTestSuite(int argc, char** argv)
      : ui::test::UITestSuite(argc, argv) {
  }

 protected:
  virtual void Initialize() OVERRIDE;
  virtual void Shutdown() OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(MessageCenterTestSuite);
};

void MessageCenterTestSuite::Initialize() {
  ui::test::UITestSuite::Initialize();
}

void MessageCenterTestSuite::Shutdown() {
  ui::test::UITestSuite::Shutdown();
}

int main(int argc, char** argv) {
  return MessageCenterTestSuite(argc, argv).Run();
}
