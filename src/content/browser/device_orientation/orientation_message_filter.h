// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DEVICE_ORIENTATION_ORIENTATION_MESSAGE_FILTER_H_
#define CONTENT_BROWSER_DEVICE_ORIENTATION_ORIENTATION_MESSAGE_FILTER_H_

#include <map>

#include "content/browser/device_orientation/message_filter.h"

namespace content {

class OrientationMessageFilter : public DeviceOrientationMessageFilterOld {
 public:
  OrientationMessageFilter();

  // DeviceOrientationMessageFilter implementation.
  virtual bool OnMessageReceived(const IPC::Message& message,
                                 bool* message_was_ok) OVERRIDE;

 private:
  virtual ~OrientationMessageFilter();

  DISALLOW_COPY_AND_ASSIGN(OrientationMessageFilter);
};

}  // namespace content

#endif  // CONTENT_BROWSER_DEVICE_ORIENTATION_ORIENTATION_MESSAGE_FILTER_H_
