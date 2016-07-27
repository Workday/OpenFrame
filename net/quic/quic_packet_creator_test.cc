// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_packet_creator.h"

#include <stdint.h>

#include "base/stl_util.h"
#include "net/quic/crypto/null_encrypter.h"
#include "net/quic/crypto/quic_decrypter.h"
#include "net/quic/crypto/quic_encrypter.h"
#include "net/quic/quic_flags.h"
#include "net/quic/quic_utils.h"
#include "net/quic/test_tools/mock_random.h"
#include "net/quic/test_tools/quic_framer_peer.h"
#include "net/quic/test_tools/quic_packet_creator_peer.h"
#include "net/quic/test_tools/quic_test_utils.h"
#include "net/test/gtest_util.h"
#include "testing/gmock/include/gmock/gmock.h"

using base::StringPiece;
using std::ostream;
using std::string;
using std::vector;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::SaveArg;
using testing::StrictMock;
using testing::_;

namespace net {
namespace test {
namespace {

// Run tests with combinations of {QuicVersion, ToggleVersionSerialization}.
struct TestParams {
  TestParams(QuicVersion version,
             bool version_serialization,
             QuicConnectionIdLength length,
             bool copy_use_prefetch)
      : version(version),
        connection_id_length(length),
        version_serialization(version_serialization),
        copy_use_prefetch(copy_use_prefetch) {}

  friend ostream& operator<<(ostream& os, const TestParams& p) {
    os << "{ client_version: " << QuicVersionToString(p.version)
       << " connection id length: " << p.connection_id_length
       << " include version: " << p.version_serialization
       << " copy use prefetch: " << p.copy_use_prefetch << " }";
    return os;
  }

  QuicVersion version;
  QuicConnectionIdLength connection_id_length;
  bool version_serialization;
  bool copy_use_prefetch;
};

// Constructs various test permutations.
vector<TestParams> GetTestParams() {
  vector<TestParams> params;
  QuicConnectionIdLength max = PACKET_8BYTE_CONNECTION_ID;
  QuicVersionVector all_supported_versions = QuicSupportedVersions();
  for (size_t i = 0; i < all_supported_versions.size(); ++i) {
    params.push_back(TestParams(all_supported_versions[i], true, max, false));
    params.push_back(TestParams(all_supported_versions[i], false, max, false));
  }
  params.push_back(TestParams(all_supported_versions[0], true,
                              PACKET_0BYTE_CONNECTION_ID, false));
  params.push_back(TestParams(all_supported_versions[0], true,
                              PACKET_1BYTE_CONNECTION_ID, false));
  params.push_back(TestParams(all_supported_versions[0], true,
                              PACKET_4BYTE_CONNECTION_ID, false));
  params.push_back(TestParams(all_supported_versions[0], true, max, true));
  return params;
}

class MockDelegate : public QuicPacketCreator::DelegateInterface {
 public:
  MockDelegate() {}
  ~MockDelegate() override {}

  MOCK_METHOD1(OnSerializedPacket, void(SerializedPacket* packet));
  MOCK_METHOD0(OnResetFecGroup, void());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockDelegate);
};

class QuicPacketCreatorTest : public ::testing::TestWithParam<TestParams> {
 public:
  void ClearSerializedPacket(SerializedPacket* serialized_packet) {
    if (serialized_packet == nullptr) {
      return;
    }

    delete serialized_packet->retransmittable_frames;
    delete serialized_packet->packet;
  }

  void SaveSerializedPacket(SerializedPacket* serialized_packet) {
    if (serialized_packet == nullptr) {
      return;
    }

    serialized_packet_ = *serialized_packet;
    serialized_packet_.packet = serialized_packet->packet->Clone();
    delete serialized_packet->packet;
  }

 protected:
  QuicPacketCreatorTest()
      : server_framer_(SupportedVersions(GetParam().version),
                       QuicTime::Zero(),
                       Perspective::IS_SERVER),
        client_framer_(SupportedVersions(GetParam().version),
                       QuicTime::Zero(),
                       Perspective::IS_CLIENT),
        connection_id_(2),
        data_("foo"),
        creator_(connection_id_, &client_framer_, &mock_random_, &delegate_),
        serialized_packet_(creator_.NoPacket()) {
    creator_.set_connection_id_length(GetParam().connection_id_length);
    client_framer_.set_visitor(&framer_visitor_);
    client_framer_.set_received_entropy_calculator(&entropy_calculator_);
    server_framer_.set_visitor(&framer_visitor_);
    FLAGS_quic_packet_creator_prefetch = GetParam().copy_use_prefetch;
  }

  ~QuicPacketCreatorTest() override {}

  void ProcessPacket(QuicEncryptedPacket* encrypted) {
    server_framer_.ProcessPacket(*encrypted);
  }

  void CheckStreamFrame(const QuicFrame& frame,
                        QuicStreamId stream_id,
                        const string& data,
                        QuicStreamOffset offset,
                        bool fin) {
    EXPECT_EQ(STREAM_FRAME, frame.type);
    ASSERT_TRUE(frame.stream_frame);
    EXPECT_EQ(stream_id, frame.stream_frame->stream_id);
    EXPECT_EQ(data, frame.stream_frame->data);
    EXPECT_EQ(offset, frame.stream_frame->offset);
    EXPECT_EQ(fin, frame.stream_frame->fin);
  }

  // Returns the number of bytes consumed by the header of packet, including
  // the version.
  size_t GetPacketHeaderOverhead(InFecGroup is_in_fec_group) {
    return GetPacketHeaderSize(
        creator_.connection_id_length(), kIncludeVersion, !kIncludePathId,
        QuicPacketCreatorPeer::NextPacketNumberLength(&creator_),
        is_in_fec_group);
  }

  // Returns the number of bytes of overhead that will be added to a packet
  // of maximum length.
  size_t GetEncryptionOverhead() {
    return creator_.max_packet_length() - client_framer_.GetMaxPlaintextSize(
        creator_.max_packet_length());
  }

  // Returns the number of bytes consumed by the non-data fields of a stream
  // frame, assuming it is the last frame in the packet
  size_t GetStreamFrameOverhead(InFecGroup is_in_fec_group) {
    return QuicFramer::GetMinStreamFrameSize(kClientDataStreamId1, kOffset,
                                             true, is_in_fec_group);
  }

  // Enables and turns on FEC protection. Returns true if FEC protection is on.
  bool SwitchFecProtectionOn(size_t max_packets_per_fec_group) {
    creator_.set_max_packets_per_fec_group(max_packets_per_fec_group);
    creator_.MaybeStartFecProtection();
    return QuicPacketCreatorPeer::IsFecProtected(&creator_);
  }

