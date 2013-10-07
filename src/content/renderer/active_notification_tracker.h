// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_ACTIVE_NOTIFICATION_TRACKER_H_
#define CONTENT_RENDERER_ACTIVE_NOTIFICATION_TRACKER_H_

#include <map>

#include "base/basictypes.h"
#include "base/containers/hash_tables.h"
#include "base/id_map.h"
#include "content/common/content_export.h"
#include "third_party/WebKit/public/web/WebNotification.h"

namespace WebKit {
class WebNotificationPermissionCallback;
}

namespace content {

// This class manages the set of active Notification objects in either
// a render or worker process.  This class should be accessed only on
// the main thread.
class CONTENT_EXPORT ActiveNotificationTracker {
 public:
  ActiveNotificationTracker();
  ~ActiveNotificationTracker();

  // Methods for tracking active notification objects.
  int RegisterNotification(const WebKit::WebNotification& notification);
  void UnregisterNotification(int id);
  bool GetId(const WebKit::WebNotification& notification, int& id);
  bool GetNotification(int id, WebKit::WebNotification* notification);

  // Methods for tracking active permission requests.
  int RegisterPermissionRequest(
      WebKit::WebNotificationPermissionCallback* callback);
  void OnPermissionRequestComplete(int id);
  WebKit::WebNotificationPermissionCallback* GetCallback(int id);

  // Clears out all active notifications.  Useful on page navigation.
  void Clear();

  // Detaches all active notifications from their presenter.  Necessary
  // when the Presenter is destroyed.
  void DetachAll();

 private:
  typedef std::map<WebKit::WebNotification, int> ReverseTable;

  // Tracking maps for active notifications and permission requests.
  IDMap<WebKit::WebNotification> notification_table_;
  ReverseTable reverse_notification_table_;
  IDMap<WebKit::WebNotificationPermissionCallback> callback_table_;

  DISALLOW_COPY_AND_ASSIGN(ActiveNotificationTracker);
};

}  // namespace content

#endif  // CONTENT_RENDERER_ACTIVE_NOTIFICATION_TRACKER_H_
