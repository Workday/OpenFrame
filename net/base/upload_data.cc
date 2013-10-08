// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/upload_data.h"

#include "base/logging.h"

namespace net {

UploadData::UploadData()
    : identifier_(0),
      is_chunked_(false),
      last_chunk_appended_(false) {
}

void UploadData::AppendBytes(const char* bytes, int bytes_len) {
  DCHECK(!is_chunked_);
  if (bytes_len > 0) {
    elements_.push_back(new UploadElement());
    elements_.back()->SetToBytes(bytes, bytes_len);
  }
}

void UploadData::AppendFileRange(const base::FilePath& file_path,
                                 uint64 offset, uint64 length,
                                 const base::Time& expected_modification_time) {
  DCHECK(!is_chunked_);
  elements_.push_back(new UploadElement());
  elements_.back()->SetToFilePathRange(file_path, offset, length,
                                       expected_modification_time);
}

UploadData::~UploadData() {
}

}  // namespace net
