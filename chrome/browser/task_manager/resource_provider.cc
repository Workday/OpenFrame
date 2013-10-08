// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/task_manager/resource_provider.h"

#include "third_party/WebKit/public/web/WebCache.h"

namespace content {
class WebContents;
}

namespace extensions {
class Extension;
}

namespace task_manager {

int Resource::GetRoutingID() const {
  return 0;
}

bool Resource::ReportsCacheStats() const {
  return false;
}

WebKit::WebCache::ResourceTypeStats Resource::GetWebCoreCacheStats() const {
  return WebKit::WebCache::ResourceTypeStats();
}

bool Resource::ReportsFPS() const {
  return false;
}

float Resource::GetFPS() const {
  return 0.0f;
}

bool Resource::ReportsSqliteMemoryUsed() const {
  return false;
}

size_t Resource::SqliteMemoryUsedBytes() const {
  return 0;
}

const extensions::Extension* Resource::GetExtension() const {
  return NULL;
}

bool Resource::ReportsV8MemoryStats() const {
  return false;
}

size_t Resource::GetV8MemoryAllocated() const {
  return 0;
}

size_t Resource::GetV8MemoryUsed() const {
  return 0;
}

bool Resource::CanInspect() const {
  return false;
}

content::WebContents* Resource::GetWebContents() const {
  return NULL;
}

bool Resource::IsBackground() const {
  return false;
}

}  // namespace task_manager
