// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NOTIFICATIONS_NOTIFICATION_DELEGATE_H_
#define CHROME_BROWSER_NOTIFICATIONS_NOTIFICATION_DELEGATE_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "ui/message_center/notification_delegate.h"

namespace content {
class RenderViewHost;
}

// Delegate for a notification. This class has two roles: to implement callback
// methods for notification, and to provide an identity of the associated
// notification.
class NotificationDelegate : public message_center::NotificationDelegate {
 public:
  // Returns unique id of the notification.
  virtual std::string id() const = 0;

  // Returns the id of renderer process which creates the notification, or -1.
  virtual int process_id() const;

  // Returns the RenderViewHost that generated the notification, or NULL.
  virtual content::RenderViewHost* GetRenderViewHost() const = 0;

  // Lets the delegate know that no more rendering will be necessary.
  virtual void ReleaseRenderViewHost();

 protected:
  virtual ~NotificationDelegate() {}
};

#endif  // CHROME_BROWSER_NOTIFICATIONS_NOTIFICATION_DELEGATE_H_
