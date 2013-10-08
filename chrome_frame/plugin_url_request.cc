// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome_frame/plugin_url_request.h"

#include "chrome/common/automation_messages.h"

PluginUrlRequest::PluginUrlRequest()
    : delegate_(NULL),
      remote_request_id_(-1),
      post_data_len_(0),
      enable_frame_busting_(false),
      resource_type_(ResourceType::MAIN_FRAME),
      load_flags_(0),
      is_chunked_upload_(false) {
}

PluginUrlRequest::~PluginUrlRequest() {
}

bool PluginUrlRequest::Initialize(PluginUrlRequestDelegate* delegate,
    int remote_request_id, const std::string& url, const std::string& method,
    const std::string& referrer, const std::string& extra_headers,
    net::UploadData* upload_data, ResourceType::Type resource_type,
    bool enable_frame_busting, int load_flags) {
  delegate_ = delegate;
  remote_request_id_ = remote_request_id;
  url_ = url;
  method_ = method;
  referrer_ = referrer;
  extra_headers_ = extra_headers;
  resource_type_ = resource_type;
  load_flags_ = load_flags;

  if (upload_data) {
    // We store a pointer to UrlmonUploadDataStream and not net::UploadData
    // since UrlmonUploadDataStream implements thread safe ref counting and
    // UploadData does not.
    CComObject<UrlmonUploadDataStream>* upload_stream = NULL;
    HRESULT hr = CComObject<UrlmonUploadDataStream>::CreateInstance(
        &upload_stream);
    if (FAILED(hr)) {
      NOTREACHED();
    } else {
      upload_stream->AddRef();
      upload_stream->Initialize(upload_data);
      upload_data_.Attach(upload_stream);
      is_chunked_upload_ = upload_data->is_chunked();
      STATSTG stat;
      upload_stream->Stat(&stat, STATFLAG_NONAME);
      post_data_len_ = stat.cbSize.QuadPart;
    }
  }

  enable_frame_busting_ = enable_frame_busting;

  return true;
}
