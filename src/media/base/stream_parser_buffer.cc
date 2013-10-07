// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/stream_parser_buffer.h"

#include "base/logging.h"
#include "media/base/buffers.h"

namespace media {

scoped_refptr<StreamParserBuffer> StreamParserBuffer::CreateEOSBuffer() {
  return make_scoped_refptr(new StreamParserBuffer(NULL, 0, NULL, 0, false));
}

scoped_refptr<StreamParserBuffer> StreamParserBuffer::CopyFrom(
    const uint8* data, int data_size, bool is_keyframe) {
  return make_scoped_refptr(
      new StreamParserBuffer(data, data_size, NULL, 0, is_keyframe));
}

scoped_refptr<StreamParserBuffer> StreamParserBuffer::CopyFrom(
    const uint8* data, int data_size,
    const uint8* side_data, int side_data_size, bool is_keyframe) {
  return make_scoped_refptr(
      new StreamParserBuffer(data, data_size, side_data, side_data_size,
                             is_keyframe));
}

base::TimeDelta StreamParserBuffer::GetDecodeTimestamp() const {
  if (decode_timestamp_ == kNoTimestamp())
    return timestamp();
  return decode_timestamp_;
}

void StreamParserBuffer::SetDecodeTimestamp(const base::TimeDelta& timestamp) {
  decode_timestamp_ = timestamp;
}

StreamParserBuffer::StreamParserBuffer(const uint8* data, int data_size,
                                       const uint8* side_data,
                                       int side_data_size, bool is_keyframe)
    : DecoderBuffer(data, data_size, side_data, side_data_size),
      is_keyframe_(is_keyframe),
      decode_timestamp_(kNoTimestamp()),
      config_id_(kInvalidConfigId) {
  // TODO(scherkus): Should DataBuffer constructor accept a timestamp and
  // duration to force clients to set them? Today they end up being zero which
  // is both a common and valid value and could lead to bugs.
  if (data) {
    set_duration(kNoTimestamp());
  }
}

StreamParserBuffer::~StreamParserBuffer() {
}

int StreamParserBuffer::GetConfigId() const {
  return config_id_;
}

void StreamParserBuffer::SetConfigId(int config_id) {
  config_id_ = config_id;
}

}  // namespace media
