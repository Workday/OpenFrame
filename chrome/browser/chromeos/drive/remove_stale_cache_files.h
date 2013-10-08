// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_REMOVE_STALE_CACHE_FILES_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_REMOVE_STALE_CACHE_FILES_H_

namespace drive{
namespace internal {

class FileCache;
class ResourceMetadata;

// Removes files from |cache| which are not present in |resource_metadata|.
// Must be run on the same task runner as |cache| and |resource_metadata| use.
void RemoveStaleCacheFiles(FileCache* cache,
                           ResourceMetadata* resource_metadata);

}  // namespace internal
}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_REMOVE_STALE_CACHE_FILES_H_
