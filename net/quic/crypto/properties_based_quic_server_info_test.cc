// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/crypto/properties_based_quic_server_info.h"

#include <string>

#include "net/base/net_errors.h"
#include "net/http/http_server_properties_impl.h"
#include "net/quic/crypto/quic_server_info.h"
#include "net/quic/quic_server_id.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace test {

namespace {
const std::string kServerConfigA("server_config_a");
const std::string kSourceAddressTokenA("source_address_token_a");
const std::string kServerConfigSigA("server_config_sig_a");
const std::string kCertA("cert_a");
const std::string kCertB("cert_b");
}  // namespace

class PropertiesBasedQuicServerInfoTest : public ::testing::Test {
 protected:
  PropertiesBasedQuicServerInfoTest()
      : server_id_("www.google.com", 443, PRIVACY_MODE_DISABLED),
        server_info_(server_id_, http_server_properties_.GetWeakPtr()) {}

  // Initialize |server_info_| object and persist it.
  void InitializeAndPersist() {
    server_info_.Start();
    EXPECT_TRUE(server_info_.IsDataReady());
    QuicServerInfo::State* state = server_info_.mutable_state();
    EXPECT_TRUE(state->certs.empty());

    state->server_config = kServerConfigA;
    state->source_address_token = kSourceAddressTokenA;
    state->server_config_sig = kServerConfigSigA;
    state->certs.push_back(kCertA);
    EXPECT_TRUE(server_info_.IsReadyToPersist());
    server_info_.Persist();
    EXPECT_TRUE(server_info_.IsReadyToPersist());
    EXPECT_TRUE(server_info_.IsDataReady());
    server_info_.OnExternalCacheHit();
  }

  // Verify the data that is persisted in InitializeAndPersist().
  void VerifyInitialData(const QuicServerInfo::State& state) {
    EXPECT_EQ(kServerConfigA, state.server_config);
    EXPECT_EQ(kSourceAddressTokenA, state.source_address_token);
    EXPECT_EQ(kServerConfigSigA, state.server_config_sig);
    EXPECT_EQ(kCertA, state.certs[0]);
  }

  HttpServerPropertiesImpl http_server_properties_;
  QuicServerId server_id_;
  PropertiesBasedQuicServerInfo server_info_;
  CompletionCallback callback_;
};

// Test persisting, reading and verifying and then updating and verifing.
TEST_F(PropertiesBasedQuicServerInfoTest, Update) {
  InitializeAndPersist();

  // Read the persisted data and verify we have read the data correctly.
  PropertiesBasedQuicServerInfo server_info1(
      server_id_, http_server_properties_.GetWeakPtr());
  server_info1.Start();
  EXPECT_EQ(OK, server_info1.WaitForDataReady(callback_));  // Read the data.
  EXPECT_TRUE(server_info1.IsDataReady());

  // Verify the data.
  const QuicServerInfo::State& state1 = server_info1.state();
  EXPECT_EQ(1U, state1.certs.size());
  VerifyInitialData(state1);

  // Update the data, by adding another cert.
  QuicServerInfo::State* state2 = server_info1.mutable_state();
  state2->certs.push_back(kCertB);
  EXPECT_TRUE(server_info_.IsReadyToPersist());
  server_info1.Persist();

  // Read the persisted data and verify we have read the data correctly.
  PropertiesBasedQuicServerInfo server_info2(
      server_id_, http_server_properties_.GetWeakPtr());
  server_info2.Start();
  EXPECT_EQ(OK, server_info2.WaitForDataReady(callback_));  // Read the data.
  EXPECT_TRUE(server_info1.IsDataReady());

  // Verify updated data.
  const QuicServerInfo::State& state3 = server_info2.state();
  VerifyInitialData(state3);
  EXPECT_EQ(2U, state3.certs.size());
  EXPECT_EQ(kCertB, state3.certs[1]);
}

}  // namespace test
}  // namespace net
