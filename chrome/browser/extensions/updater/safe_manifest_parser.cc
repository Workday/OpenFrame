// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/updater/safe_manifest_parser.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/logging.h"
#include "chrome/common/chrome_utility_messages.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/utility_process_host.h"
#include "content/public/common/content_switches.h"
#include "ipc/ipc_message_macros.h"

using content::BrowserThread;

namespace extensions {

SafeManifestParser::SafeManifestParser(const std::string& xml,
                                       ManifestFetchData* fetch_data,
                                       const UpdateCallback& update_callback)
    : xml_(xml),
      fetch_data_(fetch_data),
      update_callback_(update_callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

void SafeManifestParser::Start() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!BrowserThread::PostTask(
          BrowserThread::IO, FROM_HERE,
          base::Bind(&SafeManifestParser::ParseInSandbox, this))) {
    NOTREACHED();
  }
}

SafeManifestParser::~SafeManifestParser() {
  // If we're using UtilityProcessHost, we may not be destroyed on
  // the UI or IO thread.
}

void SafeManifestParser::ParseInSandbox() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  content::UtilityProcessHost* host = content::UtilityProcessHost::Create(
      this,
      BrowserThread::GetMessageLoopProxyForThread(BrowserThread::UI).get());
  host->EnableZygote();
  host->Send(new ChromeUtilityMsg_ParseUpdateManifest(xml_));
}

bool SafeManifestParser::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(SafeManifestParser, message)
    IPC_MESSAGE_HANDLER(ChromeUtilityHostMsg_ParseUpdateManifest_Succeeded,
                        OnParseUpdateManifestSucceeded)
    IPC_MESSAGE_HANDLER(ChromeUtilityHostMsg_ParseUpdateManifest_Failed,
                        OnParseUpdateManifestFailed)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void SafeManifestParser::OnParseUpdateManifestSucceeded(
    const UpdateManifest::Results& results) {
  VLOG(2) << "parsing manifest succeeded (" << fetch_data_->full_url() << ")";
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  update_callback_.Run(*fetch_data_, &results);
}

void SafeManifestParser::OnParseUpdateManifestFailed(
    const std::string& error_message) {
  VLOG(2) << "parsing manifest failed (" << fetch_data_->full_url() << ")";
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  LOG(WARNING) << "Error parsing update manifest:\n" << error_message;
  update_callback_.Run(*fetch_data_, NULL);
}

}  // namespace extensions
