// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Streams classes.
//
// These memory-resident streams are used for serializing data into a sequential
// region of memory.
//
// Streams are divided into SourceStreams for reading and SinkStreams for
// writing.  Streams are aggregated into Sets which allows several streams to be
// used at once.  Example: we can write A1, B1, A2, B2 but achieve the memory
// layout A1 A2 B1 B2 by writing 'A's to one stream and 'B's to another.
//
// The aggregated streams are important to Courgette's compression efficiency,
// we use it to cluster similar kinds of data which helps to generate longer
// common subsequences and repeated sequences.

#include "courgette/streams.h"

#include <memory.h>

#include "base/basictypes.h"
#include "base/logging.h"

namespace courgette {

// Update this version number if the serialization format of a StreamSet
// changes.
static const unsigned int kStreamsSerializationFormatVersion = 20090218;

//
// This is a cut down Varint implementation, implementing only what we use for
// streams.
//
class Varint {
 public:
  // Maximum lengths of varint encoding of uint32
  static const int kMax32 = 5;

  // Parses a Varint32 encoded value from |source| and stores it in |output|,
  // and returns a pointer to the following byte.  Returns NULL if a valid
  // varint value was not found before |limit|.
  static const uint8* Parse32WithLimit(const uint8* source, const uint8* limit,
                                       uint32* output);

