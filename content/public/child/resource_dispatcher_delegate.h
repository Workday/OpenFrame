// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_CHILD_RESOURCE_DISPATCHER_DELEGATE_H_
#define CONTENT_PUBLIC_CHILD_RESOURCE_DISPATCHER_DELEGATE_H_

#include "content/common/content_export.h"
#include "webkit/child/resource_loader_bridge.h"

namespace content {

// Interface that allows observing request events and optionally replacing the
// peer.
class CONTENT_EXPORT ResourceDispatcherDelegate {
 public:
  virtual ~ResourceDispatcherDelegate() {}

  virtual webkit_glue::ResourceLoaderBridge::Peer* OnRequestComplete(
      webkit_glue::ResourceLoaderBridge::Peer* current_peer,
      ResourceType::Type resource_type,
      int error_code) = 0;

  virtual webkit_glue::ResourceLoaderBridge::Peer* OnReceivedResponse(
      webkit_glue::ResourceLoaderBridge::Peer* current_peer,
      const std::string& mime_type,
      const GURL& url) = 0;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_CHILD_RESOURCE_DISPATCHER_DELEGATE_H_
