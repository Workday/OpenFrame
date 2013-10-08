// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROME_QUOTA_PERMISSION_CONTEXT_H_
#define CHROME_BROWSER_CHROME_QUOTA_PERMISSION_CONTEXT_H_

#include "base/compiler_specific.h"
#include "content/public/browser/quota_permission_context.h"

class ChromeQuotaPermissionContext : public content::QuotaPermissionContext {
 public:
  ChromeQuotaPermissionContext();

  // The callback will be dispatched on the IO thread.
  virtual void RequestQuotaPermission(
      const GURL& origin_url,
      quota::StorageType type,
      int64 new_quota,
      int render_process_id,
      int render_view_id,
      const PermissionCallback& callback) OVERRIDE;

  void DispatchCallbackOnIOThread(
      const PermissionCallback& callback,
      QuotaPermissionResponse response);

 private:
  virtual ~ChromeQuotaPermissionContext();
};

#endif  // CHROME_BROWSER_CHROME_QUOTA_PERMISSION_CONTEXT_H_
