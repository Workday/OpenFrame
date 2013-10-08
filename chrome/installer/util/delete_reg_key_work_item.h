// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_INSTALLER_UTIL_DELETE_REG_KEY_WORK_ITEM_H_
#define CHROME_INSTALLER_UTIL_DELETE_REG_KEY_WORK_ITEM_H_

#include <windows.h>

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/installer/util/registry_key_backup.h"
#include "chrome/installer/util/work_item.h"

class RegistryKeyBackup;

// A WorkItem subclass that deletes a registry key at the given path.  Be aware
// that in the event of rollback the key's values and subkeys are restored but
// the key and its subkeys take on their default security descriptors.
class DeleteRegKeyWorkItem : public WorkItem {
 public:
  virtual ~DeleteRegKeyWorkItem();

  virtual bool Do() OVERRIDE;

  virtual void Rollback() OVERRIDE;

 private:
  friend class WorkItem;

  DeleteRegKeyWorkItem(HKEY predefined_root, const std::wstring& path);

  // Root key from which we delete the key. The root key can only be
  // one of the predefined keys on Windows.
  HKEY predefined_root_;

  // Path of the key to be deleted.
  std::wstring path_;

  // Backup of the deleted key.
  RegistryKeyBackup backup_;

  DISALLOW_COPY_AND_ASSIGN(DeleteRegKeyWorkItem);
};

#endif  // CHROME_INSTALLER_UTIL_DELETE_REG_KEY_WORK_ITEM_H_
