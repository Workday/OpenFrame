// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_MTP_DEVICE_MAP_SERVICE_H_
#define CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_MTP_DEVICE_MAP_SERVICE_H_

#include <map>

#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/threading/thread_checker.h"

namespace chrome {

class MTPDeviceAsyncDelegate;

// This class provides media transfer protocol (MTP) device delegate to
// complete media file system operations. ScopedMTPDeviceMapEntry class
// manages the device map entries.
class MTPDeviceMapService {
 public:
  static MTPDeviceMapService* GetInstance();

  /////////////////////////////////////////////////////////////////////////////
  //   Following methods are used to manage MTPDeviceAsyncDelegate objects.  //
  /////////////////////////////////////////////////////////////////////////////
  // Adds the MTP device delegate to the map service. |device_location|
  // specifies the mount location of the MTP device.
  // Called on the IO thread.
  void AddAsyncDelegate(const base::FilePath::StringType& device_location,
                        MTPDeviceAsyncDelegate* delegate);

  // Removes the MTP device delegate from the map service. |device_location|
  // specifies the mount location of the MTP device.
  // Called on the IO thread.
  void RemoveAsyncDelegate(const base::FilePath::StringType& device_location);

  // Gets the media device delegate associated with |filesystem_id|.
  // Return NULL if the |filesystem_id| is no longer valid (e.g. because the
  // corresponding device is detached, etc).
  // Called on the IO thread.
  MTPDeviceAsyncDelegate* GetMTPDeviceAsyncDelegate(
      const std::string& filesystem_id);

 private:
  friend struct base::DefaultLazyInstanceTraits<MTPDeviceMapService>;

  // Mapping of device_location and MTPDeviceAsyncDelegate* object. It is safe
  // to store and access the raw pointer. This class operates on the IO thread.
  typedef std::map<base::FilePath::StringType, MTPDeviceAsyncDelegate*>
      AsyncDelegateMap;

  // Get access to this class using GetInstance() method.
  MTPDeviceMapService();
  ~MTPDeviceMapService();

  /////////////////////////////////////////////////////////////////////////////
  // Following member variables are used to manage asynchronous              //
  // MTP device delegate objects.                                            //
  /////////////////////////////////////////////////////////////////////////////
  // Map of attached mtp device async delegates.
  AsyncDelegateMap async_delegate_map_;
  base::ThreadChecker thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(MTPDeviceMapService);
};

}  // namespace chrome

#endif  // CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_MTP_DEVICE_MAP_SERVICE_H_
