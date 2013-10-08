// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_RESOURCE_METADATA_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_RESOURCE_METADATA_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/files/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/drive/file_errors.h"
#include "chrome/browser/chromeos/drive/resource_metadata_storage.h"

namespace base {
class SequencedTaskRunner;
}

namespace drive {

class ResourceEntry;

typedef std::vector<ResourceEntry> ResourceEntryVector;
typedef std::map<std::string /* resource_id */, ResourceEntry>
    ResourceEntryMap;

// Holds information needed to fetch contents of a directory.
// This object is copyable.
class DirectoryFetchInfo {
 public:
  DirectoryFetchInfo() : changestamp_(0) {}
  DirectoryFetchInfo(const std::string& resource_id,
                     int64 changestamp)
      : resource_id_(resource_id),
        changestamp_(changestamp) {
  }

  // Returns true if the object is empty.
  bool empty() const { return resource_id_.empty(); }

  // Resource ID of the directory.
  const std::string& resource_id() const { return resource_id_; }

  // Changestamp of the directory. The changestamp is used to determine if
  // the directory contents should be fetched.
  int64 changestamp() const { return changestamp_; }

  // Returns a string representation of this object.
  std::string ToString() const;

 private:
  const std::string resource_id_;
  const int64 changestamp_;
};

// Callback similar to FileOperationCallback but with a given |file_path|.
// Used for operations that change a file path like moving files.
typedef base::Callback<void(FileError error,
                            const base::FilePath& file_path)>
    FileMoveCallback;

// Used to get a resource entry from the file system.
// If |error| is not FILE_ERROR_OK, |entry_info| is set to NULL.
typedef base::Callback<void(FileError error,
                            scoped_ptr<ResourceEntry> entry)>
    GetResourceEntryCallback;

typedef base::Callback<void(FileError error,
                            scoped_ptr<ResourceEntryVector> entries)>
    ReadDirectoryCallback;

typedef base::Callback<void(int64)> GetChangestampCallback;

// This is a part of EntryInfoPairResult.
struct EntryInfoResult {
  EntryInfoResult();
  ~EntryInfoResult();

  base::FilePath path;
  FileError error;
  scoped_ptr<ResourceEntry> entry;
};

// The result of GetResourceEntryPairCallback(). Used to get a pair of entries
// in one function call.
struct EntryInfoPairResult {
  EntryInfoPairResult();
  ~EntryInfoPairResult();

  EntryInfoResult first;
  EntryInfoResult second;  // Only filled if the first entry is found.
};

// Used to receive the result from GetResourceEntryPairCallback().
typedef base::Callback<void(scoped_ptr<EntryInfoPairResult> pair_result)>
    GetResourceEntryPairCallback;

typedef base::Callback<void(const ResourceEntry& entry)> IterateCallback;

namespace internal {

// Storage for Drive Metadata.
// All methods must be run with |blocking_task_runner| unless otherwise noted.
class ResourceMetadata {
 public:
  typedef ResourceMetadataStorage::Iterator Iterator;

  ResourceMetadata(
      ResourceMetadataStorage* storage,
      scoped_refptr<base::SequencedTaskRunner> blocking_task_runner);

  // Initializes this object.
  // This method should be called before any other methods.
  FileError Initialize() WARN_UNUSED_RESULT;

  // Destroys this object.  This method posts a task to |blocking_task_runner_|
  // to safely delete this object.
  // Must be called on the UI thread.
  void Destroy();

  // Resets this object.
  // Must be called on the UI thread.
  void ResetOnUIThread(const FileOperationCallback& callback);

  // Synchronous version of ResetOnUIThread.
  FileError Reset();

  // Largest change timestamp that was the source of content for the current
  // state of the root directory.
  // Must be called on the UI thread.
  void GetLargestChangestampOnUIThread(const GetChangestampCallback& callback);
  void SetLargestChangestampOnUIThread(int64 value,
                                       const FileOperationCallback& callback);

  // Synchronous version of GetLargestChangestampOnUIThread.
  int64 GetLargestChangestamp();

  // Synchronous version of SetLargestChangestampOnUIThread.
  FileError SetLargestChangestamp(int64 value);

  // Runs AddEntry() on blocking pool. Upon completion, the |callback| will be
  // called with the new file path.
  // |callback| must not be null.
  // Must be called on the UI thread.
  void AddEntryOnUIThread(const ResourceEntry& entry,
                          const FileMoveCallback& callback);

  // Adds |entry| to the metadata tree based on its parent_resource_id
  // synchronously.
  FileError AddEntry(const ResourceEntry& entry);

  // Moves entry specified by |file_path| to the directory specified by
  // |directory_path| and calls the callback asynchronously. Removes the entry
  // from the previous parent. |callback| must not be null.
  // Must be called on the UI thread.
  void MoveEntryToDirectoryOnUIThread(const base::FilePath& file_path,
                                      const base::FilePath& directory_path,
                                      const FileMoveCallback& callback);

  // Renames entry specified by |file_path| with the new name |new_name| and
  // calls |callback| asynchronously. |callback| must not be null.
  // Must be called on the UI thread.
  void RenameEntryOnUIThread(const base::FilePath& file_path,
                             const std::string& new_name,
                             const FileMoveCallback& callback);

  // Removes entry with |resource_id| from its parent.
  FileError RemoveEntry(const std::string& resource_id);

  // Finds an entry (a file or a directory) by |resource_id|.
  // |callback| must not be null.
  // Must be called on the UI thread.
  void GetResourceEntryByIdOnUIThread(const std::string& resource_id,
                                      const GetResourceEntryCallback& callback);

