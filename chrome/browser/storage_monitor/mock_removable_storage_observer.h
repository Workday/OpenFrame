// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_STORAGE_MONITOR_MOCK_REMOVABLE_STORAGE_OBSERVER_H_
#define CHROME_BROWSER_STORAGE_MONITOR_MOCK_REMOVABLE_STORAGE_OBSERVER_H_

#include "chrome/browser/storage_monitor/removable_storage_observer.h"
#include "chrome/browser/storage_monitor/storage_info.h"

namespace chrome {

class MockRemovableStorageObserver : public RemovableStorageObserver {
 public:
  MockRemovableStorageObserver();
  virtual ~MockRemovableStorageObserver();

  virtual void OnRemovableStorageAttached(const StorageInfo& info) OVERRIDE;

  virtual void OnRemovableStorageDetached(const StorageInfo& info) OVERRIDE;

  int attach_calls() { return attach_calls_; }

  int detach_calls() { return detach_calls_; }

  const StorageInfo& last_attached() {
    return last_attached_;
  }

  const StorageInfo& last_detached() {
    return last_detached_;
  }

 private:
  int attach_calls_;
  int detach_calls_;
  StorageInfo last_attached_;
  StorageInfo last_detached_;
};

}  // namespace chrome

#endif  // CHROME_BROWSER_STORAGE_MONITOR_MOCK_REMOVABLE_STORAGE_OBSERVER_H_
