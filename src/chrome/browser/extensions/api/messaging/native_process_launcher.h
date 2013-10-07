// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_MESSAGING_NATIVE_PROCESS_LAUNCHER_H_
#define CHROME_BROWSER_EXTENSIONS_API_MESSAGING_NATIVE_PROCESS_LAUNCHER_H_

#include "base/callback_forward.h"
#include "base/memory/scoped_ptr.h"
#include "base/platform_file.h"
#include "base/process/process.h"
#include "ui/gfx/native_widget_types.h"

class CommandLine;
class GURL;

namespace base {
class FilePath;
}

namespace extensions {

class NativeProcessLauncher {
 public:
  enum LaunchResult {
    RESULT_SUCCESS,
    RESULT_INVALID_NAME,
    RESULT_NOT_FOUND,
    RESULT_FORBIDDEN,
    RESULT_FAILED_TO_START,
  };

  // Callback that's called after the process has been launched. |result| is
  // set to false in case of a failure. Handler must take ownership of the IO
  // handles.
  typedef base::Callback<void (LaunchResult result,
                               base::PlatformFile read_file,
                               base::PlatformFile write_file)> LaunchedCallback;

  static scoped_ptr<NativeProcessLauncher> CreateDefault(
      gfx::NativeView native_view);

  NativeProcessLauncher() {}
  virtual ~NativeProcessLauncher() {}

  // Finds native messaging host with the specified name and launches it
  // asynchronously. Also checks that the specified |origin| is permitted to
  // access the host. |callback| is called after the process has been started.
  // If the launcher is destroyed before the callback is called then the call is
  // canceled and the process is stopped if it has been started already (by
  // closing IO pipes).
  virtual void Launch(const GURL& origin,
                      const std::string& native_host_name,
                      LaunchedCallback callback) const = 0;

 protected:
  // The following two methods are platform specific and are implemented in
  // platform-specific .cc files.

  // Finds manifest file for the native messaging host |native_host_name|.
  static base::FilePath FindManifest(const std::string& native_host_name,
                                     std::string* error_message);

  // Launches native messaging process.
  static bool LaunchNativeProcess(
      const CommandLine& command_line,
      base::PlatformFile* read_file,
      base::PlatformFile* write_file);

 private:
  DISALLOW_COPY_AND_ASSIGN(NativeProcessLauncher);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_MESSAGING_NATIVE_PROCESS_LAUNCHER_H_
