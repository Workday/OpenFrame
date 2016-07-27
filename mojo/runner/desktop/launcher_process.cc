// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <iostream>

#include "base/base_switches.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/message_loop/message_loop.h"
#include "base/path_service.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "mojo/runner/context.h"
#include "mojo/runner/switches.h"
#include "mojo/shell/switches.h"

namespace mojo {
namespace runner {

int LauncherProcessMain(const GURL& mojo_url, const base::Closure& callback) {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(switches::kMojoSingleProcess) &&
      !command_line->HasSwitch("gtest_list_tests"))
    command_line->AppendSwitch(switches::kEnableMultiprocess);
  command_line->AppendSwitch("use-new-edk");
  // http://crbug.com/546644
  command_line->AppendSwitch(switches::kMojoNoSandbox);

  base::PlatformThread::SetName("mojo_runner");

  // We want the shell::Context to outlive the MessageLoop so that pipes are
  // all gracefully closed / error-out before we try to shut the Context down.
  Context shell_context;
  {
    base::MessageLoop message_loop;
    base::FilePath shell_dir;
    PathService::Get(base::DIR_MODULE, &shell_dir);
    if (!shell_context.Init(shell_dir))
      return 0;

    if (mojo_url.is_empty()) {
      message_loop.PostTask(
          FROM_HERE,
          base::Bind(&Context::RunCommandLineApplication,
                     base::Unretained(&shell_context), base::Closure()));
    } else {
      message_loop.PostTask(FROM_HERE,
                            base::Bind(&mojo::runner::Context::Run,
                                       base::Unretained(&shell_context),
                                       mojo_url));
    }
    message_loop.Run();

    // Must be called before |message_loop| is destroyed.
    shell_context.Shutdown();
  }

  return 0;
}

}  // namespace runner
}  // namespace mojo
