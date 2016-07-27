// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "MockWebIDBDatabase.h"

namespace blink {

MockWebIDBDatabase::MockWebIDBDatabase() {}

MockWebIDBDatabase::~MockWebIDBDatabase() {}

PassOwnPtr<MockWebIDBDatabase> MockWebIDBDatabase::create()
{
    return adoptPtr(new MockWebIDBDatabase());
}

} // namespace blink
