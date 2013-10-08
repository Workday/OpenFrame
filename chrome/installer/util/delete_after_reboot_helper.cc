// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file defines helper methods used to schedule files for deletion
// on next reboot. The code here is heavily borrowed and simplified from
//  http://code.google.com/p/omaha/source/browse/trunk/common/file.cc and
//  http://code.google.com/p/omaha/source/browse/trunk/common/utils.cc
//
// This implementation really is not fast, so do not use it where that will
// matter.

#include "chrome/installer/util/delete_after_reboot_helper.h"

#include <string>
#include <vector>

#include "base/file_util.h"
#include "base/files/file_enumerator.h"
#include "base/strings/string_util.h"
#include "base/win/registry.h"

// The moves-pending-reboot is a MULTISZ registry key in the HKLM part of the
// registry.
const wchar_t kSessionManagerKey[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Session Manager";
const wchar_t kPendingFileRenameOps[] = L"PendingFileRenameOperations";

namespace {

// Returns true if this directory name is 'safe' for deletion (doesn't contain
// "..", doesn't specify a drive root)
bool IsSafeDirectoryNameForDeletion(const wchar_t* dir_name) {
  DCHECK(dir_name);

  // empty name isn't allowed
  if (!(dir_name && *dir_name))
    return false;

  // require a character other than \/:. after the last :
  // disallow anything with ".."
  bool ok = false;
  for (const wchar_t* s = dir_name; *s; ++s) {
    if (*s != L'\\' && *s != L'/' && *s != L':' && *s != L'.')
      ok = true;
    if (*s == L'.' && s > dir_name && *(s - 1) == L'.')
      return false;
    if (*s == L':')
      ok = false;
  }
  return ok;
}

}  // end namespace

// Must only be called for regular files or directories that will be empty.
bool ScheduleFileSystemEntityForDeletion(const wchar_t* path) {
  // Check if the file exists, return false if not.
  WIN32_FILE_ATTRIBUTE_DATA attrs = {0};
  if (!::GetFileAttributesEx(path, ::GetFileExInfoStandard, &attrs)) {
    PLOG(WARNING) << path << " does not exist.";
    return false;
  }

  DWORD flags = MOVEFILE_DELAY_UNTIL_REBOOT;
  if (!base::DirectoryExists(base::FilePath::FromWStringHack(path))) {
    // This flag valid only for files
    flags |= MOVEFILE_REPLACE_EXISTING;
  }

  if (!::MoveFileEx(path, NULL, flags)) {
    PLOG(ERROR) << "Could not schedule " << path << " for deletion.";
    return false;
  }

#ifndef NDEBUG
  // Useful debugging code to track down what files are in use.
  if (flags & MOVEFILE_REPLACE_EXISTING) {
    // Attempt to open the file exclusively.
    HANDLE file = ::CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, 0, NULL);
    if (file != INVALID_HANDLE_VALUE) {
      LOG(INFO) << " file not in use: " << path;
      ::CloseHandle(file);
    } else {
      PLOG(INFO) << " file in use (or not found?): " << path;
    }
  }
#endif

  VLOG(1) << "Scheduled for deletion: " << path;
  return true;
}

bool ScheduleDirectoryForDeletion(const wchar_t* dir_name) {
  if (!IsSafeDirectoryNameForDeletion(dir_name)) {
    LOG(ERROR) << "Unsafe directory name for deletion: " << dir_name;
    return false;
  }

  // Make sure the directory exists (it is ok if it doesn't)
  DWORD dir_attributes = ::GetFileAttributes(dir_name);
  if (dir_attributes == INVALID_FILE_ATTRIBUTES) {
    if (::GetLastError() == ERROR_FILE_NOT_FOUND) {
      return true;  // Ok if directory is missing
    } else {
      PLOG(ERROR) << "Could not GetFileAttributes for " << dir_name;
      return false;
    }
  }
  // Confirm it is a directory
  if (!(dir_attributes & FILE_ATTRIBUTE_DIRECTORY)) {
    LOG(ERROR) << "Scheduled directory is not a directory: " << dir_name;
    return false;
  }

  // First schedule all the normal files for deletion.
  {
    bool success = true;
    base::FileEnumerator file_enum(base::FilePath(dir_name), false,
                                   base::FileEnumerator::FILES);
    for (base::FilePath file = file_enum.Next(); !file.empty();
         file = file_enum.Next()) {
      success = ScheduleFileSystemEntityForDeletion(file.value().c_str());
      if (!success) {
        LOG(ERROR) << "Failed to schedule file for deletion: " << file.value();
        return false;
      }
    }
  }

  // Then recurse to all the subdirectories.
  {
    bool success = true;
    base::FileEnumerator dir_enum(base::FilePath(dir_name), false,
                                  base::FileEnumerator::DIRECTORIES);
    for (base::FilePath sub_dir = dir_enum.Next(); !sub_dir.empty();
         sub_dir = dir_enum.Next()) {
      success = ScheduleDirectoryForDeletion(sub_dir.value().c_str());
      if (!success) {
        LOG(ERROR) << "Failed to schedule subdirectory for deletion: "
                   << sub_dir.value();
        return false;
      }
    }
  }

  // Now schedule the empty directory itself
  if (!ScheduleFileSystemEntityForDeletion(dir_name))
    LOG(ERROR) << "Failed to schedule directory for deletion: " << dir_name;

  return true;
}

