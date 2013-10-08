// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/resource_metadata.h"

#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/sys_info.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#include "chrome/browser/chromeos/drive/resource_metadata_storage.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace drive {
namespace {

// Sets entry's base name from its title and other attributes.
void SetBaseNameFromTitle(ResourceEntry* entry) {
  std::string base_name = entry->title();
  if (entry->has_file_specific_info() &&
      entry->file_specific_info().is_hosted_document()) {
    base_name += entry->file_specific_info().document_extension();
  }
  entry->set_base_name(util::NormalizeFileName(base_name));
}

// Creates an entry by copying |source|, and setting the base name properly.
ResourceEntry CreateEntryWithProperBaseName(const ResourceEntry& source) {
  ResourceEntry entry(source);
  SetBaseNameFromTitle(&entry);
  return entry;
}

// Returns true if enough disk space is available for DB operation.
// TODO(hashimoto): Merge this with FileCache's FreeDiskSpaceGetterInterface.
bool EnoughDiskSpaceIsAvailableForDBOperation(const base::FilePath& path) {
  const int64 kRequiredDiskSpaceInMB = 128;  // 128 MB seems to be large enough.
  return base::SysInfo::AmountOfFreeDiskSpace(path) >=
      kRequiredDiskSpaceInMB * (1 << 20);
}

// Runs |callback| with arguments.
void RunGetResourceEntryCallback(const GetResourceEntryCallback& callback,
                                 scoped_ptr<ResourceEntry> entry,
                                 FileError error) {
  DCHECK(!callback.is_null());

  if (error != FILE_ERROR_OK)
    entry.reset();
  callback.Run(error, entry.Pass());
}

// Runs |callback| with arguments.
void RunReadDirectoryCallback(const ReadDirectoryCallback& callback,
                              scoped_ptr<ResourceEntryVector> entries,
                              FileError error) {
  DCHECK(!callback.is_null());

  if (error != FILE_ERROR_OK)
    entries.reset();
  callback.Run(error, entries.Pass());
}

// Runs |callback| with arguments.
void RunFileMoveCallback(const FileMoveCallback& callback,
                         base::FilePath* path,
                         FileError error) {
  DCHECK(!callback.is_null());
  DCHECK(path);

  callback.Run(error, *path);
}

// Helper function to run tasks with FileMoveCallback.
void PostFileMoveTask(
    base::TaskRunner* task_runner,
    const base::Callback<FileError(base::FilePath* out_file_path)>& task,
    const FileMoveCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!task.is_null());
  DCHECK(!callback.is_null());

  base::FilePath* file_path = new base::FilePath;

  base::PostTaskAndReplyWithResult(
      task_runner,
      FROM_HERE,
      base::Bind(task, file_path),
      base::Bind(&RunFileMoveCallback, callback, base::Owned(file_path)));
}

FileError AddEntryWithFilePath(internal::ResourceMetadata* metadata,
                               const ResourceEntry& entry,
                               base::FilePath* out_file_path) {
  DCHECK(metadata);
  DCHECK(out_file_path);
  FileError error = metadata->AddEntry(entry);
  if (error == FILE_ERROR_OK)
    *out_file_path = metadata->GetFilePath(entry.resource_id());
  return error;
}

}  // namespace

std::string DirectoryFetchInfo::ToString() const {
  return ("resource_id: " + resource_id_ +
          ", changestamp: " + base::Int64ToString(changestamp_));
}

EntryInfoResult::EntryInfoResult() : error(FILE_ERROR_FAILED) {
}

EntryInfoResult::~EntryInfoResult() {
}

EntryInfoPairResult::EntryInfoPairResult() {
}

EntryInfoPairResult::~EntryInfoPairResult() {
}

