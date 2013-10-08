// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/fake_safe_browsing_database_manager.h"

#include <algorithm>
#include <iterator>
#include <vector>

#include "base/bind_helpers.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop_proxy.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/browser/safe_browsing/safe_browsing_util.h"

namespace extensions {

FakeSafeBrowsingDatabaseManager::FakeSafeBrowsingDatabaseManager()
    : SafeBrowsingDatabaseManager(
          make_scoped_refptr(SafeBrowsingService::CreateSafeBrowsingService())),
      enabled_(false) {
}

FakeSafeBrowsingDatabaseManager::~FakeSafeBrowsingDatabaseManager() {
}

bool FakeSafeBrowsingDatabaseManager::CheckExtensionIDs(
    const std::set<std::string>& extension_ids,
    Client* client) {
  if (!enabled_)
    return true;

  // Need to construct the full SafeBrowsingCheck rather than calling
  // OnCheckExtensionsResult directly because it's protected. Grr!
  std::vector<std::string> extension_ids_vector(extension_ids.begin(),
                                                extension_ids.end());
  std::vector<SBFullHash> extension_id_hashes;
  std::transform(extension_ids_vector.begin(), extension_ids_vector.end(),
                 std::back_inserter(extension_id_hashes),
                 safe_browsing_util::StringToSBFullHash);

  scoped_ptr<SafeBrowsingCheck> safe_browsing_check(
      new SafeBrowsingCheck(std::vector<GURL>(),
                             extension_id_hashes,
                             client,
                             safe_browsing_util::EXTENSIONBLACKLIST));

  for (size_t i = 0; i < extension_ids_vector.size(); ++i) {
    const std::string& extension_id = extension_ids_vector[i];
    if (unsafe_ids_.count(extension_id))
      safe_browsing_check->full_hash_results[i] = SB_THREAT_TYPE_EXTENSION;
  }

  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(&FakeSafeBrowsingDatabaseManager::OnSafeBrowsingResult,
                 this,
                 base::Passed(&safe_browsing_check),
                 client));
  return false;
}

void FakeSafeBrowsingDatabaseManager::OnSafeBrowsingResult(
    scoped_ptr<SafeBrowsingCheck> result,
    Client* client) {
  client->OnSafeBrowsingResult(*result);
}

}  // namespace extensions
