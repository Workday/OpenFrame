// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DEVTOOLS_DEVTOOLS_NETLOG_OBSERVER_H_
#define CONTENT_BROWSER_DEVTOOLS_DEVTOOLS_NETLOG_OBSERVER_H_

#include "base/containers/hash_tables.h"
#include "base/memory/ref_counted.h"
#include "net/base/net_log.h"
#include "webkit/common/resource_devtools_info.h"

namespace net {
class URLRequest;
}  // namespace net

namespace content {
struct ResourceResponse;

// DevToolsNetLogObserver watches the NetLog event stream and collects the
// stuff that may be of interest to DevTools. Currently, this only includes
// actual HTTP/SPDY headers sent and received over the network.
//
// As DevToolsNetLogObserver shares live data with objects that live on the
// IO Thread, it must also reside on the IO Thread.  Only OnAddEntry can be
// called from other threads.
class DevToolsNetLogObserver : public net::NetLog::ThreadSafeObserver {
  typedef webkit_glue::ResourceDevToolsInfo ResourceInfo;

 public:
  // net::NetLog::ThreadSafeObserver implementation:
  virtual void OnAddEntry(const net::NetLog::Entry& entry) OVERRIDE;

  void OnAddURLRequestEntry(const net::NetLog::Entry& entry);
  void OnAddHTTPStreamJobEntry(const net::NetLog::Entry& entry);
  void OnAddSocketEntry(const net::NetLog::Entry& entry);

  static void Attach();
  static void Detach();

  // Must be called on the IO thread. May return NULL if no observers
  // are active.
  static DevToolsNetLogObserver* GetInstance();
  static void PopulateResponseInfo(net::URLRequest*,
                                   ResourceResponse*);
  static int GetAndResetEncodedDataLength(net::URLRequest* request);

 private:
  static DevToolsNetLogObserver* instance_;

  DevToolsNetLogObserver();
  virtual ~DevToolsNetLogObserver();

  ResourceInfo* GetResourceInfo(uint32 id);

  typedef base::hash_map<uint32, scoped_refptr<ResourceInfo> > RequestToInfoMap;
  typedef base::hash_map<uint32, int> RequestToEncodedDataLengthMap;
  typedef base::hash_map<uint32, uint32> HTTPStreamJobToSocketMap;
  typedef base::hash_map<uint32, uint32> SocketToRequestMap;
  RequestToInfoMap request_to_info_;
  RequestToEncodedDataLengthMap request_to_encoded_data_length_;
  HTTPStreamJobToSocketMap http_stream_job_to_socket_;
  SocketToRequestMap socket_to_request_;

  DISALLOW_COPY_AND_ASSIGN(DevToolsNetLogObserver);
};

}  // namespace content

#endif  // CONTENT_BROWSER_DEVTOOLS_DEVTOOLS_NETLOG_OBSERVER_H_