// Converts the strings found in |buffer| to a list of wstrings that is returned
// in |value|.
// |buffer| points to a series of pairs of null-terminated wchar_t strings
// followed by a terminating null character.
// |byte_count| is the length of |buffer| in bytes.
// |value| is a pointer to an empty vector of wstrings. On success, this vector
// contains all of the strings extracted from |buffer|.
// Returns S_OK on success, E_INVALIDARG if buffer does not meet tha above
// specification.
HRESULT MultiSZBytesToStringArray(const char* buffer, size_t byte_count,
                                  std::vector<PendingMove>* value) {
  DCHECK(buffer);
  DCHECK(value);
  DCHECK(value->empty());

  DWORD data_len = byte_count / sizeof(wchar_t);
  const wchar_t* data = reinterpret_cast<const wchar_t*>(buffer);
  const wchar_t* data_end = data + data_len;
  if (data_len > 1) {
    // must be terminated by two null characters
    if (data[data_len - 1] != 0 || data[data_len - 2] != 0) {
      DLOG(ERROR) << "Invalid MULTI_SZ found.";
      return E_INVALIDARG;
    }

    // put null-terminated strings into arrays
    while (data < data_end) {
      std::wstring str_from(data);
      data += str_from.length() + 1;
      if (data < data_end) {
        std::wstring str_to(data);
        data += str_to.length() + 1;
        value->push_back(std::make_pair(str_from, str_to));
      }
    }
  }
  return S_OK;
}

void StringArrayToMultiSZBytes(const std::vector<PendingMove>& strings,
                               std::vector<char>* buffer) {
  DCHECK(buffer);
  buffer->clear();

  if (strings.empty()) {
    // Leave buffer empty if we have no strings.
    return;
  }

  size_t total_wchars = 0;
  {
    std::vector<PendingMove>::const_iterator iter(strings.begin());
    for (; iter != strings.end(); ++iter) {
      total_wchars += iter->first.length();
      total_wchars++;  // Space for the null char.
      total_wchars += iter->second.length();
      total_wchars++;  // Space for the null char.
    }
    total_wchars++;  // Space for the extra terminating null char.
  }

  size_t total_length = total_wchars * sizeof(wchar_t);
  buffer->resize(total_length);
  wchar_t* write_pointer = reinterpret_cast<wchar_t*>(&((*buffer)[0]));
  // Keep an end pointer around for sanity checking.
  wchar_t* end_pointer = write_pointer + total_wchars;

  std::vector<PendingMove>::const_iterator copy_iter(strings.begin());
  for (; copy_iter != strings.end() && write_pointer < end_pointer;
       copy_iter++) {
    // First copy the source string.
    size_t string_length = copy_iter->first.length() + 1;
    memcpy(write_pointer, copy_iter->first.c_str(),
           string_length * sizeof(wchar_t));
    write_pointer += string_length;
    // Now copy the destination string.
    string_length = copy_iter->second.length() + 1;
    memcpy(write_pointer, copy_iter->second.c_str(),
           string_length * sizeof(wchar_t));
    write_pointer += string_length;

    // We should never run off the end while in this loop.
    DCHECK(write_pointer < end_pointer);
  }
  *write_pointer = L'\0';  // Explicitly set the final null char.
  DCHECK(++write_pointer == end_pointer);
}

std::wstring GetShortPathName(const wchar_t* path) {
  std::wstring short_path;
  DWORD length = GetShortPathName(path, WriteInto(&short_path, MAX_PATH),
                                  MAX_PATH);
  DWORD last_error = ::GetLastError();
  DLOG_IF(WARNING, length == 0 && last_error != ERROR_PATH_NOT_FOUND)
      << __FUNCTION__ << " gle=" << last_error;
  if (length == 0) {
    // GetShortPathName fails if the path is no longer present. Instead of
    // returning an empty string, just return the original string. This will
    // serve our purposes.
    return path;
  }

  short_path.resize(length);
  return short_path;
}

