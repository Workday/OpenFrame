// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/shell/test_package_manager.h"

namespace mojo {
namespace shell {
namespace test {

TestPackageManager::TestPackageManager() {}
TestPackageManager::~TestPackageManager() {}

void TestPackageManager::SetApplicationManager(ApplicationManager* manager) {}
void TestPackageManager::FetchRequest(
    URLRequestPtr request,
    const Fetcher::FetchCallback& loader_callback) {}
uint32_t TestPackageManager::HandleWithContentHandler(
    Fetcher* fetcher,
    const Identity& source,
    const GURL& target_url,
    const CapabilityFilter& target_filter,
    InterfaceRequest<Application>* application_request) {
  return 0;
}

}  // namespace test
}  // namespace shell
}  // namespace mojo