namespace internal {

ResourceMetadata::ResourceMetadata(
    ResourceMetadataStorage* storage,
    scoped_refptr<base::SequencedTaskRunner> blocking_task_runner)
    : blocking_task_runner_(blocking_task_runner),
      storage_(storage),
      weak_ptr_factory_(this) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

FileError ResourceMetadata::Initialize() {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  if (!SetUpDefaultEntries())
    return FILE_ERROR_FAILED;

  return FILE_ERROR_OK;
}

void ResourceMetadata::Destroy() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  weak_ptr_factory_.InvalidateWeakPtrs();
  blocking_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&ResourceMetadata::DestroyOnBlockingPool,
                 base::Unretained(this)));
}

void ResourceMetadata::ResetOnUIThread(const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  base::PostTaskAndReplyWithResult(
      blocking_task_runner_.get(),
      FROM_HERE,
      base::Bind(&ResourceMetadata::Reset, base::Unretained(this)),
      callback);
}

FileError ResourceMetadata::Reset() {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  if (!storage_->SetLargestChangestamp(0) ||
      !RemoveEntryRecursively(util::kDriveGrandRootSpecialResourceId) ||
      !SetUpDefaultEntries())
    return FILE_ERROR_FAILED;

  return FILE_ERROR_OK;
}

ResourceMetadata::~ResourceMetadata() {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
}

bool ResourceMetadata::SetUpDefaultEntries() {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  // Initialize the grand root and "other" entries. "/drive" and "/drive/other".
  // As an intermediate change, "/drive/root" is also added here.
  ResourceEntry entry;
  if (!storage_->GetEntry(util::kDriveGrandRootSpecialResourceId, &entry)) {
    ResourceEntry root;
    root.mutable_file_info()->set_is_directory(true);
    root.set_resource_id(util::kDriveGrandRootSpecialResourceId);
    root.set_title(util::kDriveGrandRootDirName);
    if (!storage_->PutEntry(CreateEntryWithProperBaseName(root)))
      return false;
  }
  if (!storage_->GetEntry(util::kDriveOtherDirSpecialResourceId, &entry)) {
    if (!PutEntryUnderDirectory(util::CreateOtherDirEntry()))
      return false;
  }
  return true;
}

void ResourceMetadata::DestroyOnBlockingPool() {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  delete this;
}

void ResourceMetadata::GetLargestChangestampOnUIThread(
    const GetChangestampCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());
  base::PostTaskAndReplyWithResult(
      blocking_task_runner_.get(),
      FROM_HERE,
      base::Bind(&ResourceMetadata::GetLargestChangestamp,
                 base::Unretained(this)),
      callback);
}

void ResourceMetadata::SetLargestChangestampOnUIThread(
    int64 value,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());
  base::PostTaskAndReplyWithResult(
      blocking_task_runner_.get(),
      FROM_HERE,
      base::Bind(&ResourceMetadata::SetLargestChangestamp,
                 base::Unretained(this),
                 value),
      callback);
}

int64 ResourceMetadata::GetLargestChangestamp() {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  return storage_->GetLargestChangestamp();
}

FileError ResourceMetadata::SetLargestChangestamp(int64 value) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  storage_->SetLargestChangestamp(value);
  return FILE_ERROR_OK;
}

void ResourceMetadata::AddEntryOnUIThread(const ResourceEntry& entry,
                                          const FileMoveCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  PostFileMoveTask(
      blocking_task_runner_.get(),
      base::Bind(&AddEntryWithFilePath, base::Unretained(this), entry),
      callback);
}

FileError ResourceMetadata::AddEntry(const ResourceEntry& entry) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  ResourceEntry existing_entry;
  if (storage_->GetEntry(entry.resource_id(), &existing_entry))
    return FILE_ERROR_EXISTS;

  ResourceEntry parent;
  if (!storage_->GetEntry(entry.parent_resource_id(), &parent) ||
      !parent.file_info().is_directory())
    return FILE_ERROR_NOT_FOUND;

  if (!PutEntryUnderDirectory(entry))
    return FILE_ERROR_FAILED;

  return FILE_ERROR_OK;
}

