// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/ppb_file_ref_proxy.h"

#include <map>

#include "base/bind.h"
#include "ppapi/c/pp_directory_entry.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/c/ppb_file_ref.h"
#include "ppapi/c/private/ppb_file_ref_private.h"
#include "ppapi/c/private/ppb_proxy_private.h"
#include "ppapi/proxy/enter_proxy.h"
#include "ppapi/proxy/host_dispatcher.h"
#include "ppapi/proxy/plugin_dispatcher.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/serialized_var.h"
#include "ppapi/shared_impl/array_writer.h"
#include "ppapi/shared_impl/ppb_file_ref_shared.h"
#include "ppapi/shared_impl/scoped_pp_resource.h"
#include "ppapi/shared_impl/tracked_callback.h"
#include "ppapi/thunk/resource_creation_api.h"
#include "ppapi/thunk/thunk.h"

using ppapi::thunk::EnterResourceNoLock;
using ppapi::thunk::PPB_FileRef_API;
using ppapi::thunk::ResourceCreationAPI;

namespace ppapi {
namespace proxy {

namespace {

void ReleaseEntries(const std::vector<PP_DirectoryEntry>& entries) {
  ResourceTracker* tracker = PpapiGlobals::Get()->GetResourceTracker();
  for (std::vector<PP_DirectoryEntry>::const_iterator it = entries.begin();
       it != entries.end(); ++it)
    tracker->ReleaseResource(it->file_ref);
}

}  // namespace

class FileRef : public PPB_FileRef_Shared {
 public:
  explicit FileRef(const PPB_FileRef_CreateInfo& info);
  virtual ~FileRef();

  // Resource overrides.
  virtual void LastPluginRefWasDeleted() OVERRIDE;

  // PPB_FileRef_API implementation (not provided by PPB_FileRef_Shared).
  virtual PP_Resource GetParent() OVERRIDE;
  virtual int32_t MakeDirectory(
      PP_Bool make_ancestors,
      scoped_refptr<TrackedCallback> callback) OVERRIDE;
  virtual int32_t Touch(PP_Time last_access_time,
                        PP_Time last_modified_time,
                        scoped_refptr<TrackedCallback> callback) OVERRIDE;
  virtual int32_t Delete(scoped_refptr<TrackedCallback> callback) OVERRIDE;
  virtual int32_t Rename(PP_Resource new_file_ref,
                         scoped_refptr<TrackedCallback> callback) OVERRIDE;
  virtual int32_t Query(PP_FileInfo* info,
                        scoped_refptr<TrackedCallback> callback) OVERRIDE;
  virtual int32_t ReadDirectoryEntries(
      const PP_ArrayOutput& output,
      scoped_refptr<TrackedCallback> callback) OVERRIDE;
  virtual int32_t QueryInHost(
      linked_ptr<PP_FileInfo> info,
      scoped_refptr<TrackedCallback> callback) OVERRIDE;
  virtual int32_t ReadDirectoryEntriesInHost(
      linked_ptr<std::vector<ppapi::PPB_FileRef_CreateInfo> > files,
      linked_ptr<std::vector<PP_FileType> > file_types,
      scoped_refptr<TrackedCallback> callback) OVERRIDE;
  virtual PP_Var GetAbsolutePath() OVERRIDE;

  // Executes the pending callback with the given ID. See pending_callbacks_.
  void ExecuteCallback(uint32_t callback_id, int32_t result);
  int32_t SetFileInfo(uint32_t callback_id, const PP_FileInfo& info);
  int32_t SetReadDirectoryEntriesOutput(
      uint32_t callback_id,
      const std::vector<ppapi::PPB_FileRef_CreateInfo>& infos,
      const std::vector<PP_FileType>& file_types);

 private:
  PluginDispatcher* GetDispatcher() const {
    return PluginDispatcher::GetForResource(this);
  }

