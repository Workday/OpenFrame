// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/file_manager/private_api_base.h"

#include "base/strings/string_number_conversions.h"
#include "chrome/browser/chromeos/drive/logging.h"

namespace file_manager {
namespace {

const int kSlowOperationThresholdMs = 500;  // In ms.

}  // namespace

LoggedAsyncExtensionFunction::LoggedAsyncExtensionFunction()
    : log_on_completion_(false) {
  start_time_  = base::Time::Now();
}

LoggedAsyncExtensionFunction::~LoggedAsyncExtensionFunction() {
}

void LoggedAsyncExtensionFunction::SendResponse(bool success) {
  int64 elapsed = (base::Time::Now() - start_time_).InMilliseconds();
  if (log_on_completion_) {
    drive::util::Log(logging::LOG_INFO,
                     "%s[%d] %s. (elapsed time: %sms)",
                     name().c_str(),
                     request_id(),
                     success ? "succeeded" : "failed",
                     base::Int64ToString(elapsed).c_str());
  } else if (elapsed >= kSlowOperationThresholdMs) {
    drive::util::Log(
        logging::LOG_WARNING,
        "PEFORMANCE WARNING: %s[%d] was slow. (elapsed time: %sms)",
        name().c_str(),
        request_id(),
        base::Int64ToString(elapsed).c_str());
  }

  AsyncExtensionFunction::SendResponse(success);
}

}  // namespace file_manager
