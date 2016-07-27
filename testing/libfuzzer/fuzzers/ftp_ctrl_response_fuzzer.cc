// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ftp/ftp_ctrl_response_buffer.h"
#include "net/log/net_log.h"

// Entry point for LibFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const unsigned char* data, size_t size) {
  const net::BoundNetLog log;
  net::FtpCtrlResponseBuffer buffer(log);
  if (!buffer.ConsumeData(reinterpret_cast<const char*>(data), size)) {
    return 0;
  }
  while (buffer.ResponseAvailable()) {
    (void)buffer.PopResponse();
  }
  return 0;
}