void ResourceMetadata::MoveEntryToDirectoryOnUIThread(
    const base::FilePath& file_path,
    const base::FilePath& directory_path,
    const FileMoveCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  PostFileMoveTask(blocking_task_runner_.get(),
                   base::Bind(&ResourceMetadata::MoveEntryToDirectory,
                              base::Unretained(this),
                              file_path,
                              directory_path),
                   callback);
}

void ResourceMetadata::RenameEntryOnUIThread(const base::FilePath& file_path,
                                             const std::string& new_name,
                                             const FileMoveCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  PostFileMoveTask(blocking_task_runner_.get(),
                   base::Bind(&ResourceMetadata::RenameEntry,
                              base::Unretained(this),
                              file_path,
                              new_name),
                   callback);
}

FileError ResourceMetadata::RemoveEntry(const std::string& resource_id) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  // Disallow deletion of special entries "/drive" and "/drive/other".
  if (util::IsSpecialResourceId(resource_id))
    return FILE_ERROR_ACCESS_DENIED;

  ResourceEntry entry;
  if (!storage_->GetEntry(resource_id, &entry))
    return FILE_ERROR_NOT_FOUND;

  if (!RemoveEntryRecursively(entry.resource_id()))
    return FILE_ERROR_FAILED;
  return FILE_ERROR_OK;
}

void ResourceMetadata::GetResourceEntryByIdOnUIThread(
    const std::string& resource_id,
    const GetResourceEntryCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  scoped_ptr<ResourceEntry> entry(new ResourceEntry);
  ResourceEntry* entry_ptr = entry.get();
  base::PostTaskAndReplyWithResult(
      blocking_task_runner_.get(),
      FROM_HERE,
      base::Bind(&ResourceMetadata::GetResourceEntryById,
                 base::Unretained(this),
                 resource_id,
                 entry_ptr),
      base::Bind(&RunGetResourceEntryCallback, callback, base::Passed(&entry)));
}

FileError ResourceMetadata::GetResourceEntryById(
    const std::string& resource_id,
    ResourceEntry* out_entry) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(!resource_id.empty());
  DCHECK(out_entry);

  return storage_->GetEntry(resource_id, out_entry) ?
      FILE_ERROR_OK : FILE_ERROR_NOT_FOUND;
}

void ResourceMetadata::GetResourceEntryByPathOnUIThread(
    const base::FilePath& file_path,
    const GetResourceEntryCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  scoped_ptr<ResourceEntry> entry(new ResourceEntry);
  ResourceEntry* entry_ptr = entry.get();
  base::PostTaskAndReplyWithResult(
      blocking_task_runner_.get(),
      FROM_HERE,
      base::Bind(&ResourceMetadata::GetResourceEntryByPath,
                 base::Unretained(this),
                 file_path,
                 entry_ptr),
      base::Bind(&RunGetResourceEntryCallback, callback, base::Passed(&entry)));
}

FileError ResourceMetadata::GetResourceEntryByPath(const base::FilePath& path,
                                                   ResourceEntry* out_entry) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(out_entry);

  return FindEntryByPathSync(path, out_entry) ?
      FILE_ERROR_OK : FILE_ERROR_NOT_FOUND;
}

void ResourceMetadata::ReadDirectoryByPathOnUIThread(
    const base::FilePath& file_path,
    const ReadDirectoryCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  scoped_ptr<ResourceEntryVector> entries(new ResourceEntryVector);
  ResourceEntryVector* entries_ptr = entries.get();
  base::PostTaskAndReplyWithResult(
      blocking_task_runner_.get(),
      FROM_HERE,
      base::Bind(&ResourceMetadata::ReadDirectoryByPath,
                 base::Unretained(this),
                 file_path,
                 entries_ptr),
      base::Bind(&RunReadDirectoryCallback, callback, base::Passed(&entries)));
}