  QuicIOVector MakeIOVector(StringPiece s) {
    return ::net::MakeIOVector(s, &iov_);
  }

  static const QuicStreamOffset kOffset = 1u;

  QuicFrames frames_;
  QuicFramer server_framer_;
  QuicFramer client_framer_;
  StrictMock<MockFramerVisitor> framer_visitor_;
  StrictMock<MockDelegate> delegate_;
  QuicConnectionId connection_id_;
  string data_;
  struct iovec iov_;
  MockRandom mock_random_;
  QuicPacketCreator creator_;
  MockEntropyCalculator entropy_calculator_;
  SerializedPacket serialized_packet_;
};

// Run all packet creator tests with all supported versions of QUIC, and with
// and without version in the packet header, as well as doing a run for each
// length of truncated connection id.
INSTANTIATE_TEST_CASE_P(QuicPacketCreatorTests,
                        QuicPacketCreatorTest,
                        ::testing::ValuesIn(GetTestParams()));

TEST_P(QuicPacketCreatorTest, SerializeFrames) {
  frames_.push_back(QuicFrame(new QuicAckFrame(MakeAckFrame(0u))));
  frames_.push_back(
      QuicFrame(new QuicStreamFrame(0u, false, 0u, StringPiece())));
  frames_.push_back(
      QuicFrame(new QuicStreamFrame(0u, true, 0u, StringPiece())));
  char buffer[kMaxPacketSize];
  SerializedPacket serialized =
      creator_.SerializeAllFrames(frames_, buffer, kMaxPacketSize);
  delete frames_[0].ack_frame;
  delete frames_[1].stream_frame;
  delete frames_[2].stream_frame;

  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnAckFrame(_));
    EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized.packet);
  delete serialized.packet;
}

TEST_P(QuicPacketCreatorTest, SerializeWithFEC) {
  // Enable FEC protection, and send FEC packet every 6 packets.
  EXPECT_TRUE(SwitchFecProtectionOn(6));
  // Should return false since we do not have enough packets in the FEC group to
  // trigger an FEC packet.
  ASSERT_FALSE(creator_.ShouldSendFec(/*force_close=*/false));

  frames_.push_back(
      QuicFrame(new QuicStreamFrame(0u, false, 0u, StringPiece())));
  char buffer[kMaxPacketSize];
  SerializedPacket serialized =
      creator_.SerializeAllFrames(frames_, buffer, kMaxPacketSize);
  delete frames_[0].stream_frame;

  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnFecProtectedPayload(_));
    EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized.packet);
  delete serialized.packet;

  // Should return false since we do not have enough packets in the FEC group to
  // trigger an FEC packet.
  ASSERT_FALSE(creator_.ShouldSendFec(/*force_close=*/false));
  // Should return true since there are packets in the FEC group.
  ASSERT_TRUE(creator_.ShouldSendFec(/*force_close=*/true));

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.set_should_fec_protect(true);
  creator_.MaybeSendFecPacketAndCloseGroup(/*force_send_fec=*/true,
                                           /*is_fec_timeout=*/false);
  ASSERT_EQ(2u, serialized_packet_.packet_number);
  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnFecData(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized_packet_.packet);
  ClearSerializedPacket(&serialized_packet_);
}

TEST_P(QuicPacketCreatorTest, SerializeChangingSequenceNumberLength) {
  frames_.push_back(QuicFrame(new QuicAckFrame(MakeAckFrame(0u))));
  creator_.AddSavedFrame(frames_[0]);
  QuicPacketCreatorPeer::SetNextPacketNumberLength(&creator_,
                                                   PACKET_4BYTE_PACKET_NUMBER);

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.Flush();
  // The packet number length will not change mid-packet.
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
            serialized_packet_.packet_number_length);

  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnAckFrame(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized_packet_.packet);
  ClearSerializedPacket(&serialized_packet_);

  creator_.AddSavedFrame(frames_[0]);
  creator_.Flush();
  // Now the actual packet number length should have changed.
  EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER,
            serialized_packet_.packet_number_length);
  delete frames_[0].ack_frame;

  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnAckFrame(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized_packet_.packet);
  ClearSerializedPacket(&serialized_packet_);
}

TEST_P(QuicPacketCreatorTest, ChangeSequenceNumberLengthMidPacket) {
  // Changing the packet number length with queued frames in the creator
  // should hold the change until after any currently queued frames are
  // serialized.

  // Packet 1.
  // Queue a frame in the creator.
  EXPECT_FALSE(creator_.HasPendingFrames());
  QuicFrame ack_frame = QuicFrame(new QuicAckFrame(MakeAckFrame(0u)));
  creator_.AddSavedFrame(ack_frame);

  // Now change packet number length.
  QuicPacketCreatorPeer::SetNextPacketNumberLength(&creator_,
                                                   PACKET_4BYTE_PACKET_NUMBER);

  // Add a STOP_WAITING frame since it contains a packet number,
  // whose length should be 1.
  QuicStopWaitingFrame stop_waiting_frame;
  EXPECT_TRUE(creator_.AddSavedFrame(QuicFrame(&stop_waiting_frame)));
  EXPECT_TRUE(creator_.HasPendingFrames());

  // Ensure the packet is successfully created.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.Flush();
  ASSERT_TRUE(serialized_packet_.packet);
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
            serialized_packet_.packet_number_length);

  // Verify that header in transmitted packet has 1 byte sequence length.
  QuicPacketHeader header;
  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_)).WillOnce(
        DoAll(SaveArg<0>(&header), Return(true)));
    EXPECT_CALL(framer_visitor_, OnAckFrame(_));
    EXPECT_CALL(framer_visitor_, OnStopWaitingFrame(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized_packet_.packet);
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
            header.public_header.packet_number_length);
  ClearSerializedPacket(&serialized_packet_);

  // Packet 2.
  EXPECT_FALSE(creator_.HasPendingFrames());
  // Generate Packet 2 with one frame -- packet number length should now
  // change to 4 bytes.
  EXPECT_TRUE(creator_.AddSavedFrame(QuicFrame(&stop_waiting_frame)));
  EXPECT_TRUE(creator_.HasPendingFrames());

  // Ensure the packet is successfully created.
  creator_.Flush();
  ASSERT_TRUE(serialized_packet_.packet);
  EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER,
            serialized_packet_.packet_number_length);

  // Verify that header in transmitted packet has 4 byte sequence length.
  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_)).WillOnce(
        DoAll(SaveArg<0>(&header), Return(true)));
    EXPECT_CALL(framer_visitor_, OnStopWaitingFrame(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized_packet_.packet);
  EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER,
            header.public_header.packet_number_length);

  ClearSerializedPacket(&serialized_packet_);
  delete ack_frame.ack_frame;
}

