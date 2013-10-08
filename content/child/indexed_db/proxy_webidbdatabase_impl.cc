// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/child/indexed_db/proxy_webidbdatabase_impl.h"

#include <vector>

#include "content/child/thread_safe_sender.h"
#include "content/child/indexed_db/indexed_db_dispatcher.h"
#include "content/child/indexed_db/indexed_db_key_builders.h"
#include "content/common/indexed_db/indexed_db_messages.h"
#include "third_party/WebKit/public/platform/WebIDBKeyPath.h"
#include "third_party/WebKit/public/platform/WebIDBMetadata.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/platform/WebVector.h"
#include "webkit/child/worker_task_runner.h"

using WebKit::WebIDBCallbacks;
using WebKit::WebIDBDatabaseCallbacks;
using WebKit::WebIDBMetadata;
using WebKit::WebIDBKey;
using WebKit::WebIDBKeyPath;
using WebKit::WebIDBKeyRange;
using WebKit::WebString;
using WebKit::WebVector;
using webkit_glue::WorkerTaskRunner;

namespace content {

RendererWebIDBDatabaseImpl::RendererWebIDBDatabaseImpl(
    int32 ipc_database_id,
    int32 ipc_database_callbacks_id,
    ThreadSafeSender* thread_safe_sender)
    : ipc_database_id_(ipc_database_id),
      ipc_database_callbacks_id_(ipc_database_callbacks_id),
      thread_safe_sender_(thread_safe_sender) {}

RendererWebIDBDatabaseImpl::~RendererWebIDBDatabaseImpl() {
  // It's not possible for there to be pending callbacks that address this
  // object since inside WebKit, they hold a reference to the object which owns
  // this object. But, if that ever changed, then we'd need to invalidate
  // any such pointers.
  thread_safe_sender_->Send(new IndexedDBHostMsg_DatabaseDestroyed(
      ipc_database_id_));
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance(thread_safe_sender_.get());
  dispatcher->DatabaseDestroyed(ipc_database_id_);
}

void RendererWebIDBDatabaseImpl::createObjectStore(
    long long transaction_id,
    long long object_store_id,
    const WebString& name,
    const WebIDBKeyPath& key_path,
    bool auto_increment) {
  IndexedDBHostMsg_DatabaseCreateObjectStore_Params params;
  params.ipc_database_id = ipc_database_id_;
  params.transaction_id = transaction_id;
  params.object_store_id = object_store_id;
  params.name = name;
  params.key_path = IndexedDBKeyPathBuilder::Build(key_path);
  params.auto_increment = auto_increment;

  thread_safe_sender_->Send(
      new IndexedDBHostMsg_DatabaseCreateObjectStore(params));
}

void RendererWebIDBDatabaseImpl::deleteObjectStore(
    long long transaction_id,
    long long object_store_id) {
  thread_safe_sender_->Send(
      new IndexedDBHostMsg_DatabaseDeleteObjectStore(
          ipc_database_id_,
          transaction_id,
          object_store_id));
}

void RendererWebIDBDatabaseImpl::createTransaction(
    long long transaction_id,
    WebIDBDatabaseCallbacks* callbacks,
    const WebVector<long long>& object_store_ids,
    unsigned short mode) {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance(thread_safe_sender_.get());
  dispatcher->RequestIDBDatabaseCreateTransaction(
      ipc_database_id_, transaction_id, callbacks, object_store_ids, mode);
}

void RendererWebIDBDatabaseImpl::close() {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance(thread_safe_sender_.get());
  dispatcher->RequestIDBDatabaseClose(ipc_database_id_,
                                      ipc_database_callbacks_id_);
}

void RendererWebIDBDatabaseImpl::get(
    long long transaction_id,
    long long object_store_id,
    long long index_id,
    const WebIDBKeyRange& key_range,
    bool key_only,
    WebIDBCallbacks* callbacks) {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance(thread_safe_sender_.get());
  dispatcher->RequestIDBDatabaseGet(
      ipc_database_id_,
      transaction_id,
      object_store_id,
      index_id,
      IndexedDBKeyRangeBuilder::Build(key_range),
      key_only,
      callbacks);
}

void RendererWebIDBDatabaseImpl::put(
    long long transaction_id,
    long long object_store_id,
    const WebKit::WebData& value,
    const WebIDBKey& key,
    PutMode put_mode,
    WebIDBCallbacks* callbacks,
    const WebVector<long long>& web_index_ids,
    const WebVector<WebIndexKeys>& web_index_keys) {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance(thread_safe_sender_.get());
  dispatcher->RequestIDBDatabasePut(ipc_database_id_,
                                    transaction_id,
                                    object_store_id,
                                    value,
                                    IndexedDBKeyBuilder::Build(key),
                                    put_mode,
                                    callbacks,
                                    web_index_ids,
                                    web_index_keys);
}

void RendererWebIDBDatabaseImpl::setIndexKeys(
    long long transaction_id,
    long long object_store_id,
    const WebIDBKey& primary_key,
    const WebVector<long long>& index_ids,
    const WebVector<WebIndexKeys>& index_keys) {
  IndexedDBHostMsg_DatabaseSetIndexKeys_Params params;
  params.ipc_database_id = ipc_database_id_;
  params.transaction_id = transaction_id;
  params.object_store_id = object_store_id;
  params.primary_key = IndexedDBKeyBuilder::Build(primary_key);
  COMPILE_ASSERT(sizeof(params.index_ids[0]) ==
                 sizeof(index_ids[0]), Cant_copy);
  params.index_ids.assign(index_ids.data(),
                          index_ids.data() + index_ids.size());

  params.index_keys.resize(index_keys.size());
  for (size_t i = 0; i < index_keys.size(); ++i) {
    params.index_keys[i].resize(index_keys[i].size());
    for (size_t j = 0; j < index_keys[i].size(); ++j) {
      params.index_keys[i][j] = IndexedDBKeyBuilder::Build(index_keys[i][j]);
    }
  }
  thread_safe_sender_->Send(new IndexedDBHostMsg_DatabaseSetIndexKeys(params));
}

void RendererWebIDBDatabaseImpl::setIndexesReady(
    long long transaction_id,
    long long object_store_id,
    const WebVector<long long>& web_index_ids) {
  std::vector<int64> index_ids(web_index_ids.data(),
                               web_index_ids.data() + web_index_ids.size());
  thread_safe_sender_->Send(new IndexedDBHostMsg_DatabaseSetIndexesReady(
      ipc_database_id_, transaction_id, object_store_id, index_ids));
}

void RendererWebIDBDatabaseImpl::openCursor(
    long long transaction_id,
    long long object_store_id,
    long long index_id,
    const WebIDBKeyRange& key_range,
    unsigned short direction,
    bool key_only,
    TaskType task_type,
    WebIDBCallbacks* callbacks) {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance(thread_safe_sender_.get());
  dispatcher->RequestIDBDatabaseOpenCursor(
      ipc_database_id_,
      transaction_id,
      object_store_id,
      index_id,
      IndexedDBKeyRangeBuilder::Build(key_range),
      direction,
      key_only,
      task_type,
      callbacks);
}

void RendererWebIDBDatabaseImpl::count(
    long long transaction_id,
    long long object_store_id,
    long long index_id,
    const WebIDBKeyRange& key_range,
    WebIDBCallbacks* callbacks) {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance(thread_safe_sender_.get());
  dispatcher->RequestIDBDatabaseCount(
      ipc_database_id_,
      transaction_id,
      object_store_id,
      index_id,
      IndexedDBKeyRangeBuilder::Build(key_range),
      callbacks);
}

void RendererWebIDBDatabaseImpl::deleteRange(
    long long transaction_id,
    long long object_store_id,
    const WebIDBKeyRange& key_range,
    WebIDBCallbacks* callbacks) {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance(thread_safe_sender_.get());
  dispatcher->RequestIDBDatabaseDeleteRange(
      ipc_database_id_,
      transaction_id,
      object_store_id,
      IndexedDBKeyRangeBuilder::Build(key_range),
      callbacks);
}

void RendererWebIDBDatabaseImpl::clear(
    long long transaction_id,
    long long object_store_id,
    WebIDBCallbacks* callbacks) {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance(thread_safe_sender_.get());
  dispatcher->RequestIDBDatabaseClear(
      ipc_database_id_, transaction_id, object_store_id, callbacks);
}

void RendererWebIDBDatabaseImpl::createIndex(
    long long transaction_id,
    long long object_store_id,
    long long index_id,
    const WebString& name,
    const WebIDBKeyPath& key_path,
    bool unique,
    bool multi_entry)
{
  IndexedDBHostMsg_DatabaseCreateIndex_Params params;
  params.ipc_database_id = ipc_database_id_;
  params.transaction_id = transaction_id;
  params.object_store_id = object_store_id;
  params.index_id = index_id;
  params.name = name;
  params.key_path = IndexedDBKeyPathBuilder::Build(key_path);
  params.unique = unique;
  params.multi_entry = multi_entry;

  thread_safe_sender_->Send(new IndexedDBHostMsg_DatabaseCreateIndex(params));
}

void RendererWebIDBDatabaseImpl::deleteIndex(
    long long transaction_id,
    long long object_store_id,
    long long index_id)
{
  thread_safe_sender_->Send(
      new IndexedDBHostMsg_DatabaseDeleteIndex(
          ipc_database_id_,
          transaction_id,
          object_store_id, index_id));
}

void RendererWebIDBDatabaseImpl::abort(long long transaction_id) {
  thread_safe_sender_->Send(new IndexedDBHostMsg_DatabaseAbort(
      ipc_database_id_, transaction_id));
}

void RendererWebIDBDatabaseImpl::commit(long long transaction_id) {
  thread_safe_sender_->Send(new IndexedDBHostMsg_DatabaseCommit(
      ipc_database_id_, transaction_id));
}

}  // namespace content