  // Synchronous version of GetResourceEntryByIdOnUIThread().
  FileError GetResourceEntryById(const std::string& resource_id,
                                 ResourceEntry* out_entry);

  // Finds an entry (a file or a directory) by |file_path|.
  // |callback| must not be null.
  // Must be called on the UI thread.
  void GetResourceEntryByPathOnUIThread(
      const base::FilePath& file_path,
      const GetResourceEntryCallback& callback);

  // Synchronous version of GetResourceEntryByPathOnUIThread().
  FileError GetResourceEntryByPath(const base::FilePath& file_path,
                                   ResourceEntry* out_entry);

  // Finds and reads a directory by |file_path|.
  // |callback| must not be null.
  // Must be called on the UI thread.
  void ReadDirectoryByPathOnUIThread(const base::FilePath& file_path,
                                     const ReadDirectoryCallback& callback);

  // Synchronous version of ReadDirectoryByPathOnUIThread().
  FileError ReadDirectoryByPath(const base::FilePath& file_path,
                                ResourceEntryVector* out_entries);

  // Similar to GetResourceEntryByPath() but this function finds a pair of
  // entries by |first_path| and |second_path|. If the entry for
  // |first_path| is not found, this function does not try to get the
  // entry of |second_path|. |callback| must not be null.
  // Must be called on the UI thread.
  void GetResourceEntryPairByPathsOnUIThread(
      const base::FilePath& first_path,
      const base::FilePath& second_path,
      const GetResourceEntryPairCallback& callback);

  // Replaces an existing entry whose ID is |entry.resource_id()| with |entry|.
  FileError RefreshEntry(const ResourceEntry& entry);

  // Removes all child files of the directory pointed by
  // |directory_fetch_info| and replaces them with
  // |entry_map|. The changestamp of the directory will be updated per
  // |directory_fetch_info|. |callback| is called with the directory path.
  // |callback| must not be null.
  // Must be called on the UI thread.
  void RefreshDirectoryOnUIThread(
      const DirectoryFetchInfo& directory_fetch_info,
      const ResourceEntryMap& entry_map,
      const FileMoveCallback& callback);

  // Recursively get child directories of entry pointed to by |resource_id|.
  void GetChildDirectories(const std::string& resource_id,
                           std::set<base::FilePath>* child_directories);

  // Returns the resource id of the resource named |base_name| directly under
  // the directory with |parent_resource_id|.
  // If not found, empty string will be returned.
  std::string GetChildResourceId(
      const std::string& parent_resource_id, const std::string& base_name);

  // Returns an object to iterate over entries.
  scoped_ptr<Iterator> GetIterator();

  // Returns virtual file path of the entry.
  base::FilePath GetFilePath(const std::string& resource_id);

 private:
  // Note: Use Destroy() to delete this object.
  ~ResourceMetadata();

  // Sets up entries which should be present by default.
  bool SetUpDefaultEntries();

  // Used to implement Destroy().
  void DestroyOnBlockingPool();

  // Used to implement MoveEntryToDirectoryOnUIThread().
  FileError MoveEntryToDirectory(const base::FilePath& file_path,
                                 const base::FilePath& directory_path,
                                 base::FilePath* out_file_path);

  // Used to implement RenameEntryOnUIThread().
  FileError RenameEntry(const base::FilePath& file_path,
                        const std::string& new_name,
                        base::FilePath* out_file_path);

  // Used to implement RefreshDirectoryOnUIThread().
  FileError RefreshDirectory(const DirectoryFetchInfo& directory_fetch_info,
                             const ResourceEntryMap& entry_map,
                             base::FilePath* out_file_path);

  // Continues with GetResourceEntryPairByPathsOnUIThread after the first
  // entry has been asynchronously fetched. This fetches the second entry
  // only if the first was found.
  void GetResourceEntryPairByPathsOnUIThreadAfterGetFirst(
      const base::FilePath& first_path,
      const base::FilePath& second_path,
      const GetResourceEntryPairCallback& callback,
      FileError error,
      scoped_ptr<ResourceEntry> entry);

  // Continues with GetResourceEntryPairByPathsOnUIThread after the second
  // entry has been asynchronously fetched.
  void GetResourceEntryPairByPathsOnUIThreadAfterGetSecond(
      const base::FilePath& second_path,
      const GetResourceEntryPairCallback& callback,
      scoped_ptr<EntryInfoPairResult> result,
      FileError error,
      scoped_ptr<ResourceEntry> entry);

  // Searches for |file_path| synchronously.
  bool FindEntryByPathSync(const base::FilePath& file_path,
                           ResourceEntry* out_entry);

  // Puts an entry under its parent directory. Removes the child from the old
  // parent if there is. This method will also do name de-duplication to ensure
  // that the exposed presentation path does not have naming conflicts. Two
  // files with the same name "Foo" will be renames to "Foo (1)" and "Foo (2)".
  bool PutEntryUnderDirectory(const ResourceEntry& entry);

  // Removes the entry and its descendants.
  bool RemoveEntryRecursively(const std::string& resource_id);

  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;

  ResourceMetadataStorage* storage_;

  // This should remain the last member so it'll be destroyed first and
  // invalidate its weak pointers before other members are destroyed.
  base::WeakPtrFactory<ResourceMetadata> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ResourceMetadata);
};

}  // namespace internal
}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_RESOURCE_METADATA_H_