TEST_P(QuicPacketCreatorTest, SerializeWithFECChangingSequenceNumberLength) {
  // Test goal is to test the following sequence (P1 => generate Packet 1):
  // P1 <change seq num length> P2 FEC,
  // and we expect that packet number length should not change until the end
  // of the open FEC group.

  // Enable FEC protection, and send FEC packet every 6 packets.
  EXPECT_TRUE(SwitchFecProtectionOn(6));
  // Should return false since we do not have enough packets in the FEC group to
  // trigger an FEC packet.
  ASSERT_FALSE(creator_.ShouldSendFec(/*force_close=*/false));
  frames_.push_back(QuicFrame(new QuicAckFrame(MakeAckFrame(0u))));

  // Generate Packet 1.
  creator_.AddSavedFrame(frames_[0]);
  // Change the packet number length mid-FEC group and it should not change.
  QuicPacketCreatorPeer::SetNextPacketNumberLength(&creator_,
                                                   PACKET_4BYTE_PACKET_NUMBER);
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.Flush();
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
            serialized_packet_.packet_number_length);

  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnFecProtectedPayload(_));
    EXPECT_CALL(framer_visitor_, OnAckFrame(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized_packet_.packet);
  ClearSerializedPacket(&serialized_packet_);

  // Generate Packet 2.
  creator_.AddSavedFrame(frames_[0]);
  creator_.Flush();
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
            serialized_packet_.packet_number_length);

  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnFecProtectedPayload(_));
    EXPECT_CALL(framer_visitor_, OnAckFrame(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized_packet_.packet);
  ClearSerializedPacket(&serialized_packet_);

  // Should return false since we do not have enough packets in the FEC group to
  // trigger an FEC packet.
  ASSERT_FALSE(creator_.ShouldSendFec(/*force_close=*/false));
  // Should return true since there are packets in the FEC group.
  ASSERT_TRUE(creator_.ShouldSendFec(/*force_close=*/true));

  // Force generation of FEC packet.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.set_should_fec_protect(true);
  creator_.MaybeSendFecPacketAndCloseGroup(/*force_send_fec=*/true,
                                           /*is_fec_timeout=*/false);
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
            serialized_packet_.packet_number_length);
  ASSERT_EQ(3u, serialized_packet_.packet_number);

  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnFecData(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized_packet_.packet);
  ClearSerializedPacket(&serialized_packet_);

  // Ensure the next FEC group starts using the new packet number length.
  char buffer[kMaxPacketSize];
  SerializedPacket serialized =
      creator_.SerializeAllFrames(frames_, buffer, kMaxPacketSize);
  EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER, serialized.packet_number_length);
  delete frames_[0].ack_frame;
  ClearSerializedPacket(&serialized);
}

TEST_P(QuicPacketCreatorTest, ReserializeFramesWithSequenceNumberLength) {
  // If the original packet number length, the current packet number
  // length, and the configured send packet number length are different, the
  // retransmit must sent with the original length and the others do not change.
  QuicPacketCreatorPeer::SetNextPacketNumberLength(&creator_,
                                                   PACKET_4BYTE_PACKET_NUMBER);
  QuicPacketCreatorPeer::SetPacketNumberLength(&creator_,
                                               PACKET_2BYTE_PACKET_NUMBER);
  QuicStreamFrame* stream_frame =
      new QuicStreamFrame(kCryptoStreamId, /*fin=*/false, 0u, StringPiece());
  RetransmittableFrames frames(ENCRYPTION_NONE);
  frames.AddFrame(QuicFrame(stream_frame));
  char buffer[kMaxPacketSize];
  SerializedPacket serialized = creator_.ReserializeAllFrames(
      frames, PACKET_1BYTE_PACKET_NUMBER, buffer, kMaxPacketSize);
  EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::NextPacketNumberLength(&creator_));
  EXPECT_EQ(PACKET_2BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER, serialized.packet_number_length);

  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized.packet);
  delete serialized.packet;
}

TEST_P(QuicPacketCreatorTest, ReserializeFramesWithPadding) {
  QuicFrame frame;
  QuicIOVector io_vector(MakeIOVector("fake handshake message data"));
  QuicPacketCreatorPeer::CreateStreamFrame(&creator_, kCryptoStreamId,
                                           io_vector, 0u, 0u, false, &frame);
  RetransmittableFrames frames(ENCRYPTION_NONE);
  frames.AddFrame(frame);
  frames.set_needs_padding(true);
  char buffer[kMaxPacketSize];
  SerializedPacket serialized = creator_.ReserializeAllFrames(
      frames, QuicPacketCreatorPeer::NextPacketNumberLength(&creator_), buffer,
      kMaxPacketSize);
  EXPECT_EQ(kDefaultMaxPacketSize, serialized.packet->length());
  delete serialized.packet;
}

TEST_P(QuicPacketCreatorTest, ReserializeFramesWithFullPacketAndPadding) {
  const size_t overhead = GetPacketHeaderOverhead(NOT_IN_FEC_GROUP)
      + GetEncryptionOverhead() + GetStreamFrameOverhead(NOT_IN_FEC_GROUP);
  size_t capacity = kDefaultMaxPacketSize - overhead;
  for (int delta = -5; delta <= 0; ++delta) {
    string data(capacity + delta, 'A');
    size_t bytes_free = 0 - delta;

    QuicFrame frame;
    QuicIOVector io_vector(MakeIOVector(data));
    QuicPacketCreatorPeer::CreateStreamFrame(
        &creator_, kCryptoStreamId, io_vector, 0, kOffset, false, &frame);
    RetransmittableFrames frames(ENCRYPTION_NONE);
    frames.AddFrame(frame);
    frames.set_needs_padding(true);
    char buffer[kMaxPacketSize];
    SerializedPacket serialized = creator_.ReserializeAllFrames(
        frames, QuicPacketCreatorPeer::NextPacketNumberLength(&creator_),
        buffer, kMaxPacketSize);

    // If there is not enough space in the packet to fit a padding frame
    // (1 byte) and to expand the stream frame (another 2 bytes) the packet
    // will not be padded.
    if (bytes_free < 3) {
      EXPECT_EQ(kDefaultMaxPacketSize - bytes_free,
                serialized.packet->length());
    } else {
      EXPECT_EQ(kDefaultMaxPacketSize, serialized.packet->length());
    }

    delete serialized.packet;
    frames_.clear();
  }
}

