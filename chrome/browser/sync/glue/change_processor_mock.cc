// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/change_processor_mock.h"

#include "base/compiler_specific.h"

namespace browser_sync {

ChangeProcessorMock::ChangeProcessorMock()
    : ChangeProcessor(this) {}

ChangeProcessorMock::~ChangeProcessorMock() {}

}  // namespace browser_sync
