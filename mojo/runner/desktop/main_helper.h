// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_RUNNER_DESKTOP_MAIN_HELPER_H
#define MOJO_RUNNER_DESKTOP_MAIN_HELPER_H

#include "base/callback.h"
#include "url/gurl.h"

namespace mojo {
namespace runner {

// Helper method to start Mojo Runner code.
// If |mojo_url| is not empty, the given mojo application is started. Otherwise,
// an application must have been specified on the command line and it is run.
// |callback| is only called in the later case.
int RunnerMain(int argc, char** argv,
               const GURL& mojo_url,
               const base::Closure& callback);

}  // namespace runner
}  // namespace mojo

#endif  // MOJO_RUNNER_DESKTOP_MAIN_HELPER_H