TEST_P(QuicPacketCreatorTest, SerializeConnectionClose) {
  QuicConnectionCloseFrame frame;
  frame.error_code = QUIC_NO_ERROR;
  frame.error_details = "error";

  QuicFrames frames;
  frames.push_back(QuicFrame(&frame));
  char buffer[kMaxPacketSize];
  SerializedPacket serialized =
      creator_.SerializeAllFrames(frames, buffer, kMaxPacketSize);
  ASSERT_EQ(1u, serialized.packet_number);
  ASSERT_EQ(1u, creator_.packet_number());

  InSequence s;
  EXPECT_CALL(framer_visitor_, OnPacket());
  EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
  EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
  EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
  EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
  EXPECT_CALL(framer_visitor_, OnConnectionCloseFrame(_));
  EXPECT_CALL(framer_visitor_, OnPacketComplete());

  ProcessPacket(serialized.packet);
  delete serialized.packet;
}

TEST_P(QuicPacketCreatorTest, SwitchFecOnOffWithNoGroup) {
  // Enable FEC protection.
  creator_.set_max_packets_per_fec_group(6);
  EXPECT_TRUE(QuicPacketCreatorPeer::IsFecEnabled(&creator_));
  EXPECT_FALSE(QuicPacketCreatorPeer::IsFecProtected(&creator_));

  // Turn on FEC protection.
  creator_.MaybeStartFecProtection();
  EXPECT_TRUE(QuicPacketCreatorPeer::IsFecProtected(&creator_));
  // We have no packets in the FEC group, so no FEC packet can be created.
  EXPECT_FALSE(creator_.ShouldSendFec(/*force_close=*/true));
  // Since no packets are in FEC group yet, we should be able to turn FEC
  // off with no trouble.
  creator_.MaybeSendFecPacketAndCloseGroup(/*force_send_fec=*/true,
                                           /*is_fec_timeout=*/false);
  EXPECT_FALSE(QuicPacketCreatorPeer::IsFecProtected(&creator_));
}

TEST_P(QuicPacketCreatorTest, SwitchFecOnOffWithGroupInProgress) {
  // Enable FEC protection, and send FEC packet every 6 packets.
  EXPECT_TRUE(SwitchFecProtectionOn(6));
  frames_.push_back(
      QuicFrame(new QuicStreamFrame(0u, false, 0u, StringPiece())));
  char buffer[kMaxPacketSize];
  SerializedPacket serialized =
      creator_.SerializeAllFrames(frames_, buffer, kMaxPacketSize);
  delete frames_[0].stream_frame;
  delete serialized.packet;

  EXPECT_TRUE(QuicPacketCreatorPeer::IsFecProtected(&creator_));
  // We do not have enough packets in the FEC group to trigger an FEC packet.
  EXPECT_FALSE(creator_.ShouldSendFec(/*force_close=*/false));
  // Should return true since there are packets in the FEC group.
  EXPECT_TRUE(creator_.ShouldSendFec(/*force_close=*/true));

  // Switching FEC off should not change creator state, since there is an
  // FEC packet under construction.
  EXPECT_DFATAL(QuicPacketCreatorPeer::StopFecProtectingPackets(&creator_),
                "Cannot stop FEC protection with open FEC group.");
  EXPECT_TRUE(QuicPacketCreatorPeer::IsFecProtected(&creator_));
  // Confirm that FEC packet is still under construction.
  EXPECT_TRUE(creator_.ShouldSendFec(/*force_close=*/true));

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::ClearSerializedPacket));
  // Switching FEC on/off should work now.
  creator_.MaybeSendFecPacketAndCloseGroup(/*force_send_fec=*/true,
                                           /*is_fec_timeout=*/false);
  EXPECT_FALSE(QuicPacketCreatorPeer::IsFecProtected(&creator_));
  creator_.MaybeStartFecProtection();
  EXPECT_TRUE(QuicPacketCreatorPeer::IsFecProtected(&creator_));
}

TEST_P(QuicPacketCreatorTest, SwitchFecOnWithStreamFrameQueued) {
  // Add a stream frame to the creator.
  QuicFrame frame;
  QuicIOVector io_vector(MakeIOVector("test"));
  ASSERT_TRUE(
      creator_.ConsumeData(1u, io_vector, 0u, 0u, false, false, &frame));
  ASSERT_TRUE(frame.stream_frame);
  size_t consumed = frame.stream_frame->data.length();
  EXPECT_EQ(4u, consumed);
  EXPECT_TRUE(creator_.HasPendingFrames());

  // Enable FEC protection, and send FEC packet every 6 packets.
  creator_.set_max_packets_per_fec_group(6);
  EXPECT_TRUE(QuicPacketCreatorPeer::IsFecEnabled(&creator_));
  EXPECT_DFATAL(QuicPacketCreatorPeer::StartFecProtectingPackets(&creator_),
                "Cannot start FEC protection with pending frames.");
  EXPECT_FALSE(QuicPacketCreatorPeer::IsFecProtected(&creator_));

  // Start FEC protection after current open packet is flushed.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::ClearSerializedPacket));
  creator_.MaybeStartFecProtection();
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_TRUE(QuicPacketCreatorPeer::IsFecProtected(&creator_));
}

TEST_P(QuicPacketCreatorTest, ConsumeData) {
  QuicFrame frame;
  QuicIOVector io_vector(MakeIOVector("test"));
  ASSERT_TRUE(
      creator_.ConsumeData(1u, io_vector, 0u, 0u, false, false, &frame));
  ASSERT_TRUE(frame.stream_frame);
  size_t consumed = frame.stream_frame->data.length();
  EXPECT_EQ(4u, consumed);
  CheckStreamFrame(frame, 1u, "test", 0u, false);
  EXPECT_TRUE(creator_.HasPendingFrames());
}

TEST_P(QuicPacketCreatorTest, ConsumeDataFin) {
  QuicFrame frame;
  QuicIOVector io_vector(MakeIOVector("test"));
  ASSERT_TRUE(
      creator_.ConsumeData(1u, io_vector, 0u, 10u, true, false, &frame));
  ASSERT_TRUE(frame.stream_frame);
  size_t consumed = frame.stream_frame->data.length();
  EXPECT_EQ(4u, consumed);
  CheckStreamFrame(frame, 1u, "test", 10u, true);
  EXPECT_TRUE(creator_.HasPendingFrames());
}

