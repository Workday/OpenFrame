// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_FAKE_SAFE_BROWSING_DATABASE_MANAGER_H_
#define CHROME_BROWSER_EXTENSIONS_FAKE_SAFE_BROWSING_DATABASE_MANAGER_H_

#include <set>
#include <string>

#include "chrome/browser/safe_browsing/database_manager.h"

namespace extensions {

// A fake safe browsing database manager for use with extensions tests.
//
// By default it is disabled (returning true and ignoring |unsafe_ids_|);
// call set_enabled to enable it.
class FakeSafeBrowsingDatabaseManager : public SafeBrowsingDatabaseManager {
 public:
  FakeSafeBrowsingDatabaseManager();

  // Returns true if synchronously safe, false if not in which case the unsafe
  // IDs taken from |unsafe_ids_| are passed to to |client| on the current
  // message loop.
  virtual bool CheckExtensionIDs(const std::set<std::string>& extension_ids,
                                 Client* client) OVERRIDE;

  void set_enabled(bool enabled) { enabled_ = enabled; }

  void set_unsafe_ids(const std::set<std::string>& unsafe_ids) {
    unsafe_ids_ = unsafe_ids;
  }

 private:
  virtual ~FakeSafeBrowsingDatabaseManager();

  // Runs client->SafeBrowsingResult(result).
  void OnSafeBrowsingResult(scoped_ptr<SafeBrowsingCheck> result,
                            Client* client);

  // Whether to respond to CheckExtensionIDs immediately with true (indicating
  // that there is definitely no extension ID match).
  bool enabled_;

  // The extension IDs considered unsafe.
  std::set<std::string> unsafe_ids_;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_FAKE_SAFE_BROWSING_DATABASE_MANAGER_H_