  // Adds a callback to the list and returns its ID.
  uint32_t SendCallback(scoped_refptr<TrackedCallback> callback);

  // This class can have any number of out-standing requests with completion
  // callbacks, in contrast to most resources which have one possible pending
  // callback pending (like a Flush callback).
  //
  // To keep track of them, assign integer IDs to the callbacks, which is how
  // the callback will be identified when it's passed to the host and then
  // back here. Use unsigned so that overflow is well-defined.
  uint32_t next_callback_id_;
  typedef std::map<uint32_t,
                   scoped_refptr<TrackedCallback> > PendingCallbackMap;
  PendingCallbackMap pending_callbacks_;

  // Used to keep pointers to PP_FileInfo instances that are written before
  // callbacks are invoked. The id of a pending file info will match that of
  // the corresponding callback.
  typedef std::map<uint32_t, PP_FileInfo*> PendingFileInfoMap;
  PendingFileInfoMap pending_file_infos_;

  // Used to keep PP_ArrayOutput instances that are written before callbacks
  // are invoked. The id of a pending array output will match that of the
  // corresponding callback.
  typedef std::map<uint32_t, PP_ArrayOutput>
      PendingReadDirectoryEntriesOutputMap;
  PendingReadDirectoryEntriesOutputMap pending_read_entries_outputs_;

