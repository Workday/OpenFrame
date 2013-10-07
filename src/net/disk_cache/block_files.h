// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See net/disk_cache/disk_cache.h for the public interface.

#ifndef NET_DISK_CACHE_BLOCK_FILES_H_
#define NET_DISK_CACHE_BLOCK_FILES_H_

#include <vector>

#include "base/files/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "net/base/net_export.h"
#include "net/disk_cache/addr.h"
#include "net/disk_cache/disk_format_base.h"
#include "net/disk_cache/mapped_file.h"

namespace base {
class ThreadChecker;
}

namespace disk_cache {

// An instance of this class represents the header of a block file in memory.
// Note that this class doesn't perform any file operation.
class NET_EXPORT_PRIVATE BlockHeader {
 public:
  BlockHeader();
  explicit BlockHeader(BlockFileHeader* header);
  explicit BlockHeader(MappedFile* file);
  BlockHeader(const BlockHeader& other);
  ~BlockHeader();

  // Creates a new entry on the allocation map, updating the apropriate
  // counters. |target| is the type of block to use (number of empty blocks),
  // and |size| is the actual number of blocks to use.
  bool CreateMapBlock(int target, int size, int* index);

  // Deletes the block pointed by |index|.
  void DeleteMapBlock(int index, int block_size);

  // Returns true if the specified block is used.
  bool UsedMapBlock(int index, int size);

  // Restores the "empty counters" and allocation hints.
  void FixAllocationCounters();

  // Returns true if the current block file should not be used as-is to store
  // more records. |block_count| is the number of blocks to allocate.
  bool NeedToGrowBlockFile(int block_count);

  // Returns the number of empty blocks for this file.
  int EmptyBlocks() const;

  // Returns true if the counters look OK.
  bool ValidateCounters() const;

  // Returns the size of the wrapped structure (BlockFileHeader).
  int Size() const;

  BlockFileHeader* operator->() { return header_; }
  void operator=(const BlockHeader& other) { header_ = other.header_; }
  BlockFileHeader* Get() { return header_; }

 private:
  BlockFileHeader* header_;
};

typedef std::vector<BlockHeader> BlockFilesBitmaps;

// This class handles the set of block-files open by the disk cache.
class NET_EXPORT_PRIVATE BlockFiles {
 public:
  explicit BlockFiles(const base::FilePath& path);
  ~BlockFiles();

  // Performs the object initialization. create_files indicates if the backing
  // files should be created or just open.
  bool Init(bool create_files);

  // Returns the file that stores a given address.
  MappedFile* GetFile(Addr address);

  // Creates a new entry on a block file. block_type indicates the size of block
  // to be used (as defined on cache_addr.h), block_count is the number of
  // blocks to allocate, and block_address is the address of the new entry.
  bool CreateBlock(FileType block_type, int block_count, Addr* block_address);

  // Removes an entry from the block files. If deep is true, the storage is zero
  // filled; otherwise the entry is removed but the data is not altered (must be
  // already zeroed).
  void DeleteBlock(Addr address, bool deep);

  // Close all the files and set the internal state to be initializad again. The
  // cache is being purged.
  void CloseFiles();

  // Sends UMA stats.
  void ReportStats();

  // Returns true if the blocks pointed by a given address are currently used.
  // This method is only intended for debugging.
  bool IsValid(Addr address);

 private:
  // Set force to true to overwrite the file if it exists.
  bool CreateBlockFile(int index, FileType file_type, bool force);
  bool OpenBlockFile(int index);

  // Attemp to grow this file. Fails if the file cannot be extended anymore.
  bool GrowBlockFile(MappedFile* file, BlockFileHeader* header);

  // Returns the appropriate file to use for a new block.
  MappedFile* FileForNewBlock(FileType block_type, int block_count);

  // Returns the next block file on this chain, creating new files if needed.
  MappedFile* NextFile(MappedFile* file);

  // Creates an empty block file and returns its index.
  int CreateNextBlockFile(FileType block_type);

  // Removes a chained block file that is now empty.
  bool RemoveEmptyFile(FileType block_type);

  // Restores the header of a potentially inconsistent file.
  bool FixBlockFileHeader(MappedFile* file);

  // Retrieves stats for the given file index.
  void GetFileStats(int index, int* used_count, int* load);

  // Returns the filename for a given file index.
  base::FilePath Name(int index);

  bool init_;
  char* zero_buffer_;  // Buffer to speed-up cleaning deleted entries.
  base::FilePath path_;  // Path to the backing folder.
  std::vector<MappedFile*> block_files_;  // The actual files.
  scoped_ptr<base::ThreadChecker> thread_checker_;

  FRIEND_TEST_ALL_PREFIXES(DiskCacheTest, BlockFiles_ZeroSizeFile);
  FRIEND_TEST_ALL_PREFIXES(DiskCacheTest, BlockFiles_TruncatedFile);
  FRIEND_TEST_ALL_PREFIXES(DiskCacheTest, BlockFiles_InvalidFile);
  FRIEND_TEST_ALL_PREFIXES(DiskCacheTest, BlockFiles_Stats);

  DISALLOW_COPY_AND_ASSIGN(BlockFiles);
};

}  // namespace disk_cache

#endif  // NET_DISK_CACHE_BLOCK_FILES_H_