FileError ResourceMetadata::ReadDirectoryByPath(
    const base::FilePath& path,
    ResourceEntryVector* out_entries) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(out_entries);

  ResourceEntry entry;
  if (!FindEntryByPathSync(path, &entry))
    return FILE_ERROR_NOT_FOUND;

  if (!entry.file_info().is_directory())
    return FILE_ERROR_NOT_A_DIRECTORY;

  std::vector<std::string> children;
  storage_->GetChildren(entry.resource_id(), &children);

  ResourceEntryVector entries(children.size());
  for (size_t i = 0; i < children.size(); ++i) {
    if (!storage_->GetEntry(children[i], &entries[i]))
      return FILE_ERROR_FAILED;
  }
  out_entries->swap(entries);
  return FILE_ERROR_OK;
}

FileError ResourceMetadata::RefreshEntry(const ResourceEntry& entry) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  ResourceEntry old_entry;
  if (!storage_->GetEntry(entry.resource_id(), &old_entry))
    return FILE_ERROR_NOT_FOUND;

  if (old_entry.parent_resource_id().empty() ||  // Reject root.
      old_entry.file_info().is_directory() !=  // Reject incompatible input.
      entry.file_info().is_directory())
    return FILE_ERROR_INVALID_OPERATION;

  // Update data.
  ResourceEntry new_parent;
  if (!storage_->GetEntry(entry.parent_resource_id(), &new_parent) ||
      !new_parent.file_info().is_directory())
    return FILE_ERROR_NOT_FOUND;

  // Remove from the old parent and add it to the new parent with the new data.
  if (!PutEntryUnderDirectory(CreateEntryWithProperBaseName(entry)))
    return FILE_ERROR_FAILED;
  return FILE_ERROR_OK;
}

void ResourceMetadata::RefreshDirectoryOnUIThread(
    const DirectoryFetchInfo& directory_fetch_info,
    const ResourceEntryMap& entry_map,
    const FileMoveCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  PostFileMoveTask(blocking_task_runner_.get(),
                   base::Bind(&ResourceMetadata::RefreshDirectory,
                              base::Unretained(this),
                              directory_fetch_info,
                              entry_map),
                   callback);
}

void ResourceMetadata::GetChildDirectories(
    const std::string& resource_id,
    std::set<base::FilePath>* child_directories) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  std::vector<std::string> children;
  storage_->GetChildren(resource_id, &children);
  for (size_t i = 0; i < children.size(); ++i) {
    ResourceEntry entry;
    if (storage_->GetEntry(children[i], &entry) &&
        entry.file_info().is_directory()) {
      child_directories->insert(GetFilePath(entry.resource_id()));
      GetChildDirectories(entry.resource_id(), child_directories);
    }
  }
}

std::string ResourceMetadata::GetChildResourceId(
    const std::string& parent_resource_id, const std::string& base_name) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  return storage_->GetChild(parent_resource_id, base_name);
}

scoped_ptr<ResourceMetadata::Iterator> ResourceMetadata::GetIterator() {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  return storage_->GetIterator();
}

base::FilePath ResourceMetadata::GetFilePath(
    const std::string& resource_id) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  base::FilePath path;
  ResourceEntry entry;
  if (storage_->GetEntry(resource_id, &entry)) {
    if (!entry.parent_resource_id().empty())
      path = GetFilePath(entry.parent_resource_id());
    path = path.Append(base::FilePath::FromUTF8Unsafe(entry.base_name()));
  }
  return path;
}

FileError ResourceMetadata::MoveEntryToDirectory(
    const base::FilePath& file_path,
    const base::FilePath& directory_path,
    base::FilePath* out_file_path) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(!directory_path.empty());
  DCHECK(!file_path.empty());
  DCHECK(out_file_path);

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  ResourceEntry entry, destination;
  if (!FindEntryByPathSync(file_path, &entry) ||
      !FindEntryByPathSync(directory_path, &destination))
    return FILE_ERROR_NOT_FOUND;
  if (!destination.file_info().is_directory())
    return FILE_ERROR_NOT_A_DIRECTORY;

  entry.set_parent_resource_id(destination.resource_id());

  FileError error = RefreshEntry(entry);
  if (error == FILE_ERROR_OK)
    *out_file_path = GetFilePath(entry.resource_id());
  return error;
}

