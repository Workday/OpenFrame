// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NACL_HOST_NACL_FILE_HOST_H_
#define CHROME_BROWSER_NACL_HOST_NACL_FILE_HOST_H_

#include <string>

#include "base/callback_forward.h"
#include "base/memory/ref_counted.h"

class ExtensionInfoMap;
class GURL;
class NaClHostMessageFilter;

namespace base {
class FilePath;
}

namespace IPC {
class Message;
}

namespace nacl {
struct PnaclInstallProgress;
}

// Opens NaCl Files in the Browser process, on behalf of the NaCl plugin.

namespace nacl_file_host {
typedef base::Callback<void(bool)> InstallCallback;
typedef base::Callback<void(const nacl::PnaclInstallProgress&)>
    InstallProgressCallback;

// Ensure that PNaCl is installed.  Calls |done_callback| if PNaCl is already
// installed.  Otherwise, issues a request to install and calls |done_callback|
// after that request completes w/ success or failure.
// If a request to install is issued, then |progress_callback| is called
// with progress updates.
void EnsurePnaclInstalled(
    const InstallCallback& done_callback,
    const InstallProgressCallback& progress_callback);

// Open a PNaCl file (readonly) on behalf of the NaCl plugin.
void GetReadonlyPnaclFd(
    scoped_refptr<NaClHostMessageFilter> nacl_host_message_filter,
    const std::string& filename,
    IPC::Message* reply_msg);

// Return true if the filename requested is valid for opening.
// Sets file_to_open to the base::FilePath which we will attempt to open.
bool PnaclCanOpenFile(const std::string& filename,
                      base::FilePath* file_to_open);

// Opens a NaCl executable file for reading and executing.
void OpenNaClExecutable(
    scoped_refptr<NaClHostMessageFilter> nacl_host_message_filter,
    scoped_refptr<ExtensionInfoMap> extension_info_map,
    int render_view_id,
    const GURL& file_url,
    IPC::Message* reply_msg);

}  // namespace nacl_file_host

#endif  // CHROME_BROWSER_NACL_HOST_NACL_FILE_HOST_H_