  // Writes the Varint32 encoded representation of |value| to buffer
  // |destination|.  |destination| must have sufficient length to hold kMax32
  // bytes.  Returns a pointer to the byte just past the last encoded byte.
  static uint8* Encode32(uint8* destination, uint32 value);
};

// Parses a Varint32 encoded unsigned number from |source|.  The Varint32
// encoding is a little-endian sequence of bytes containing base-128 digits,
// with the high order bit set to indicate if there are more digits.
//
// For each byte, we mask out the digit and 'or' it into the right place in the
// result.
//
// The digit loop is unrolled for performance.  It usually exits after the first
// one or two digits.
const uint8* Varint::Parse32WithLimit(const uint8* source,
                                      const uint8* limit,
                                      uint32* output) {
  uint32 digit, result;
  if (source >= limit)
    return NULL;
  digit = *(source++);
  result = digit & 127;
  if (digit < 128) {
    *output = result;
    return source;
  }

  if (source >= limit)
    return NULL;
  digit = *(source++);
  result |= (digit & 127) <<  7;
  if (digit < 128) {
    *output = result;
    return source;
  }

  if (source >= limit)
    return NULL;
  digit = *(source++);
  result |= (digit & 127) << 14;
  if (digit < 128) {
    *output = result;
    return source;
  }

  if (source >= limit)
    return NULL;
  digit = *(source++);
  result |= (digit & 127) << 21;
  if (digit < 128) {
    *output = result;
    return source;
  }

  if (source >= limit)
    return NULL;
  digit = *(source++);
  result |= (digit & 127) << 28;
  if (digit < 128) {
    *output = result;
    return source;
  }

  return NULL;  // Value is too long to be a Varint32.
}

// Write the base-128 digits in little-endian order.  All except the last digit
// have the high bit set to indicate more digits.
inline uint8* Varint::Encode32(uint8* destination, uint32 value) {
  while (value >= 128) {
    *(destination++) = value | 128;
    value = value >> 7;
  }
  *(destination++) = value;
  return destination;
}

void SourceStream::Init(const SinkStream& sink) {
  Init(sink.Buffer(), sink.Length());
}

bool SourceStream::Read(void* destination, size_t count) {
  if (current_ + count > end_)
    return false;
  memcpy(destination, current_, count);
  current_ += count;
  return true;
}

bool SourceStream::ReadVarint32(uint32* output_value) {
  const uint8* after = Varint::Parse32WithLimit(current_, end_, output_value);
  if (!after)
    return false;
  current_ = after;
  return true;
}

bool SourceStream::ReadVarint32Signed(int32* output_value) {
  // Signed numbers are encoded as unsigned numbers so that numbers nearer zero
  // have shorter varint encoding.
  //  0000xxxx encoded as 000xxxx0.
  //  1111xxxx encoded as 000yyyy1 where yyyy is complement of xxxx.
  uint32 unsigned_value;
  if (!ReadVarint32(&unsigned_value))
    return false;
  if (unsigned_value & 1)
    *output_value = ~static_cast<int32>(unsigned_value >> 1);
  else
    *output_value = (unsigned_value >> 1);
  return true;
}

bool SourceStream::ShareSubstream(size_t offset, size_t length,
                                  SourceStream* substream) {
  if (offset > Remaining())
    return false;
  if (length > Remaining() - offset)
    return false;
  substream->Init(current_ + offset, length);
  return true;
}

bool SourceStream::ReadSubstream(size_t length, SourceStream* substream) {
  if (!ShareSubstream(0, length, substream))
    return false;
  current_ += length;
  return true;
}

bool SourceStream::Skip(size_t byte_count) {
  if (current_ + byte_count > end_)
    return false;
  current_ += byte_count;
  return true;
}

CheckBool SinkStream::Write(const void* data, size_t byte_count) {
  return buffer_.append(static_cast<const char*>(data), byte_count);
}

CheckBool SinkStream::WriteVarint32(uint32 value) {
  uint8 buffer[Varint::kMax32];
  uint8* end = Varint::Encode32(buffer, value);
  return Write(buffer, end - buffer);
}

CheckBool SinkStream::WriteVarint32Signed(int32 value) {
  // Encode signed numbers so that numbers nearer zero have shorter
  // varint encoding.
  //  0000xxxx encoded as 000xxxx0.
  //  1111xxxx encoded as 000yyyy1 where yyyy is complement of xxxx.
  bool ret;
  if (value < 0)
    ret = WriteVarint32(~value * 2 + 1);
  else
    ret = WriteVarint32(value * 2);
  return ret;
}

CheckBool SinkStream::WriteSizeVarint32(size_t value) {
  uint32 narrowed_value = static_cast<uint32>(value);
  // On 32-bit, the compiler should figure out this test always fails.
  LOG_ASSERT(value == narrowed_value);
  return WriteVarint32(narrowed_value);
}

CheckBool SinkStream::Append(SinkStream* other) {
  bool ret = Write(other->buffer_.data(), other->buffer_.size());
  if (ret)
    other->Retire();
  return ret;
}

void SinkStream::Retire() {
  buffer_.clear();
}

////////////////////////////////////////////////////////////////////////////////

SourceStreamSet::SourceStreamSet()
    : count_(kMaxStreams) {
}

SourceStreamSet::~SourceStreamSet() {
}


// Initializes from |source|.
// The stream set for N streams is serialized as a header
//   <version><N><length1><length2>...<lengthN>
// followed by the stream contents
//   <bytes1><bytes2>...<bytesN>
//
bool SourceStreamSet::Init(const void* source, size_t byte_count) {
  const uint8* start = static_cast<const uint8*>(source);
  const uint8* end = start + byte_count;

  unsigned int version;
  const uint8* finger = Varint::Parse32WithLimit(start, end, &version);
  if (finger == NULL)
    return false;
  if (version != kStreamsSerializationFormatVersion)
    return false;

  unsigned int count;
  finger = Varint::Parse32WithLimit(finger, end, &count);
  if (finger == NULL)
    return false;
  if (count > kMaxStreams)
    return false;

  count_ = count;

  unsigned int lengths[kMaxStreams];
  size_t accumulated_length = 0;

  for (size_t i = 0; i < count_; ++i) {
    finger = Varint::Parse32WithLimit(finger, end, &lengths[i]);
    if (finger == NULL)
      return false;
    accumulated_length += lengths[i];
  }

  // Remaining bytes should add up to sum of lengths.
  if (static_cast<size_t>(end - finger) != accumulated_length)
    return false;

  accumulated_length = finger - start;
  for (size_t i = 0; i < count_; ++i) {
    stream(i)->Init(start + accumulated_length, lengths[i]);
    accumulated_length += lengths[i];
  }

  return true;
}

bool SourceStreamSet::Init(SourceStream* source) {
  // TODO(sra): consume the rest of |source|.
  return Init(source->Buffer(), source->Remaining());
}

bool SourceStreamSet::ReadSet(SourceStreamSet* set) {
  uint32 stream_count = 0;
  SourceStream* control_stream = this->stream(0);
  if (!control_stream->ReadVarint32(&stream_count))
    return false;

  uint32 lengths[kMaxStreams] = {};  // i.e. all zero.

  for (size_t i = 0; i < stream_count; ++i) {
    if (!control_stream->ReadVarint32(&lengths[i]))
      return false;
  }

  for (size_t i = 0; i < stream_count; ++i) {
    if (!this->stream(i)->ReadSubstream(lengths[i], set->stream(i)))
      return false;
  }
  return true;
}

bool SourceStreamSet::Empty() const {
  for (size_t i = 0; i < count_; ++i) {
    if (streams_[i].Remaining() != 0)
      return false;
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////

SinkStreamSet::SinkStreamSet()
  : count_(kMaxStreams) {
}

SinkStreamSet::~SinkStreamSet() {
}

void SinkStreamSet::Init(size_t stream_index_limit) {
  count_ = stream_index_limit;
}

// The header for a stream set for N streams is serialized as
//   <version><N><length1><length2>...<lengthN>
CheckBool SinkStreamSet::CopyHeaderTo(SinkStream* header) {
  bool ret = header->WriteVarint32(kStreamsSerializationFormatVersion);
  if (ret) {
    ret = header->WriteSizeVarint32(count_);
    for (size_t i = 0; ret && i < count_; ++i) {
      ret = header->WriteSizeVarint32(stream(i)->Length());
    }
  }
  return ret;
}

// Writes |this| to |combined_stream|.  See SourceStreamSet::Init for the layout
// of the stream metadata and contents.
CheckBool SinkStreamSet::CopyTo(SinkStream *combined_stream) {
  SinkStream header;
  bool ret = CopyHeaderTo(&header);
  if (!ret)
    return ret;

  // Reserve the correct amount of storage.
  size_t length = header.Length();
  for (size_t i = 0; i < count_; ++i) {
    length += stream(i)->Length();
  }
  ret = combined_stream->Reserve(length);
  if (ret) {
    ret = combined_stream->Append(&header);
    for (size_t i = 0; ret && i < count_; ++i) {
      ret = combined_stream->Append(stream(i));
    }
  }
  return ret;
}

CheckBool SinkStreamSet::WriteSet(SinkStreamSet* set) {
  uint32 lengths[kMaxStreams];
  // 'stream_count' includes all non-empty streams and all empty stream numbered
  // lower than a non-empty stream.
  size_t stream_count = 0;
  for (size_t i = 0; i < kMaxStreams; ++i) {
    SinkStream* stream = set->stream(i);
    lengths[i] = static_cast<uint32>(stream->Length());
    if (lengths[i] > 0)
      stream_count = i + 1;
  }

  SinkStream* control_stream = this->stream(0);
  bool ret = control_stream->WriteSizeVarint32(stream_count);
  for (size_t i = 0; ret && i < stream_count; ++i) {
    ret = control_stream->WriteSizeVarint32(lengths[i]);
  }

  for (size_t i = 0; ret && i < stream_count; ++i) {
    ret = this->stream(i)->Append(set->stream(i));
  }
  return ret;
}

}  // namespace
