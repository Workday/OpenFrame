// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/api_resource.h"

namespace extensions {

ApiResource::ApiResource(const std::string& owner_extension_id)
    : owner_extension_id_(owner_extension_id) {

  CHECK(!owner_extension_id_.empty());
}

ApiResource::~ApiResource() {
}

}  // namespace extensions
