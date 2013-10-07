// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/fake_system_clock_client.h"

namespace chromeos {

FakeSystemClockClient::FakeSystemClockClient() {
}

FakeSystemClockClient::~FakeSystemClockClient() {
}

void FakeSystemClockClient::AddObserver(Observer* observer) {
}

void FakeSystemClockClient::RemoveObserver(Observer* observer) {
}

bool FakeSystemClockClient::HasObserver(Observer* observer) {
  return false;
}

} // namespace chromeos
