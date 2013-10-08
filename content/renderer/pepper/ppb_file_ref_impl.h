// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_PEPPER_PPB_FILE_REF_IMPL_H_
#define CONTENT_RENDERER_PEPPER_PPB_FILE_REF_IMPL_H_

#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/files/file_path.h"
#include "base/id_map.h"
#include "base/memory/linked_ptr.h"
#include "base/platform_file.h"
#include "ipc/ipc_listener.h"
#include "ipc/ipc_platform_file.h"
#include "ppapi/c/pp_file_info.h"
#include "ppapi/c/ppb_file_ref.h"
#include "ppapi/shared_impl/ppb_file_ref_shared.h"
#include "ppapi/shared_impl/scoped_pp_resource.h"
#include "ppapi/shared_impl/var.h"
#include "url/gurl.h"

namespace ppapi {
namespace host {
class ResourceHost;
}
}

namespace content {

using ppapi::StringVar;

class PepperFileSystemHost;

class PPB_FileRef_Impl : public ppapi::PPB_FileRef_Shared,
                         public IPC::Listener {
 public:
  PPB_FileRef_Impl(const ppapi::PPB_FileRef_CreateInfo& info,
                   PP_Resource file_system);
  PPB_FileRef_Impl(const ppapi::PPB_FileRef_CreateInfo& info,
                   const base::FilePath& external_file_path);

  // The returned object will have a refcount of 0 (just like "new").
  static PPB_FileRef_Impl* CreateInternal(PP_Instance instance,
                                          PP_Resource pp_file_system,
                                          const std::string& path);

  // The returned object will have a refcount of 0 (just like "new").
  static PPB_FileRef_Impl* CreateExternal(
      PP_Instance instance,
      const base::FilePath& external_file_path,
      const std::string& display_name);

  // PPB_FileRef_API implementation (not provided by PPB_FileRef_Shared).
  virtual PP_Resource GetParent() OVERRIDE;
  virtual int32_t MakeDirectory(
      PP_Bool make_ancestors,
      scoped_refptr<ppapi::TrackedCallback> callback) OVERRIDE;
  virtual int32_t Touch(
      PP_Time last_access_time,
      PP_Time last_modified_time,
      scoped_refptr<ppapi::TrackedCallback> callback) OVERRIDE;
  virtual int32_t Delete(
      scoped_refptr<ppapi::TrackedCallback> callback) OVERRIDE;
  virtual int32_t Rename(
      PP_Resource new_file_ref,
      scoped_refptr<ppapi::TrackedCallback> callback) OVERRIDE;
  virtual int32_t Query(
      PP_FileInfo* info,
      scoped_refptr<ppapi::TrackedCallback> callback) OVERRIDE;
  virtual int32_t ReadDirectoryEntries(
      const PP_ArrayOutput& output,
      scoped_refptr<ppapi::TrackedCallback> callback) OVERRIDE;
  virtual int32_t QueryInHost(
      linked_ptr<PP_FileInfo> info,
      scoped_refptr<ppapi::TrackedCallback> callback) OVERRIDE;
  virtual int32_t ReadDirectoryEntriesInHost(
      linked_ptr<std::vector<ppapi::PPB_FileRef_CreateInfo> > files,
      linked_ptr<std::vector<PP_FileType> > file_types,
      scoped_refptr<ppapi::TrackedCallback> callback) OVERRIDE;
  virtual PP_Var GetAbsolutePath() OVERRIDE;

  PP_Resource file_system_resource() const { return file_system_; }

  // Returns the system path corresponding to this file. Valid only for
  // external filesystems.
  base::FilePath GetSystemPath() const;

  // Returns the FileSystem API URL corresponding to this file.
  GURL GetFileSystemURL() const;

  // Checks if file ref has file system instance and if the instance is opened.
  bool HasValidFileSystem() const;

  void AddFileSystemRefCount() {
    file_system_ref_ = file_system_;
  }

 private:
  virtual ~PPB_FileRef_Impl();

  // IPC::Listener implementation.
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

  void OnAsyncFileOpened(
      base::PlatformFileError error_code,
      IPC::PlatformFileForTransit file_for_transit,
      int message_id);

  // Many mutation functions are allow only to non-external filesystems, This
  // function returns true if the filesystem is opened and isn't external as an
  // access check for these functions.
  bool IsValidNonExternalFileSystem() const;

  PepperFileSystemHost* GetFileSystemHost() const;
  static PepperFileSystemHost* GetFileSystemHostInternal(
      PP_Instance instance, PP_Resource resource);

  // 0 for external filesystems.  This is a plugin side resource that we don't
  // hold a reference here, so file_system_ could be destroyed earlier than
  // this object.  Right now we checked the existance in plugin delegate before
  // use.  But it's better to hold a reference once we migrate FileRef to the
  // new design.
  PP_Resource file_system_;

  // Holds a reference of FileSystem when running in process.  See
  // PPB_FileRef_Proxy for corresponding code for out-of-process mode.  Note
  // that this ScopedPPResource is only expected to be used when running in
  // process (since PPB_FileRef_Proxy takes care of out-of-process case).
  // Also note that this workaround will be no longer needed after FileRef
  // refactoring.
  ppapi::ScopedPPResource file_system_ref_;

  // Used only for external filesystems.
  base::FilePath external_file_system_path_;

  // Lazily initialized var created from the external path. This is so we can
  // return the identical string object every time it is requested.
  scoped_refptr<StringVar> external_path_var_;

  int routing_id_;

  typedef base::Callback<void (base::PlatformFileError, base::PassPlatformFile)>
      AsyncOpenFileCallback;

  IDMap<AsyncOpenFileCallback> pending_async_open_files_;

  DISALLOW_COPY_AND_ASSIGN(PPB_FileRef_Impl);
};

}  // namespace content

#endif  // CONTENT_RENDERER_PEPPER_PPB_FILE_REF_IMPL_H_
