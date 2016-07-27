// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/at_exit.h"
#include "base/command_line.h"
#include "mojo/application/public/cpp/application_connection.h"
#include "mojo/application/public/cpp/application_delegate.h"
#include "mojo/application/public/cpp/application_impl.h"
#include "mojo/runner/child/test_native_main.h"
#include "mojo/runner/init.h"
#include "mojo/shell/application_manager_apptests.mojom.h"

using mojo::shell::test::mojom::CreateInstanceForHandleTestPtr;

namespace {

class TargetApplicationDelegate : public mojo::ApplicationDelegate {
 public:
  TargetApplicationDelegate() {}
  ~TargetApplicationDelegate() override {}

 private:
  // mojo::ApplicationDelegate:
  void Initialize(mojo::ApplicationImpl* app) override {
    CreateInstanceForHandleTestPtr service;
    app->ConnectToService("mojo:mojo_shell_apptests", &service);
    service->Ping("From Target");
  }
  bool ConfigureIncomingConnection(
      mojo::ApplicationConnection* connection) override {
    return true;
  }

  DISALLOW_COPY_AND_ASSIGN(TargetApplicationDelegate);
};

}  // namespace

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);

  mojo::runner::InitializeLogging();

  TargetApplicationDelegate delegate;
  return mojo::runner::TestNativeMain(&delegate);
}