  // Holds a reference on plugin side when running out of process, so that
  // FileSystem won't die before FileRef.  See PPB_FileRef_Impl for
  // corresponding code for in-process mode.  Note that this workaround will
  // be no longer needed after FileRef refactoring.
  ScopedPPResource file_system_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(FileRef);
};

FileRef::FileRef(const PPB_FileRef_CreateInfo& info)
    : PPB_FileRef_Shared(OBJECT_IS_PROXY, info),
      next_callback_id_(0u),
      file_system_(info.file_system_plugin_resource) {
}

FileRef::~FileRef() {
  // The callbacks map should have been cleared by LastPluginRefWasDeleted.
  DCHECK(pending_callbacks_.empty());
  DCHECK(pending_file_infos_.empty());
  DCHECK(pending_read_entries_outputs_.empty());
}

void FileRef::LastPluginRefWasDeleted() {
  // The callback tracker will abort our callbacks for us.
  pending_callbacks_.clear();
  pending_file_infos_.clear();
  pending_read_entries_outputs_.clear();
}

PP_Resource FileRef::GetParent() {
  PPB_FileRef_CreateInfo create_info;
  GetDispatcher()->Send(new PpapiHostMsg_PPBFileRef_GetParent(
      API_ID_PPB_FILE_REF, host_resource(), &create_info));
  return PPB_FileRef_Proxy::DeserializeFileRef(create_info);
}

int32_t FileRef::MakeDirectory(PP_Bool make_ancestors,
                               scoped_refptr<TrackedCallback> callback) {
  GetDispatcher()->Send(new PpapiHostMsg_PPBFileRef_MakeDirectory(
      API_ID_PPB_FILE_REF, host_resource(), make_ancestors,
      SendCallback(callback)));
  return PP_OK_COMPLETIONPENDING;
}

int32_t FileRef::Touch(PP_Time last_access_time,
                       PP_Time last_modified_time,
                       scoped_refptr<TrackedCallback> callback) {
  GetDispatcher()->Send(new PpapiHostMsg_PPBFileRef_Touch(
      API_ID_PPB_FILE_REF, host_resource(), last_access_time,
      last_modified_time, SendCallback(callback)));
  return PP_OK_COMPLETIONPENDING;
}

int32_t FileRef::Delete(scoped_refptr<TrackedCallback> callback) {
  GetDispatcher()->Send(new PpapiHostMsg_PPBFileRef_Delete(
      API_ID_PPB_FILE_REF, host_resource(), SendCallback(callback)));
  return PP_OK_COMPLETIONPENDING;
}

int32_t FileRef::Rename(PP_Resource new_file_ref,
                        scoped_refptr<TrackedCallback> callback) {
  Resource* new_file_ref_object =
      PpapiGlobals::Get()->GetResourceTracker()->GetResource(new_file_ref);
  if (!new_file_ref_object ||
      new_file_ref_object->host_resource().instance() != pp_instance())
    return PP_ERROR_BADRESOURCE;

  GetDispatcher()->Send(new PpapiHostMsg_PPBFileRef_Rename(
      API_ID_PPB_FILE_REF, host_resource(),
      new_file_ref_object->host_resource(), SendCallback(callback)));
  return PP_OK_COMPLETIONPENDING;
}

int32_t FileRef::Query(PP_FileInfo* info,
                       scoped_refptr<TrackedCallback> callback) {
  // Store the pending file info id.
  uint32_t id = SendCallback(callback);
  pending_file_infos_[id] = info;
  GetDispatcher()->Send(new PpapiHostMsg_PPBFileRef_Query(
      API_ID_PPB_FILE_REF, host_resource(), id));
  return PP_OK_COMPLETIONPENDING;
}

int32_t FileRef::ReadDirectoryEntries(
    const PP_ArrayOutput& output,
    scoped_refptr<TrackedCallback> callback) {
  // Store the pending read entries output id.
  uint32_t id = SendCallback(callback);
  pending_read_entries_outputs_[id] = output;
  GetDispatcher()->Send(new PpapiHostMsg_PPBFileRef_ReadDirectoryEntries(
      API_ID_PPB_FILE_REF, host_resource(), id));
  return PP_OK_COMPLETIONPENDING;
}

int32_t FileRef::QueryInHost(
    linked_ptr<PP_FileInfo> info,
    scoped_refptr<TrackedCallback> callback) {
  NOTREACHED();
  return PP_ERROR_FAILED;
}

int32_t FileRef::ReadDirectoryEntriesInHost(
    linked_ptr<std::vector<ppapi::PPB_FileRef_CreateInfo> > files,
    linked_ptr<std::vector<PP_FileType> > file_types,
    scoped_refptr<TrackedCallback> callback) {
  NOTREACHED();
  return PP_ERROR_FAILED;
}

PP_Var FileRef::GetAbsolutePath() {
  ReceiveSerializedVarReturnValue result;
  GetDispatcher()->Send(new PpapiHostMsg_PPBFileRef_GetAbsolutePath(
      API_ID_PPB_FILE_REF, host_resource(), &result));
  return result.Return(GetDispatcher());
}

void FileRef::ExecuteCallback(uint32_t callback_id, int32_t result) {
  PendingCallbackMap::iterator found = pending_callbacks_.find(callback_id);
  if (found == pending_callbacks_.end()) {
    // This will happen when the plugin deletes its resource with a pending
    // callback. The callback will be locally issued with an ABORTED call while
    // the operation may still be pending in the renderer.
    return;
  }

  // Executing the callback may mutate the callback list.
  scoped_refptr<TrackedCallback> callback = found->second;
  pending_callbacks_.erase(found);
  callback->Run(result);
}

int32_t FileRef::SetFileInfo(uint32_t callback_id, const PP_FileInfo& info) {
  PendingFileInfoMap::iterator found = pending_file_infos_.find(callback_id);
  if (found == pending_file_infos_.end())
    return PP_ERROR_FAILED;
  PP_FileInfo* target_info = found->second;
  *target_info = info;
  pending_file_infos_.erase(found);
  return PP_OK;
}

int32_t FileRef::SetReadDirectoryEntriesOutput(
    uint32_t callback_id,
    const std::vector<ppapi::PPB_FileRef_CreateInfo>& infos,
    const std::vector<PP_FileType>& file_types) {
  PendingReadDirectoryEntriesOutputMap::iterator found =
      pending_read_entries_outputs_.find(callback_id);
  if (found == pending_read_entries_outputs_.end())
    return PP_ERROR_FAILED;

  PP_ArrayOutput output = found->second;
  pending_read_entries_outputs_.erase(found);

  std::vector<PP_DirectoryEntry> entries;
  for (size_t i = 0; i < infos.size(); ++i) {
    PP_DirectoryEntry entry;
    entry.file_ref = PPB_FileRef_Proxy::DeserializeFileRef(infos[i]);
    entry.file_type = file_types[i];
    entries.push_back(entry);
  }

  ArrayWriter writer(output);
  if (!writer.is_valid()) {
    ReleaseEntries(entries);
    return PP_ERROR_BADARGUMENT;
  }

  writer.StoreVector(entries);
  return PP_OK;
}

uint32_t FileRef::SendCallback(scoped_refptr<TrackedCallback> callback) {
  // In extreme cases the IDs may wrap around, so avoid duplicates.
  while (pending_callbacks_.count(next_callback_id_))
    ++next_callback_id_;

  pending_callbacks_[next_callback_id_] = callback;
  return next_callback_id_++;
}

PPB_FileRef_Proxy::PPB_FileRef_Proxy(Dispatcher* dispatcher)
    : InterfaceProxy(dispatcher),
      callback_factory_(this) {
}

PPB_FileRef_Proxy::~PPB_FileRef_Proxy() {
}

// static
PP_Resource PPB_FileRef_Proxy::CreateProxyResource(PP_Instance instance,
                                                   PP_Resource file_system,
                                                   const char* path) {
  PPB_FileRef_CreateInfo create_info;
  PluginDispatcher::GetForInstance(instance)->Send(
      new PpapiHostMsg_PPBFileRef_Create(
          API_ID_PPB_FILE_REF, instance, file_system, path, &create_info));
  return PPB_FileRef_Proxy::DeserializeFileRef(create_info);
}

bool PPB_FileRef_Proxy::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PPB_FileRef_Proxy, msg)
#if !defined(OS_NACL)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBFileRef_Create, OnMsgCreate)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBFileRef_GetParent, OnMsgGetParent)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBFileRef_MakeDirectory,
                        OnMsgMakeDirectory)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBFileRef_Touch, OnMsgTouch)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBFileRef_Delete, OnMsgDelete)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBFileRef_Rename, OnMsgRename)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBFileRef_Query, OnMsgQuery)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBFileRef_ReadDirectoryEntries,
                        OnMsgReadDirectoryEntries)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBFileRef_GetAbsolutePath,
                        OnMsgGetAbsolutePath)
