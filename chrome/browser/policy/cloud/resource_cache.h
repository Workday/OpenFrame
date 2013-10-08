// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_CLOUD_RESOURCE_CACHE_H_
#define CHROME_BROWSER_POLICY_CLOUD_RESOURCE_CACHE_H_

#include <map>
#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/files/file_path.h"
#include "base/threading/non_thread_safe.h"

namespace policy {

// Manages storage of data at a given path. The data is keyed by a key and
// a subkey, and can be queried by (key, subkey) or (key) lookups.
// The contents of the cache have to be manually cleared using Delete() or
// PurgeOtherSubkeys().
// Instances of this class can be created on any thread, but from then on must
// be always used from the same thread, and it must support file I/O.
class ResourceCache : public base::NonThreadSafe {
 public:
  explicit ResourceCache(const base::FilePath& cache_path);
  virtual ~ResourceCache();

  // Stores |data| under (key, subkey). Returns true if the store suceeded, and
  // false otherwise.
  bool Store(const std::string& key,
             const std::string& subkey,
             const std::string& data);

  // Loads the contents of (key, subkey) into |data| and returns true. Returns
  // false if (key, subkey) isn't found or if there is a problem reading the
  // data.
  bool Load(const std::string& key,
            const std::string& subkey,
            std::string* data);

  // Loads all the subkeys of |key| into |contents|.
  void LoadAllSubkeys(const std::string& key,
                      std::map<std::string, std::string>* contents);

  // Deletes (key, subkey).
  void Delete(const std::string& key, const std::string& subkey);

  // Deletes all keys not in |keys_to_keep|, along with their subkeys.
  void PurgeOtherKeys(const std::set<std::string>& keys_to_keep);

  // Deletes all the subkeys of |key| not in |subkeys_to_keep|.
  void PurgeOtherSubkeys(const std::string& key,
                         const std::set<std::string>& subkeys_to_keep);

 private:
  // Points |path| at the cache directory for |key| and returns whether the
  // directory exists. If |allow_create| is |true|, the directory is created if
  // it did not exist yet.
  bool VerifyKeyPath(const std::string& key,
                     bool allow_create,
                     base::FilePath* path);

  // Points |path| at the file in which data for (key, subkey) should be stored
  // and returns whether the parent directory of this file exists. If
  // |allow_create_key| is |true|, the directory is created if it did not exist
  // yet. This method does not check whether the file at |path| exists or not.
  bool VerifyKeyPathAndGetSubkeyPath(const std::string& key,
                                     bool allow_create_key,
                                     const std::string& subkey,
                                     base::FilePath* subkey_path);

  base::FilePath cache_dir_;

  DISALLOW_COPY_AND_ASSIGN(ResourceCache);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_CLOUD_RESOURCE_CACHE_H_
