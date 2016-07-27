// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/shell/shell_application_loader.h"

#include "mojo/application/public/cpp/application_impl.h"
#include "mojo/shell/shell_application_delegate.h"

namespace mojo {
namespace shell {

ShellApplicationLoader::ShellApplicationLoader(ApplicationManager* manager)
    : manager_(manager) {}
ShellApplicationLoader::~ShellApplicationLoader() {}

void ShellApplicationLoader::Load(
    const GURL& url,
    InterfaceRequest<Application> application_request) {
  DCHECK(application_request.is_pending());
  app_.reset(new ApplicationImpl(new ShellApplicationDelegate(manager_),
                                 application_request.Pass()));
}

}  // namespace shell
}  // namespace mojo