TEST_P(QuicPacketCreatorTest, ConsumeDataFinOnly) {
  QuicFrame frame;
  QuicIOVector io_vector(nullptr, 0, 0);
  ASSERT_TRUE(creator_.ConsumeData(1u, io_vector, 0u, 0u, true, false, &frame));
  ASSERT_TRUE(frame.stream_frame);
  size_t consumed = frame.stream_frame->data.length();
  EXPECT_EQ(0u, consumed);
  CheckStreamFrame(frame, 1u, string(), 0u, true);
  EXPECT_TRUE(creator_.HasPendingFrames());
}

TEST_P(QuicPacketCreatorTest, CreateAllFreeBytesForStreamFrames) {
  const size_t overhead = GetPacketHeaderOverhead(NOT_IN_FEC_GROUP)
                          + GetEncryptionOverhead();
  for (size_t i = overhead; i < overhead + 100; ++i) {
    creator_.SetMaxPacketLength(i);
    const bool should_have_room = i > overhead + GetStreamFrameOverhead(
        NOT_IN_FEC_GROUP);
    ASSERT_EQ(should_have_room, creator_.HasRoomForStreamFrame(
                                    kClientDataStreamId1, kOffset));
    if (should_have_room) {
      QuicFrame frame;
      QuicIOVector io_vector(MakeIOVector("testdata"));
      EXPECT_CALL(delegate_, OnSerializedPacket(_))
          .WillRepeatedly(
              Invoke(this, &QuicPacketCreatorTest::ClearSerializedPacket));
      ASSERT_TRUE(creator_.ConsumeData(kClientDataStreamId1, io_vector, 0u,
                                       kOffset, false, false, &frame));
      ASSERT_TRUE(frame.stream_frame);
      size_t bytes_consumed = frame.stream_frame->data.length();
      EXPECT_LT(0u, bytes_consumed);
      creator_.Flush();
    }
  }
}

TEST_P(QuicPacketCreatorTest, StreamFrameConsumption) {
  // Compute the total overhead for a single frame in packet.
  const size_t overhead = GetPacketHeaderOverhead(NOT_IN_FEC_GROUP) +
                          GetEncryptionOverhead() +
                          GetStreamFrameOverhead(NOT_IN_FEC_GROUP);
  size_t capacity = kDefaultMaxPacketSize - overhead;
  // Now, test various sizes around this size.
  for (int delta = -5; delta <= 5; ++delta) {
    string data(capacity + delta, 'A');
    size_t bytes_free = delta > 0 ? 0 : 0 - delta;
    QuicFrame frame;
    QuicIOVector io_vector(MakeIOVector(data));
    size_t bytes_consumed = QuicPacketCreatorPeer::CreateStreamFrame(
        &creator_, kClientDataStreamId1, io_vector, 0u, kOffset, false, &frame);
    EXPECT_EQ(capacity - bytes_free, bytes_consumed);

    ASSERT_TRUE(creator_.AddSavedFrame(frame));
    // BytesFree() returns bytes available for the next frame, which will
    // be two bytes smaller since the stream frame would need to be grown.
    EXPECT_EQ(2u, creator_.ExpansionOnNewFrame());
    size_t expected_bytes_free = bytes_free < 3 ? 0 : bytes_free - 2;
    EXPECT_EQ(expected_bytes_free, creator_.BytesFree()) << "delta: " << delta;
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
    creator_.Flush();
    ASSERT_TRUE(serialized_packet_.packet);
    ClearSerializedPacket(&serialized_packet_);
  }
}

TEST_P(QuicPacketCreatorTest, StreamFrameConsumptionWithFec) {
  // Enable FEC protection, and send FEC packet every 6 packets.
  EXPECT_TRUE(SwitchFecProtectionOn(6));
  // Compute the total overhead for a single frame in packet.
  const size_t overhead = GetPacketHeaderOverhead(IN_FEC_GROUP) +
                          GetEncryptionOverhead() +
                          GetStreamFrameOverhead(IN_FEC_GROUP);
  size_t capacity = kDefaultMaxPacketSize - overhead;
  // Now, test various sizes around this size.
  for (int delta = -5; delta <= 5; ++delta) {
    string data(capacity + delta, 'A');
    size_t bytes_free = delta > 0 ? 0 : 0 - delta;
    QuicFrame frame;
    QuicIOVector io_vector(MakeIOVector(data));
    size_t bytes_consumed = QuicPacketCreatorPeer::CreateStreamFrame(
        &creator_, kClientDataStreamId1, io_vector, 0u, kOffset, false, &frame);
    EXPECT_EQ(capacity - bytes_free, bytes_consumed);

    ASSERT_TRUE(creator_.AddSavedFrame(frame));
    // BytesFree() returns bytes available for the next frame. Since stream
    // frame does not grow for FEC protected packets, this should be the same
    // as bytes_free (bound by 0).
    EXPECT_EQ(0u, creator_.ExpansionOnNewFrame());
    size_t expected_bytes_free = bytes_free > 0 ? bytes_free : 0;
    EXPECT_EQ(expected_bytes_free, creator_.BytesFree()) << "delta: " << delta;
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
    creator_.Flush();
    ASSERT_TRUE(serialized_packet_.packet);
    ClearSerializedPacket(&serialized_packet_);
  }
}