#endif  // !defined(OS_NACL)

    IPC_MESSAGE_HANDLER(PpapiMsg_PPBFileRef_CallbackComplete,
                        OnMsgCallbackComplete)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBFileRef_QueryCallbackComplete,
                        OnMsgQueryCallbackComplete)
    IPC_MESSAGE_HANDLER(
        PpapiMsg_PPBFileRef_ReadDirectoryEntriesCallbackComplete,
        OnMsgReadDirectoryEntriesCallbackComplete)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

// static
void PPB_FileRef_Proxy::SerializeFileRef(PP_Resource file_ref,
                                         PPB_FileRef_CreateInfo* result) {
  EnterResourceNoLock<PPB_FileRef_API> enter(file_ref, false);
  if (enter.succeeded())
    *result = enter.object()->GetCreateInfo();
}

// static
PP_Resource PPB_FileRef_Proxy::DeserializeFileRef(
    const PPB_FileRef_CreateInfo& serialized) {
  if (serialized.resource.is_null())
    return 0;  // Resource invalid.
  return (new FileRef(serialized))->GetReference();
}

#if !defined(OS_NACL)
void PPB_FileRef_Proxy::OnMsgCreate(PP_Instance pp_instance,
                                    PP_Resource file_system,
                                    const std::string& path,
                                    PPB_FileRef_CreateInfo* result) {
  thunk::EnterResourceCreation enter(pp_instance);
  if (enter.failed())
    return;

  PP_Resource resource = enter.functions()->CreateFileRef(
      pp_instance, file_system, path.c_str());
  if (!resource)
    return;  // CreateInfo default constructor initializes to 0.
  SerializeFileRef(resource, result);
}

