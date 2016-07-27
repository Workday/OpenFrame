// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_QUIC_PACKET_CREATOR_PEER_H_
#define NET_QUIC_TEST_TOOLS_QUIC_PACKET_CREATOR_PEER_H_

#include "net/quic/quic_protocol.h"

namespace net {
class QuicPacketCreator;

namespace test {

class QuicPacketCreatorPeer {
 public:
  static bool SendVersionInPacket(QuicPacketCreator* creator);

  static void SetSendVersionInPacket(QuicPacketCreator* creator,
                                     bool send_version_in_packet);
  static void SetPacketNumberLength(
      QuicPacketCreator* creator,
      QuicPacketNumberLength packet_number_length);
  static QuicPacketNumberLength GetPacketNumberLength(
      QuicPacketCreator* creator);
  static void SetNextPacketNumberLength(
      QuicPacketCreator* creator,
      QuicPacketNumberLength next_packet_number_length);
  static QuicPacketNumberLength NextPacketNumberLength(
      QuicPacketCreator* creator);
  static void SetPacketNumber(QuicPacketCreator* creator, QuicPacketNumber s);
  static void FillPacketHeader(QuicPacketCreator* creator,
                               QuicFecGroupNumber fec_group,
                               bool fec_flag,
                               QuicPacketHeader* header);
  static size_t CreateStreamFrame(QuicPacketCreator* creator,
                                  QuicStreamId id,
                                  QuicIOVector iov,
                                  size_t iov_offset,
                                  QuicStreamOffset offset,
                                  bool fin,
                                  QuicFrame* frame);
  static bool IsFecProtected(QuicPacketCreator* creator);
  static bool IsFecEnabled(QuicPacketCreator* creator);
  static void StartFecProtectingPackets(QuicPacketCreator* creator);
  static void StopFecProtectingPackets(QuicPacketCreator* creator);
  static SerializedPacket SerializeFec(QuicPacketCreator* creator,
                                       char* buffer,
                                       size_t buffer_len);
  static void ResetFecGroup(QuicPacketCreator* creator);
  static QuicTime::Delta GetFecTimeout(QuicPacketCreator* creator);
  // TODO(rtenneti): Delete this code after the 0.25 RTT FEC experiment.
  static float GetRttMultiplierForFecTimeout(QuicPacketCreator* creator);

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicPacketCreatorPeer);
};

}  // namespace test

}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_QUIC_PACKET_CREATOR_PEER_H_