TEST_P(QuicPacketCreatorTest, CryptoStreamFramePacketPadding) {
  // Compute the total overhead for a single frame in packet.
  const size_t overhead = GetPacketHeaderOverhead(NOT_IN_FEC_GROUP)
      + GetEncryptionOverhead() + GetStreamFrameOverhead(NOT_IN_FEC_GROUP);
  ASSERT_GT(kMaxPacketSize, overhead);
  size_t capacity = kDefaultMaxPacketSize - overhead;
  // Now, test various sizes around this size.
  for (int delta = -5; delta <= 5; ++delta) {
    string data(capacity + delta, 'A');
    size_t bytes_free = delta > 0 ? 0 : 0 - delta;

    QuicFrame frame;
    QuicIOVector io_vector(MakeIOVector(data));
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillRepeatedly(
            Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
    ASSERT_TRUE(creator_.ConsumeData(kCryptoStreamId, io_vector, 0u, kOffset,
                                     false, true, &frame));
    ASSERT_TRUE(frame.stream_frame);
    size_t bytes_consumed = frame.stream_frame->data.length();
    EXPECT_LT(0u, bytes_consumed);
    creator_.Flush();
    ASSERT_TRUE(serialized_packet_.packet);
    // If there is not enough space in the packet to fit a padding frame
    // (1 byte) and to expand the stream frame (another 2 bytes) the packet
    // will not be padded.
    if (bytes_free < 3) {
      EXPECT_EQ(kDefaultMaxPacketSize - bytes_free,
                serialized_packet_.packet->length());
    } else {
      EXPECT_EQ(kDefaultMaxPacketSize, serialized_packet_.packet->length());
    }
    ClearSerializedPacket(&serialized_packet_);
  }
}

TEST_P(QuicPacketCreatorTest, NonCryptoStreamFramePacketNonPadding) {
  // Compute the total overhead for a single frame in packet.
  const size_t overhead = GetPacketHeaderOverhead(NOT_IN_FEC_GROUP)
      + GetEncryptionOverhead() + GetStreamFrameOverhead(NOT_IN_FEC_GROUP);
  ASSERT_GT(kDefaultMaxPacketSize, overhead);
  size_t capacity = kDefaultMaxPacketSize - overhead;
  // Now, test various sizes around this size.
  for (int delta = -5; delta <= 5; ++delta) {
    string data(capacity + delta, 'A');
    size_t bytes_free = delta > 0 ? 0 : 0 - delta;

    QuicFrame frame;
    QuicIOVector io_vector(MakeIOVector(data));
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
    ASSERT_TRUE(creator_.ConsumeData(kClientDataStreamId1, io_vector, 0u,
                                     kOffset, false, false, &frame));
    ASSERT_TRUE(frame.stream_frame);
    size_t bytes_consumed = frame.stream_frame->data.length();
    EXPECT_LT(0u, bytes_consumed);
    creator_.Flush();
    ASSERT_TRUE(serialized_packet_.packet);
    if (bytes_free > 0) {
      EXPECT_EQ(kDefaultMaxPacketSize - bytes_free,
                serialized_packet_.packet->length());
    } else {
      EXPECT_EQ(kDefaultMaxPacketSize, serialized_packet_.packet->length());
    }
    ClearSerializedPacket(&serialized_packet_);
  }
}

TEST_P(QuicPacketCreatorTest, SerializeVersionNegotiationPacket) {
  QuicFramerPeer::SetPerspective(&client_framer_, Perspective::IS_SERVER);
  QuicVersionVector versions;
  versions.push_back(test::QuicVersionMax());
  scoped_ptr<QuicEncryptedPacket> encrypted(
      creator_.SerializeVersionNegotiationPacket(versions));

  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnVersionNegotiationPacket(_));
  }
  QuicFramerPeer::SetPerspective(&client_framer_, Perspective::IS_CLIENT);
  client_framer_.ProcessPacket(*encrypted);
}

TEST_P(QuicPacketCreatorTest, UpdatePacketSequenceNumberLengthLeastAwaiting) {
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::NextPacketNumberLength(&creator_));

  size_t max_packets_per_fec_group = 10;
  creator_.set_max_packets_per_fec_group(max_packets_per_fec_group);
  QuicPacketCreatorPeer::SetPacketNumber(&creator_,
                                         64 - max_packets_per_fec_group);
  creator_.UpdatePacketNumberLength(2, 10000 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::NextPacketNumberLength(&creator_));

  QuicPacketCreatorPeer::SetPacketNumber(&creator_,
                                         64 * 256 - max_packets_per_fec_group);
  creator_.UpdatePacketNumberLength(2, 10000 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_2BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::NextPacketNumberLength(&creator_));

  QuicPacketCreatorPeer::SetPacketNumber(
      &creator_, 64 * 256 * 256 - max_packets_per_fec_group);
  creator_.UpdatePacketNumberLength(2, 10000 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::NextPacketNumberLength(&creator_));

  QuicPacketCreatorPeer::SetPacketNumber(
      &creator_,
      UINT64_C(64) * 256 * 256 * 256 * 256 - max_packets_per_fec_group);
  creator_.UpdatePacketNumberLength(2, 10000 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_6BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::NextPacketNumberLength(&creator_));
}

TEST_P(QuicPacketCreatorTest, UpdatePacketSequenceNumberLengthBandwidth) {
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::NextPacketNumberLength(&creator_));

  creator_.UpdatePacketNumberLength(1, 10000 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::NextPacketNumberLength(&creator_));

  creator_.UpdatePacketNumberLength(1, 10000 * 256 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_2BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::NextPacketNumberLength(&creator_));

  creator_.UpdatePacketNumberLength(1,
                                    10000 * 256 * 256 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::NextPacketNumberLength(&creator_));

  creator_.UpdatePacketNumberLength(
      1, UINT64_C(1000) * 256 * 256 * 256 * 256 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_6BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::NextPacketNumberLength(&creator_));
}

TEST_P(QuicPacketCreatorTest, SerializeFrame) {
  if (!GetParam().version_serialization) {
    creator_.StopSendingVersion();
  }
  frames_.push_back(
      QuicFrame(new QuicStreamFrame(0u, false, 0u, StringPiece())));
  char buffer[kMaxPacketSize];
  SerializedPacket serialized =
      creator_.SerializeAllFrames(frames_, buffer, kMaxPacketSize);
  delete frames_[0].stream_frame;

  QuicPacketHeader header;
  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_)).WillOnce(
        DoAll(SaveArg<0>(&header), Return(true)));
    EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized.packet);
  EXPECT_EQ(GetParam().version_serialization,
            header.public_header.version_flag);
  delete serialized.packet;
}

TEST_P(QuicPacketCreatorTest, ConsumeDataLargerThanOneStreamFrame) {
  if (!GetParam().version_serialization) {
    creator_.StopSendingVersion();
  }
  // A string larger than fits into a frame.
  size_t payload_length;
  creator_.SetMaxPacketLength(GetPacketLengthForOneStream(
      client_framer_.version(),
      QuicPacketCreatorPeer::SendVersionInPacket(&creator_),
      creator_.connection_id_length(), PACKET_1BYTE_PACKET_NUMBER,
      NOT_IN_FEC_GROUP, &payload_length));
  QuicFrame frame;
  const string too_long_payload(payload_length * 2, 'a');
  QuicIOVector io_vector(MakeIOVector(too_long_payload));
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  ASSERT_TRUE(creator_.ConsumeData(1u, io_vector, 0u, 0u, true, false, &frame));
  ASSERT_TRUE(frame.stream_frame);
  size_t consumed = frame.stream_frame->data.length();
  EXPECT_EQ(payload_length, consumed);
  const string payload(payload_length, 'a');
  CheckStreamFrame(frame, 1u, payload, 0u, false);
  creator_.Flush();
  ClearSerializedPacket(&serialized_packet_);
}