void PPB_FileRef_Proxy::OnMsgGetParent(const HostResource& host_resource,
                                       PPB_FileRef_CreateInfo* result) {
  EnterHostFromHostResource<PPB_FileRef_API> enter(host_resource);
  if (enter.succeeded())
    SerializeFileRef(enter.object()->GetParent(), result);
}

void PPB_FileRef_Proxy::OnMsgMakeDirectory(const HostResource& host_resource,
                                           PP_Bool make_ancestors,
                                           uint32_t callback_id) {
  EnterHostFromHostResourceForceCallback<PPB_FileRef_API> enter(
      host_resource, callback_factory_,
      &PPB_FileRef_Proxy::OnCallbackCompleteInHost, host_resource, callback_id);
  if (enter.succeeded()) {
    enter.SetResult(enter.object()->MakeDirectory(make_ancestors,
                                                  enter.callback()));
  }
}

void PPB_FileRef_Proxy::OnMsgTouch(const HostResource& host_resource,
                                   PP_Time last_access,
                                   PP_Time last_modified,
                                   uint32_t callback_id) {
  EnterHostFromHostResourceForceCallback<PPB_FileRef_API> enter(
      host_resource, callback_factory_,
      &PPB_FileRef_Proxy::OnCallbackCompleteInHost, host_resource, callback_id);
  if (enter.succeeded()) {
    enter.SetResult(enter.object()->Touch(last_access, last_modified,
                                          enter.callback()));
  }
}

void PPB_FileRef_Proxy::OnMsgDelete(const HostResource& host_resource,
                                    uint32_t callback_id) {
  EnterHostFromHostResourceForceCallback<PPB_FileRef_API> enter(
      host_resource, callback_factory_,
      &PPB_FileRef_Proxy::OnCallbackCompleteInHost, host_resource, callback_id);
  if (enter.succeeded())
    enter.SetResult(enter.object()->Delete(enter.callback()));
}

void PPB_FileRef_Proxy::OnMsgRename(const HostResource& file_ref,
                                    const HostResource& new_file_ref,
                                    uint32_t callback_id) {
  EnterHostFromHostResourceForceCallback<PPB_FileRef_API> enter(
      file_ref, callback_factory_,
      &PPB_FileRef_Proxy::OnCallbackCompleteInHost, file_ref, callback_id);
  if (enter.succeeded()) {
    enter.SetResult(enter.object()->Rename(new_file_ref.host_resource(),
                                           enter.callback()));
  }
}

void PPB_FileRef_Proxy::OnMsgQuery(const HostResource& file_ref,
                                   uint32_t callback_id) {
  linked_ptr<PP_FileInfo> info(new PP_FileInfo());
  EnterHostFromHostResourceForceCallback<PPB_FileRef_API> enter(
      file_ref, callback_factory_,
      &PPB_FileRef_Proxy::OnQueryCallbackCompleteInHost, file_ref,
      info, callback_id);
  if (enter.succeeded())
    enter.SetResult(enter.object()->QueryInHost(info, enter.callback()));
}

void PPB_FileRef_Proxy::OnMsgGetAbsolutePath(const HostResource& host_resource,
                                             SerializedVarReturnValue result) {
  EnterHostFromHostResource<PPB_FileRef_API> enter(host_resource);
  if (enter.succeeded())
    result.Return(dispatcher(), enter.object()->GetAbsolutePath());
}

