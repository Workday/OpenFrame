// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/notification_test_util.h"

MockNotificationDelegate::MockNotificationDelegate(const std::string& id)
    : id_(id) {
}

MockNotificationDelegate::~MockNotificationDelegate() {}

std::string MockNotificationDelegate::id() const {
  return id_;
}

content::RenderViewHost* MockNotificationDelegate::GetRenderViewHost() const {
  return NULL;
}