FileError ResourceMetadata::RenameEntry(
    const base::FilePath& file_path,
    const std::string& new_title,
    base::FilePath* out_file_path) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(!file_path.empty());
  DCHECK(!new_title.empty());
  DCHECK(out_file_path);

  DVLOG(1) << "RenameEntry " << file_path.value() << " to " << new_title;

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  ResourceEntry entry;
  if (!FindEntryByPathSync(file_path, &entry))
    return FILE_ERROR_NOT_FOUND;

  if (base::FilePath::FromUTF8Unsafe(new_title) == file_path.BaseName())
    return FILE_ERROR_EXISTS;

  entry.set_title(new_title);

  FileError error = RefreshEntry(entry);
  if (error == FILE_ERROR_OK)
    *out_file_path = GetFilePath(entry.resource_id());
  return error;
}

bool ResourceMetadata::FindEntryByPathSync(const base::FilePath& file_path,
                                           ResourceEntry* out_entry) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  // Start from the root.
  ResourceEntry entry;
  if (!storage_->GetEntry(util::kDriveGrandRootSpecialResourceId, &entry))
    return false;
  DCHECK(entry.parent_resource_id().empty());

  // Check the first component.
  std::vector<base::FilePath::StringType> components;
  file_path.GetComponents(&components);
  if (components.empty() ||
      base::FilePath(components[0]).AsUTF8Unsafe() != entry.base_name())
    return scoped_ptr<ResourceEntry>();

  // Iterate over the remaining components.
  for (size_t i = 1; i < components.size(); ++i) {
    const std::string component = base::FilePath(components[i]).AsUTF8Unsafe();
    const std::string resource_id = storage_->GetChild(entry.resource_id(),
                                                       component);
    if (resource_id.empty() || !storage_->GetEntry(resource_id, &entry))
      return false;
    DCHECK_EQ(entry.base_name(), component);
  }
  out_entry->Swap(&entry);
  return true;
}

void ResourceMetadata::GetResourceEntryPairByPathsOnUIThread(
    const base::FilePath& first_path,
    const base::FilePath& second_path,
    const GetResourceEntryPairCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  // Get the first entry.
  GetResourceEntryByPathOnUIThread(
      first_path,
      base::Bind(
          &ResourceMetadata::GetResourceEntryPairByPathsOnUIThreadAfterGetFirst,
          weak_ptr_factory_.GetWeakPtr(),
          first_path,
          second_path,
          callback));
}

FileError ResourceMetadata::RefreshDirectory(
    const DirectoryFetchInfo& directory_fetch_info,
    const ResourceEntryMap& entry_map,
    base::FilePath* out_file_path) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(!directory_fetch_info.empty());

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  ResourceEntry directory;
  if (!storage_->GetEntry(directory_fetch_info.resource_id(), &directory))
    return FILE_ERROR_NOT_FOUND;

  if (!directory.file_info().is_directory())
    return FILE_ERROR_NOT_A_DIRECTORY;

  directory.mutable_directory_specific_info()->set_changestamp(
      directory_fetch_info.changestamp());
  storage_->PutEntry(directory);

  // Go through the entry map. Handle existing entries and new entries.
  for (ResourceEntryMap::const_iterator it = entry_map.begin();
       it != entry_map.end(); ++it) {
    if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
      return FILE_ERROR_NO_LOCAL_SPACE;

    const ResourceEntry& entry = it->second;
    // Skip if the parent resource ID does not match. This is needed to
    // handle entries with multiple parents. For such entries, the first
    // parent is picked and other parents are ignored, hence some entries may
    // have a parent resource ID which does not match the target directory's.
    //
    // TODO(satorux): Move the filtering logic to somewhere more appropriate.
    // crbug.com/193525.
    if (entry.parent_resource_id() !=
        directory_fetch_info.resource_id()) {
      DVLOG(1) << "Wrong-parent entry rejected: " << entry.resource_id();
      continue;
    }

    if (!PutEntryUnderDirectory(CreateEntryWithProperBaseName(entry)))
      return FILE_ERROR_FAILED;
  }

  if (out_file_path)
    *out_file_path = GetFilePath(directory.resource_id());

  return FILE_ERROR_OK;
}