TEST_P(QuicPacketCreatorTest, AddFrameAndFlush) {
  if (!GetParam().version_serialization) {
    creator_.StopSendingVersion();
  }
  const size_t max_plaintext_size =
      client_framer_.GetMaxPlaintextSize(creator_.max_packet_length());
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_EQ(max_plaintext_size -
                GetPacketHeaderSize(
                    creator_.connection_id_length(),
                    QuicPacketCreatorPeer::SendVersionInPacket(&creator_),
                    /*include_path_id=*/false, PACKET_1BYTE_PACKET_NUMBER,
                    NOT_IN_FEC_GROUP),
            creator_.BytesFree());

  // Add a variety of frame types and then a padding frame.
  QuicAckFrame ack_frame(MakeAckFrame(0u));
  EXPECT_TRUE(creator_.AddSavedFrame(QuicFrame(&ack_frame)));
  EXPECT_TRUE(creator_.HasPendingFrames());

  QuicFrame frame;
  QuicIOVector io_vector(MakeIOVector("test"));
  ASSERT_TRUE(
      creator_.ConsumeData(1u, io_vector, 0u, 0u, false, false, &frame));
  ASSERT_TRUE(frame.stream_frame);
  size_t consumed = frame.stream_frame->data.length();
  EXPECT_EQ(4u, consumed);
  EXPECT_TRUE(creator_.HasPendingFrames());

  QuicPaddingFrame padding_frame;
  EXPECT_TRUE(creator_.AddSavedFrame(QuicFrame(padding_frame)));
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_EQ(0u, creator_.BytesFree());

  // Packet is full. Creator will flush.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  EXPECT_FALSE(creator_.AddSavedFrame(QuicFrame(&ack_frame)));

  // Ensure the packet is successfully created.
  ASSERT_TRUE(serialized_packet_.packet);
  ASSERT_TRUE(serialized_packet_.retransmittable_frames);
  RetransmittableFrames* retransmittable =
      serialized_packet_.retransmittable_frames;
  ASSERT_EQ(1u, retransmittable->frames().size());
  EXPECT_EQ(STREAM_FRAME, retransmittable->frames()[0].type);
  ASSERT_TRUE(retransmittable->frames()[0].stream_frame);
  ClearSerializedPacket(&serialized_packet_);

  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_EQ(max_plaintext_size -
                GetPacketHeaderSize(
                    creator_.connection_id_length(),
                    QuicPacketCreatorPeer::SendVersionInPacket(&creator_),
                    /*include_path_id=*/false, PACKET_1BYTE_PACKET_NUMBER,
                    NOT_IN_FEC_GROUP),
            creator_.BytesFree());
}

TEST_P(QuicPacketCreatorTest, SerializeTruncatedAckFrameWithLargePacketSize) {
  if (!GetParam().version_serialization) {
    creator_.StopSendingVersion();
  }
  creator_.SetMaxPacketLength(kMaxPacketSize);

  // Serialized length of ack frame with 2000 nack ranges should be limited by
  // the number of nack ranges that can be fit in an ack frame.
  QuicAckFrame ack_frame = MakeAckFrameWithNackRanges(2000u, 0u);
  size_t frame_len = client_framer_.GetSerializedFrameLength(
      QuicFrame(&ack_frame), creator_.BytesFree(), true, true, NOT_IN_FEC_GROUP,
      PACKET_1BYTE_PACKET_NUMBER);
  EXPECT_GT(creator_.BytesFree(), frame_len);
  EXPECT_GT(creator_.max_packet_length(), creator_.PacketSize());

  // Add ack frame to creator.
  EXPECT_TRUE(creator_.AddSavedFrame(QuicFrame(&ack_frame)));
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_GT(creator_.max_packet_length(), creator_.PacketSize());
  EXPECT_LT(0u, creator_.BytesFree());

  // Make sure that an additional stream frame can be added to the packet.
  QuicFrame frame;
  QuicIOVector io_vector(MakeIOVector("test"));
  ASSERT_TRUE(
      creator_.ConsumeData(2u, io_vector, 0u, 0u, false, false, &frame));
  ASSERT_TRUE(frame.stream_frame);
  size_t consumed = frame.stream_frame->data.length();
  EXPECT_EQ(4u, consumed);
  EXPECT_TRUE(creator_.HasPendingFrames());

  // Ensure the packet is successfully created, and the packet size estimate
  // matches the serialized packet length.
  EXPECT_CALL(entropy_calculator_, EntropyHash(_)).WillOnce(testing::Return(0));
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  size_t est_packet_size = creator_.PacketSize();
  creator_.Flush();
  ASSERT_TRUE(serialized_packet_.packet);
  EXPECT_EQ(est_packet_size, client_framer_.GetMaxPlaintextSize(
                                 serialized_packet_.packet->length()));
  ClearSerializedPacket(&serialized_packet_);
}

TEST_P(QuicPacketCreatorTest, SerializeTruncatedAckFrameWithSmallPacketSize) {
  if (!GetParam().version_serialization) {
    creator_.StopSendingVersion();
  }
  creator_.SetMaxPacketLength(500u);

  const size_t max_plaintext_size =
      client_framer_.GetMaxPlaintextSize(creator_.max_packet_length());
  EXPECT_EQ(max_plaintext_size - creator_.PacketSize(), creator_.BytesFree());

  // Serialized length of ack frame with 2000 nack ranges should be limited by
  // the packet size.
  QuicAckFrame ack_frame = MakeAckFrameWithNackRanges(2000u, 0u);
  size_t frame_len = client_framer_.GetSerializedFrameLength(
      QuicFrame(&ack_frame), creator_.BytesFree(), true, true, NOT_IN_FEC_GROUP,
      PACKET_1BYTE_PACKET_NUMBER);
  EXPECT_EQ(creator_.BytesFree(), frame_len);

  // Add ack frame to creator.
  EXPECT_TRUE(creator_.AddSavedFrame(QuicFrame(&ack_frame)));
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_EQ(client_framer_.GetMaxPlaintextSize(creator_.max_packet_length()),
            creator_.PacketSize());
  EXPECT_EQ(0u, creator_.BytesFree());

  // Ensure the packet is successfully created, and the packet size estimate
  // may not match the serialized packet length.
  EXPECT_CALL(entropy_calculator_, EntropyHash(_)).WillOnce(Return(0));
  size_t est_packet_size = creator_.PacketSize();
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.Flush();
  ASSERT_TRUE(serialized_packet_.packet);
  EXPECT_GE(est_packet_size, client_framer_.GetMaxPlaintextSize(
                                 serialized_packet_.packet->length()));
  ClearSerializedPacket(&serialized_packet_);
}


