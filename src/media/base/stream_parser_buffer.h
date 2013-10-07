// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_STREAM_PARSER_BUFFER_H_
#define MEDIA_BASE_STREAM_PARSER_BUFFER_H_

#include "media/base/decoder_buffer.h"
#include "media/base/media_export.h"

namespace media {

class MEDIA_EXPORT StreamParserBuffer : public DecoderBuffer {
 public:
  // Value used to signal an invalid decoder config ID.
  enum { kInvalidConfigId = -1 };

  static scoped_refptr<StreamParserBuffer> CreateEOSBuffer();
  static scoped_refptr<StreamParserBuffer> CopyFrom(
      const uint8* data, int data_size, bool is_keyframe);
  static scoped_refptr<StreamParserBuffer> CopyFrom(
      const uint8* data, int data_size,
      const uint8* side_data, int side_data_size, bool is_keyframe);
  bool IsKeyframe() const { return is_keyframe_; }

  // Decode timestamp. If not explicitly set, or set to kNoTimestamp(), the
  // value will be taken from the normal timestamp.
  base::TimeDelta GetDecodeTimestamp() const;
  void SetDecodeTimestamp(const base::TimeDelta& timestamp);

  // Gets/sets the ID of the decoder config associated with this
  // buffer.
  int GetConfigId() const;
  void SetConfigId(int config_id);

 private:
  StreamParserBuffer(const uint8* data, int data_size,
                     const uint8* side_data, int side_data_size,
                     bool is_keyframe);
  virtual ~StreamParserBuffer();

  bool is_keyframe_;
  base::TimeDelta decode_timestamp_;
  int config_id_;
  DISALLOW_COPY_AND_ASSIGN(StreamParserBuffer);
};

}  // namespace media

#endif  // MEDIA_BASE_STREAM_PARSER_BUFFER_H_
