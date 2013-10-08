// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_STREAMS_STREAM_HANDLE_IMPL_H_
#define CONTENT_BROWSER_STREAMS_STREAM_HANDLE_IMPL_H_

#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/synchronization/lock.h"
#include "content/public/browser/stream_handle.h"

namespace content {

class Stream;

class StreamHandleImpl : public StreamHandle {
 public:
  StreamHandleImpl(const base::WeakPtr<Stream>& stream,
                   const GURL& original_url,
                   const std::string& mime_type);
  virtual ~StreamHandleImpl();

 private:
  // StreamHandle overrides
  virtual const GURL& GetURL() OVERRIDE;
  virtual const GURL& GetOriginalURL() OVERRIDE;
  virtual const std::string& GetMimeType() OVERRIDE;

  base::WeakPtr<Stream> stream_;
  GURL url_;
  GURL original_url_;
  std::string mime_type_;
  base::MessageLoopProxy* stream_message_loop_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_STREAMS_STREAM_HANDLE_IMPL_H_