void PPB_FileRef_Proxy::OnMsgReadDirectoryEntries(const HostResource& file_ref,
                                         uint32_t callback_id) {
  linked_ptr<std::vector<ppapi::PPB_FileRef_CreateInfo> > files(
      new std::vector<ppapi::PPB_FileRef_CreateInfo>());
  linked_ptr<std::vector<PP_FileType> > file_types(
      new std::vector<PP_FileType>());
  HostCallbackParams params(file_ref, callback_id);
  EnterHostFromHostResourceForceCallback<PPB_FileRef_API> enter(
      file_ref, callback_factory_,
      &PPB_FileRef_Proxy::OnReadDirectoryEntriesCallbackCompleteInHost,
      params, files, file_types);
  if (enter.succeeded()) {
    enter.SetResult(enter.object()->ReadDirectoryEntriesInHost(
        files, file_types, enter.callback()));
  }
}

#endif  // !defined(OS_NACL)

void PPB_FileRef_Proxy::OnMsgCallbackComplete(
    const HostResource& host_resource,
    uint32_t callback_id,
    int32_t result) {
  // Forward the callback info to the plugin resource.
  EnterPluginFromHostResource<PPB_FileRef_API> enter(host_resource);
  if (enter.succeeded())
    static_cast<FileRef*>(enter.object())->ExecuteCallback(callback_id, result);
}

void PPB_FileRef_Proxy::OnMsgQueryCallbackComplete(
    const HostResource& host_resource,
    const PP_FileInfo& info,
    uint32_t callback_id,
    int32_t result) {
  EnterPluginFromHostResource<PPB_FileRef_API> enter(host_resource);
  if (!enter.succeeded())
    return;

  if (result == PP_OK) {
    result = static_cast<FileRef*>(enter.object())->SetFileInfo(
        callback_id, info);
  }
  static_cast<FileRef*>(enter.object())->ExecuteCallback(callback_id, result);
}

void PPB_FileRef_Proxy::OnMsgReadDirectoryEntriesCallbackComplete(
    const HostResource& host_resource,
    const std::vector<ppapi::PPB_FileRef_CreateInfo>& infos,
    const std::vector<PP_FileType>& file_types,
    uint32_t callback_id,
    int32_t result) {
  CHECK_EQ(infos.size(), file_types.size());

  EnterPluginFromHostResource<PPB_FileRef_API> enter(host_resource);
  if (!enter.succeeded())
    return;

  if (result == PP_OK) {
    result =
        static_cast<FileRef*>(enter.object())->SetReadDirectoryEntriesOutput(
            callback_id, infos, file_types);
  }
  static_cast<FileRef*>(enter.object())->ExecuteCallback(
      callback_id, result);
}

#if !defined(OS_NACL)
void PPB_FileRef_Proxy::OnCallbackCompleteInHost(
    int32_t result,
    const HostResource& host_resource,
    uint32_t callback_id) {
  // Execute OnMsgCallbackComplete in the plugin process.
  Send(new PpapiMsg_PPBFileRef_CallbackComplete(
      API_ID_PPB_FILE_REF, host_resource, callback_id, result));
}

void PPB_FileRef_Proxy::OnQueryCallbackCompleteInHost(
    int32_t result,
    const HostResource& host_resource,
    linked_ptr<PP_FileInfo> info,
    uint32_t callback_id) {
  Send(new PpapiMsg_PPBFileRef_QueryCallbackComplete(
      API_ID_PPB_FILE_REF, host_resource, *info, callback_id, result));
}

void PPB_FileRef_Proxy::OnReadDirectoryEntriesCallbackCompleteInHost(
    int32_t result,
    HostCallbackParams params,
    linked_ptr<std::vector<ppapi::PPB_FileRef_CreateInfo> > files,
    linked_ptr<std::vector<PP_FileType> > file_types) {
  Send(new PpapiMsg_PPBFileRef_ReadDirectoryEntriesCallbackComplete(
      API_ID_PPB_FILE_REF, params.host_resource,
      *files, *file_types, params.callback_id, result));
}

#endif  // !defined(OS_NACL)

}  // namespace proxy
}  // namespace ppapi
