// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#ifndef NET_QUIC_QUIC_WRITE_BLOCKED_LIST_H_
#define NET_QUIC_QUIC_WRITE_BLOCKED_LIST_H_

#include <set>

#include "net/base/net_export.h"
#include "net/quic/quic_flags.h"
#include "net/quic/quic_protocol.h"
#include "net/spdy/write_blocked_list.h"

namespace net {

// Keeps tracks of the QUIC streams that have data to write, sorted by
// priority.  QUIC stream priority order is:
// Crypto stream > Headers stream > Data streams by requested priority.
class NET_EXPORT_PRIVATE QuicWriteBlockedList {
 private:
  typedef WriteBlockedList<QuicStreamId> QuicWriteBlockedListBase;

 public:
  QuicWriteBlockedList();
  ~QuicWriteBlockedList();

  bool HasWriteBlockedDataStreams() const {
    return base_write_blocked_list_.HasWriteBlockedStreams();
  }

  bool HasWriteBlockedCryptoOrHeadersStream() const {
    return crypto_stream_blocked_ || headers_stream_blocked_;
  }

  size_t NumBlockedStreams() const {
    size_t num_blocked = base_write_blocked_list_.NumBlockedStreams();
    if (crypto_stream_blocked_) {
      ++num_blocked;
    }
    if (headers_stream_blocked_) {
      ++num_blocked;
    }

    return num_blocked;
  }

  // Pops the highest priorty stream, special casing crypto and headers streams.
  // Latches the most recently popped data stream for batch writing purposes.
  QuicStreamId PopFront() {
    if (crypto_stream_blocked_) {
      crypto_stream_blocked_ = false;
      return kCryptoStreamId;
    }

    if (headers_stream_blocked_) {
      headers_stream_blocked_ = false;
      return kHeadersStreamId;
    }

    SpdyPriority priority =
        base_write_blocked_list_.GetHighestPriorityWriteBlockedList();
    QuicStreamId id = base_write_blocked_list_.PopFront(priority);

    if (base_write_blocked_list_.NumBlockedStreams(priority) == 0) {
      // If no streams are blocked, don't bother latching.  This stream will be
      // the first popped for its priority anyway.
      batch_write_stream_id_[priority] = 0;
      last_priority_popped_ = priority;
    } else if (batch_write_stream_id_[priority] != id) {
      // If newly latching this batch write stream, let it write 16k.
      batch_write_stream_id_[priority] = id;
      bytes_left_for_batch_write_[priority] = 16000;
      last_priority_popped_ = priority;
    }

    return id;
  }

  void UpdateBytesForStream(QuicStreamId stream_id, size_t bytes) {
    if (batch_write_stream_id_[last_priority_popped_] == stream_id) {
      // If this was the last data stream popped by PopFront, update the
      // bytes remaining in its batch write.
      bytes_left_for_batch_write_[last_priority_popped_] -=
          static_cast<int32>(bytes);
    } else {
      // If a batch write stream was set, it should only be preempted by the
      // crypto or headers streams.  Any higher priority data stream would
      // *become* the new batch write stream.
      if (FLAGS_quic_respect_send_alarm && FLAGS_quic_batch_writes) {
        DCHECK(stream_id == kCryptoStreamId || stream_id == kHeadersStreamId ||
               batch_write_stream_id_[last_priority_popped_] == 0 ||
               bytes == 0);
      }
    }
  }

  // Pushes a stream to the back of the std::list for this priority level
  // *unless* it is latched for doing batched writes in which case it goes to
  // the front of the std::list for this priority level.
  // Headers and crypto streams are special cased to always resume first.
  void AddStream(QuicStreamId stream_id, SpdyPriority priority) {
    if (stream_id == kCryptoStreamId) {
      DCHECK_EQ(kV3HighestPriority, priority);
      // TODO(avd) Add DCHECK(!crypto_stream_blocked_)
      crypto_stream_blocked_ = true;
      return;
    }

    if (stream_id == kHeadersStreamId) {
      DCHECK_EQ(kV3HighestPriority, priority);
      // TODO(avd) Add DCHECK(!headers_stream_blocked_);
      headers_stream_blocked_ = true;
      return;
    }
    if (FLAGS_quic_batch_writes &&
        stream_id == batch_write_stream_id_[last_priority_popped_] &&
        bytes_left_for_batch_write_[last_priority_popped_] > 0) {
      // If the batch write stream has more data to write, push it to the front
      // for its priority level.
      base_write_blocked_list_.PushFront(stream_id, priority);
    } else {
      base_write_blocked_list_.PushBack(stream_id, priority);
    }
    return;
  }

  bool crypto_stream_blocked() const { return crypto_stream_blocked_; }
  bool headers_stream_blocked() const { return headers_stream_blocked_; }

 private:
  QuicWriteBlockedListBase base_write_blocked_list_;

  // If performing batch writes, this will be the stream ID of the stream doing
  // batch writes for this priority level.  We will allow this stream to write
  // until it has written kBatchWriteSize bytes, it has no more data to write,
  // or a higher priority stream preempts.
  QuicStreamId batch_write_stream_id_[kV3LowestPriority + 1];
  // Set to kBatchWriteSize when we set a new batch_write_stream_id_ for a given
  // priority.  This is decremented with each write the stream does until it is
  // done with its batch write.
  int32 bytes_left_for_batch_write_[kV3LowestPriority + 1];
  // Tracks the last priority popped for UpdateBytesForStream.
  SpdyPriority last_priority_popped_;

  bool crypto_stream_blocked_;
  bool headers_stream_blocked_;

  DISALLOW_COPY_AND_ASSIGN(QuicWriteBlockedList);
};

}  // namespace net


#endif  // NET_QUIC_QUIC_WRITE_BLOCKED_LIST_H_
