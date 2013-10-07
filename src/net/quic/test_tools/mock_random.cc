// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/mock_random.h"

namespace net {

MockRandom::MockRandom()
    : increment_(0) {
}

void MockRandom::RandBytes(void* data, size_t len) {
  memset(data, 'r' + increment_, len);
}

uint64 MockRandom::RandUint64() {
  return 0xDEADBEEF + increment_;
}

bool MockRandom::RandBool() {
  return false;
}

void MockRandom::Reseed(const void* additional_entropy, size_t entropy_len) {
}

void MockRandom::ChangeValue() {
  increment_++;
}

}  // namespace net
