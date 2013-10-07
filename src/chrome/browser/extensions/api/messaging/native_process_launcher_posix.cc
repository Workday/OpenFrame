// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/messaging/native_process_launcher.h"

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/launch.h"

namespace extensions {

namespace {

const char kNativeMessagingDirectory[] =
#if defined(OFFICIAL_BUILD) && defined(OS_MACOSX)
    "/Library/Google/Chrome/NativeMessagingHosts";
#elif defined(OFFICIAL_BUILD) && !defined(OS_MACOSX)
    "/etc/opt/chrome/native-messaging-hosts";
#elif !defined(OFFICIAL_BUILD) && defined(OS_MACOSX)
    "/Library/Application Support/Chromium/NativeMessagingHosts";
#else
    "/etc/chromium/native-messaging-hosts";
#endif

}  // namespace

// static
base::FilePath NativeProcessLauncher::FindManifest(
    const std::string& native_host_name,
    std::string* error_message) {
  return base::FilePath(kNativeMessagingDirectory).Append(
      native_host_name + ".json");
}

// static
bool NativeProcessLauncher::LaunchNativeProcess(
    const CommandLine& command_line,
    base::PlatformFile* read_file,
    base::PlatformFile* write_file) {
  base::FileHandleMappingVector fd_map;

  int read_pipe_fds[2] = {0};
  if (HANDLE_EINTR(pipe(read_pipe_fds)) != 0) {
    LOG(ERROR) << "Bad read pipe";
    return false;
  }
  file_util::ScopedFD read_pipe_read_fd(&read_pipe_fds[0]);
  file_util::ScopedFD read_pipe_write_fd(&read_pipe_fds[1]);
  fd_map.push_back(std::make_pair(*read_pipe_write_fd, STDOUT_FILENO));

  int write_pipe_fds[2] = {0};
  if (HANDLE_EINTR(pipe(write_pipe_fds)) != 0) {
    LOG(ERROR) << "Bad write pipe";
    return false;
  }
  file_util::ScopedFD write_pipe_read_fd(&write_pipe_fds[0]);
  file_util::ScopedFD write_pipe_write_fd(&write_pipe_fds[1]);
  fd_map.push_back(std::make_pair(*write_pipe_read_fd, STDIN_FILENO));

  base::LaunchOptions options;
  options.fds_to_remap = &fd_map;
  int process_id;
  if (!base::LaunchProcess(command_line, options, &process_id)) {
    LOG(ERROR) << "Error launching process";
    return false;
  }

  // We will not be reading from the write pipe, nor writing from the read pipe.
  write_pipe_read_fd.reset();
  read_pipe_write_fd.reset();

  *read_file = *read_pipe_read_fd.release();
  *write_file = *write_pipe_write_fd.release();

  return true;
}

}  // namespace extensions