HRESULT GetPendingMovesValue(
    std::vector<PendingMove>* pending_moves) {
  DCHECK(pending_moves);
  pending_moves->clear();

  // Get the current value of the key
  // If the Key is missing, that's totally acceptable.
  base::win::RegKey session_manager_key(HKEY_LOCAL_MACHINE, kSessionManagerKey,
                                        KEY_QUERY_VALUE);
  HKEY session_manager_handle = session_manager_key.Handle();
  if (!session_manager_handle)
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

  // The base::RegKey Read code squashes the return code from
  // ReqQueryValueEx, we have to do things ourselves:
  DWORD buffer_size = 0;
  std::vector<char> buffer;
  buffer.resize(1);
  DWORD type;
  DWORD result = RegQueryValueEx(session_manager_handle, kPendingFileRenameOps,
                                 0, &type, reinterpret_cast<BYTE*>(&buffer[0]),
                                 &buffer_size);

  if (result == ERROR_FILE_NOT_FOUND) {
    // No pending moves were found.
    return HRESULT_FROM_WIN32(result);
  }
  if (result != ERROR_MORE_DATA) {
    // That was unexpected.
    DLOG(ERROR) << "Unexpected result from RegQueryValueEx: " << result;
    return HRESULT_FROM_WIN32(result);
  }
  if (type != REG_MULTI_SZ) {
    DLOG(ERROR) << "Found PendingRename value of unexpected type.";
    return E_UNEXPECTED;
  }
  if (buffer_size % 2) {
    // The buffer size should be an even number (since we expect wchar_ts).
    // If this is not the case, fail here.
    DLOG(ERROR) << "Corrupt PendingRename value.";
    return E_UNEXPECTED;
  }

  // There are pending file renames. Read them in.
  buffer.resize(buffer_size);
  result = RegQueryValueEx(session_manager_handle, kPendingFileRenameOps,
                           0, &type, reinterpret_cast<LPBYTE>(&buffer[0]),
                           &buffer_size);
  if (result != ERROR_SUCCESS) {
    DLOG(ERROR) << "Failed to read from " << kPendingFileRenameOps;
    return HRESULT_FROM_WIN32(result);
  }

  // We now have a buffer of bytes that is actually a sequence of
  // null-terminated wchar_t strings terminated by an additional null character.
  // Stick this into a vector of strings for clarity.
  HRESULT hr = MultiSZBytesToStringArray(&buffer[0], buffer.size(),
                                         pending_moves);
  return hr;
}

bool MatchPendingDeletePath(const std::wstring& short_form_needle,
                            const std::wstring& reg_path) {
  std::wstring match_path(reg_path);  // Stores the path stored in each entry.

  // First chomp the prefix since that will mess up GetShortPathName.
  std::wstring prefix(L"\\??\\");
  if (StartsWith(match_path, prefix, false))
    match_path = match_path.substr(4);

  // Get the short path name of the entry.
  std::wstring short_match_path(GetShortPathName(match_path.c_str()));

  // Now compare the paths. If it isn't one we're looking for, add it
  // to the list to keep.
  return StartsWith(short_match_path, short_form_needle, false);
}

// Removes all pending moves for the given |directory| and any contained
// files or subdirectories. Returns true on success
bool RemoveFromMovesPendingReboot(const wchar_t* directory) {
  DCHECK(directory);
  std::vector<PendingMove> pending_moves;
  HRESULT hr = GetPendingMovesValue(&pending_moves);
  if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
    // No pending moves, nothing to do.
    return true;
  }
  if (FAILED(hr)) {
    // Couldn't read the key or the key was corrupt.
    return false;
  }

  // Get the short form of |directory| and use that to match.
  std::wstring short_directory(GetShortPathName(directory));

  std::vector<PendingMove> strings_to_keep;
  for (std::vector<PendingMove>::const_iterator iter(pending_moves.begin());
       iter != pending_moves.end(); iter++) {
    if (!MatchPendingDeletePath(short_directory, iter->first)) {
      // This doesn't match the deletions we are looking for. Preserve
      // this string pair, making sure that it is in fact a pair.
      strings_to_keep.push_back(*iter);
    }
  }

  if (strings_to_keep.size() == pending_moves.size()) {
    // Nothing to remove, return true.
    return true;
  }

  // Write the key back into a buffer.
  base::win::RegKey session_manager_key(HKEY_LOCAL_MACHINE, kSessionManagerKey,
                                        KEY_CREATE_SUB_KEY | KEY_SET_VALUE);
  if (!session_manager_key.Handle()) {
    // Couldn't open / create the key.
    LOG(ERROR) << "Failed to open session manager key for writing.";
    return false;
  }

  if (strings_to_keep.size() <= 1) {
    // We have only the trailing NULL string. Don't bother writing that.
    return (session_manager_key.DeleteValue(kPendingFileRenameOps) ==
        ERROR_SUCCESS);
  }
  std::vector<char> buffer;
  StringArrayToMultiSZBytes(strings_to_keep, &buffer);
  DCHECK_GT(buffer.size(), 0U);
  if (buffer.empty())
    return false;
  return (session_manager_key.WriteValue(kPendingFileRenameOps, &buffer[0],
      buffer.size(), REG_MULTI_SZ) == ERROR_SUCCESS);
}