void ResourceMetadata::GetResourceEntryPairByPathsOnUIThreadAfterGetFirst(
    const base::FilePath& first_path,
    const base::FilePath& second_path,
    const GetResourceEntryPairCallback& callback,
    FileError error,
    scoped_ptr<ResourceEntry> entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  scoped_ptr<EntryInfoPairResult> result(new EntryInfoPairResult);
  result->first.path = first_path;
  result->first.error = error;
  result->first.entry = entry.Pass();

  // If the first one is not found, don't continue.
  if (error != FILE_ERROR_OK) {
    callback.Run(result.Pass());
    return;
  }

  // Get the second entry.
  GetResourceEntryByPathOnUIThread(
      second_path,
      base::Bind(
          &ResourceMetadata::
          GetResourceEntryPairByPathsOnUIThreadAfterGetSecond,
          weak_ptr_factory_.GetWeakPtr(),
          second_path,
          callback,
          base::Passed(&result)));
}

void ResourceMetadata::GetResourceEntryPairByPathsOnUIThreadAfterGetSecond(
    const base::FilePath& second_path,
    const GetResourceEntryPairCallback& callback,
    scoped_ptr<EntryInfoPairResult> result,
    FileError error,
    scoped_ptr<ResourceEntry> entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());
  DCHECK(result.get());

  result->second.path = second_path;
  result->second.error = error;
  result->second.entry = entry.Pass();

  callback.Run(result.Pass());
}

bool ResourceMetadata::PutEntryUnderDirectory(
    const ResourceEntry& entry) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  ResourceEntry updated_entry(entry);

  // The entry name may have been changed due to prior name de-duplication.
  // We need to first restore the file name based on the title before going
  // through name de-duplication again when it is added to another directory.
  SetBaseNameFromTitle(&updated_entry);

  // Do file name de-duplication - Keep changing |entry|'s name until there is
  // no other entry with the same name under the parent.
  int modifier = 0;
  std::string new_base_name = updated_entry.base_name();
  while (true) {
    const std::string existing_entry_id =
        storage_->GetChild(entry.parent_resource_id(), new_base_name);
    if (existing_entry_id.empty() || existing_entry_id == entry.resource_id())
      break;

    base::FilePath new_path =
        base::FilePath::FromUTF8Unsafe(updated_entry.base_name());
    new_path =
        new_path.InsertBeforeExtension(base::StringPrintf(" (%d)", ++modifier));
    // The new filename must be different from the previous one.
    DCHECK_NE(new_base_name, new_path.AsUTF8Unsafe());
    new_base_name = new_path.AsUTF8Unsafe();
  }
  updated_entry.set_base_name(new_base_name);

  // Add the entry to resource map.
  return storage_->PutEntry(updated_entry);
}

bool ResourceMetadata::RemoveEntryRecursively(
    const std::string& resource_id) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  ResourceEntry entry;
  if (!storage_->GetEntry(resource_id, &entry))
    return false;

  if (entry.file_info().is_directory()) {
    std::vector<std::string> children;
    storage_->GetChildren(resource_id, &children);
    for (size_t i = 0; i < children.size(); ++i) {
      if (!RemoveEntryRecursively(children[i]))
        return false;
    }
  }
  return storage_->RemoveEntry(resource_id);
}

}  // namespace internal
}  // namespace drive
