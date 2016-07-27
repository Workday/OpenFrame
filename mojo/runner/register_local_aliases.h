// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_RUNNER_REGISTER_LOCAL_ALIASES_H_
#define MOJO_RUNNER_REGISTER_LOCAL_ALIASES_H_

namespace mojo {
namespace package_manager {
class PackageManagerImpl;
}

namespace runner {

void RegisterLocalAliases(mojo::package_manager::PackageManagerImpl* manager);

}  // namespace runner
}  // namespace mojo

#endif  // MOJO_RUNNER_REGISTER_LOCAL_ALIASES_H_
