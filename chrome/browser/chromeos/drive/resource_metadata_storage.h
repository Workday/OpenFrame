// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_RESOURCE_METADATA_STORAGE_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_RESOURCE_METADATA_STORAGE_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"

namespace base {
class SequencedTaskRunner;
}

namespace leveldb {
class DB;
class Iterator;
}

namespace drive {

class ResourceEntry;
class ResourceMetadataHeader;

namespace internal {

// Storage for ResourceMetadata which is responsible to manage resource
// entries and child-parent relationships between entries.
class ResourceMetadataStorage {
 public:
  // This should be incremented when incompatibility change is made to DB
  // format.
  //
  // Note: Skip '7' when incrementing this value next time. '7' has been used
  // in the period between r208686 and r213797.
  static const int kDBVersion = 6;

  // Object to iterate over entries stored in this storage.
  class Iterator {
   public:
    explicit Iterator(scoped_ptr<leveldb::Iterator> it);
    ~Iterator();

    // Returns true if this iterator cannot advance any more and does not point
    // to a valid entry. Get() and Advance() should not be called in such cases.
    bool IsAtEnd() const;

    // Returns the entry currently pointed by this object.
    const ResourceEntry& Get() const;

    // Gets the cache entry which corresponds to |entry_| if available.
    bool GetCacheEntry(FileCacheEntry* cache_entry);

    // Advances to the next entry.
    void Advance();

    // Returns true if this object has encountered any error.
    bool HasError() const;

   private:
    ResourceEntry entry_;
    scoped_ptr<leveldb::Iterator> it_;

    DISALLOW_COPY_AND_ASSIGN(Iterator);
  };

  // Object to iterate over cache entries stored in this storage.
  class CacheEntryIterator {
   public:
    explicit CacheEntryIterator(scoped_ptr<leveldb::Iterator> it);
    ~CacheEntryIterator();

    // Returns true if this iterator cannot advance any more and does not point
    // to a valid entry. GetID(), GetValue() and Advance() should not be called
    // in such cases.
    bool IsAtEnd() const;

    // Returns the ID of the entry currently pointed by this object.
    const std::string& GetID() const;

    // Returns the value of the entry currently pointed by this object.
    const FileCacheEntry& GetValue() const;

    // Advances to the next entry.
    void Advance();

    // Returns true if this object has encountered any error.
    bool HasError() const;

   private:
    // Used to implement Advance().
    void AdvanceInternal();

    scoped_ptr<leveldb::Iterator> it_;
    std::string resource_id_;
    FileCacheEntry entry_;

    DISALLOW_COPY_AND_ASSIGN(CacheEntryIterator);
  };

  ResourceMetadataStorage(const base::FilePath& directory_path,
                          base::SequencedTaskRunner* blocking_task_runner);

  const base::FilePath& directory_path() const { return directory_path_; }

  // Returns true if the DB used by this storage was opened, not created, during
  // Initialize().
  bool opened_existing_db() const { return opened_existing_db_; }

  // Destroys this object.
  void Destroy();

  // Initializes this object.
  bool Initialize();

  // Sets the largest changestamp.
  bool SetLargestChangestamp(int64 largest_changestamp);

  // Gets the largest changestamp.
  int64 GetLargestChangestamp();

  // Puts the entry to this storage.
  bool PutEntry(const ResourceEntry& entry);

  // Gets an entry stored in this storage.
  bool GetEntry(const std::string& resource_id, ResourceEntry* out_entry);

  // Removes an entry from this storage.
  bool RemoveEntry(const std::string& resource_id);

  // Returns an object to iterate over entries stored in this storage.
  scoped_ptr<Iterator> GetIterator();

  // Returns resource ID of the parent's child.
  std::string GetChild(const std::string& parent_resource_id,
                       const std::string& child_name);

  // Returns resource IDs of the parent's children.
  void GetChildren(const std::string& parent_resource_id,
                   std::vector<std::string>* children);

  // Puts the cache entry to this storage.
  bool PutCacheEntry(const std::string& resource_id,
                     const FileCacheEntry& entry);

  // Gets a cache entry stored in this storage.
  bool GetCacheEntry(const std::string& resource_id, FileCacheEntry* out_entry);

  // Removes a cache entry from this storage.
  bool RemoveCacheEntry(const std::string& resource_id);

  // Returns an object to iterate over cache entries stored in this storage.
  scoped_ptr<CacheEntryIterator> GetCacheEntryIterator();

 private:
  friend class ResourceMetadataStorageTest;

  // To destruct this object, use Destroy().
  ~ResourceMetadataStorage();

  // Used to implement Destroy().
  void DestroyOnBlockingPool();

  // Returns a string to be used as a key for child entry.
  static std::string GetChildEntryKey(const std::string& parent_resource_id,
                                      const std::string& child_name);

  // Puts header.
  bool PutHeader(const ResourceMetadataHeader& header);

  // Gets header.
  bool GetHeader(ResourceMetadataHeader* out_header);

  // Checks validity of the data.
  bool CheckValidity();

  // Path to the directory where the data is stored.
  base::FilePath directory_path_;

  bool opened_existing_db_;

  // Entries stored in this storage.
  scoped_ptr<leveldb::DB> resource_map_;

  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;

  DISALLOW_COPY_AND_ASSIGN(ResourceMetadataStorage);
};

}  // namespace internal
}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_RESOURCE_METADATA_STORAGE_H_