TEST_P(QuicPacketCreatorTest, EntropyFlag) {
  frames_.push_back(
      QuicFrame(new QuicStreamFrame(0u, false, 0u, StringPiece())));

  char buffer[kMaxPacketSize];
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 64; ++j) {
      SerializedPacket serialized =
          creator_.SerializeAllFrames(frames_, buffer, kMaxPacketSize);
      // Verify both BoolSource and hash algorithm.
      bool expected_rand_bool =
          (mock_random_.RandUint64() & (UINT64_C(1) << j)) != 0;
      bool observed_rand_bool =
          (serialized.entropy_hash & (1 << ((j+1) % 8))) != 0;
      uint8 rest_of_hash = serialized.entropy_hash & ~(1 << ((j+1) % 8));
      EXPECT_EQ(expected_rand_bool, observed_rand_bool);
      EXPECT_EQ(0, rest_of_hash);
      delete serialized.packet;
    }
    // After 64 calls, BoolSource will refresh the bucket - make sure it does.
    mock_random_.ChangeValue();
  }

  delete frames_[0].stream_frame;
}

TEST_P(QuicPacketCreatorTest, ResetFecGroup) {
  // Enable FEC protection, and send FEC packet every 6 packets.
  EXPECT_TRUE(SwitchFecProtectionOn(6));
  frames_.push_back(
      QuicFrame(new QuicStreamFrame(0u, false, 0u, StringPiece())));
  char buffer[kMaxPacketSize];
  SerializedPacket serialized =
      creator_.SerializeAllFrames(frames_, buffer, kMaxPacketSize);
  delete serialized.packet;

  EXPECT_TRUE(QuicPacketCreatorPeer::IsFecProtected(&creator_));
  EXPECT_TRUE(creator_.IsFecGroupOpen());
  // We do not have enough packets in the FEC group to trigger an FEC packet.
  EXPECT_FALSE(creator_.ShouldSendFec(/*force_close=*/false));
  // Should return true since there are packets in the FEC group.
  EXPECT_TRUE(creator_.ShouldSendFec(/*force_close=*/true));

  // FEC group will be reset if FEC police is alarm trigger but FEC alarm does
  // not fire.
  EXPECT_CALL(delegate_, OnResetFecGroup()).Times(1);
  creator_.set_fec_send_policy(FEC_ALARM_TRIGGER);
  creator_.set_should_fec_protect(true);
  creator_.MaybeSendFecPacketAndCloseGroup(/*force_send_fec=*/true,
                                           /*is_fec_timeout=*/false);
  EXPECT_TRUE(QuicPacketCreatorPeer::IsFecProtected(&creator_));
  EXPECT_FALSE(creator_.IsFecGroupOpen());
  // We do not have enough packets in the FEC group to trigger an FEC packet.
  EXPECT_FALSE(creator_.ShouldSendFec(/*force_close=*/false));
  // Confirm that there is no FEC packet under construction.
  EXPECT_FALSE(creator_.ShouldSendFec(/*force_close=*/true));

  EXPECT_DFATAL(serialized = QuicPacketCreatorPeer::SerializeFec(
                    &creator_, buffer, kMaxPacketSize),
                "SerializeFEC called but no group or zero packets in group.");
  delete serialized.packet;

  // Start a new FEC packet.
  serialized = creator_.SerializeAllFrames(frames_, buffer, kMaxPacketSize);
  delete frames_[0].stream_frame;
  delete serialized.packet;

  EXPECT_TRUE(QuicPacketCreatorPeer::IsFecProtected(&creator_));
  EXPECT_TRUE(creator_.IsFecGroupOpen());
  // We do not have enough packets in the FEC group to trigger an FEC packet.
  EXPECT_FALSE(creator_.ShouldSendFec(/*force_close=*/false));
  // Should return true since there are packets in the FEC group.
  EXPECT_TRUE(creator_.ShouldSendFec(/*force_close=*/true));

  // Should return false since we do not have enough packets in the FEC group to
  // trigger an FEC packet.
  ASSERT_FALSE(creator_.ShouldSendFec(/*force_close=*/false));
  // Should return true since there are packets in the FEC group.
  ASSERT_TRUE(creator_.ShouldSendFec(/*force_close=*/true));

  // Change FEC policy, send FEC packet and close FEC group.
  creator_.set_fec_send_policy(FEC_ANY_TRIGGER);
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.MaybeSendFecPacketAndCloseGroup(/*force_send_fec=*/true,
                                           /*is_fec_timeout=*/false);
  ASSERT_EQ(3u, serialized_packet_.packet_number);
  ClearSerializedPacket(&serialized_packet_);
}

TEST_P(QuicPacketCreatorTest, ResetFecGroupWithQueuedFrames) {
  // Enable FEC protection, and send FEC packet every 6 packets.
  EXPECT_TRUE(SwitchFecProtectionOn(6));
  // Add a stream frame to the creator.
  QuicFrame frame;
  QuicIOVector io_vector(MakeIOVector("test"));
  ASSERT_TRUE(
      creator_.ConsumeData(1u, io_vector, 0u, 0u, false, false, &frame));
  ASSERT_TRUE(frame.stream_frame);
  size_t consumed = frame.stream_frame->data.length();
  EXPECT_EQ(4u, consumed);
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_DFATAL(QuicPacketCreatorPeer::ResetFecGroup(&creator_),
                "Cannot reset FEC group with pending frames.");

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::ClearSerializedPacket));
  creator_.Flush();
  EXPECT_FALSE(creator_.HasPendingFrames());

  // FEC group will be reset if FEC police is alarm trigger but FEC alarm does
  // not fire.
  EXPECT_CALL(delegate_, OnResetFecGroup()).Times(1);
  creator_.set_fec_send_policy(FEC_ALARM_TRIGGER);
  creator_.set_should_fec_protect(true);
  creator_.MaybeSendFecPacketAndCloseGroup(/*force_send_fec=*/true,
                                           /*is_fec_timeout=*/false);
  EXPECT_FALSE(creator_.IsFecGroupOpen());
}

}  // namespace
}  // namespace test
}  // namespace net
