// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_EXTENSIONS_EXTENSION_TEST_UTIL_H_
#define CHROME_COMMON_EXTENSIONS_EXTENSION_TEST_UTIL_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "chrome/common/extensions/extension_builder.h"
#include "chrome/common/extensions/manifest.h"

namespace extensions {
class Extension;

// Newer functions go here.
// TODO(mpcomplete): migrate older functions over.
namespace extension_test_util {

ExtensionBuilder& BuildExtension(ExtensionBuilder& builder);
ExtensionBuilder& BuildExtensionWithPermissions(ExtensionBuilder& builder,
                                                ListBuilder& permissions);

}  // namespace extension_test_util
}  // namespace extensions

namespace extension_test_util {

// Return a very simple extension with id |id|.
scoped_refptr<extensions::Extension> CreateExtensionWithID(std::string id);

// Helpers for loading manifests, |dir| is relative to chrome::DIR_TEST_DATA
// followed by "extensions".
scoped_refptr<extensions::Extension> LoadManifestUnchecked(
    const std::string& dir,
    const std::string& test_file,
    extensions::Manifest::Location location,
    int extra_flags,
    const std::string& id,
    std::string* error);

scoped_refptr<extensions::Extension> LoadManifestUnchecked(
    const std::string& dir,
    const std::string& test_file,
    extensions::Manifest::Location location,
    int extra_flags,
    std::string* error);

scoped_refptr<extensions::Extension> LoadManifest(
    const std::string& dir,
    const std::string& test_file,
    extensions::Manifest::Location location,
    int extra_flags);

scoped_refptr<extensions::Extension> LoadManifest(const std::string& dir,
                                                  const std::string& test_file,
                                                  int extra_flags);

scoped_refptr<extensions::Extension> LoadManifestStrict(
    const std::string& dir,
    const std::string& test_file);

scoped_refptr<extensions::Extension> LoadManifest(const std::string& dir,
                                                  const std::string& test_file);

}  // namespace extension_test_util

#endif  // CHROME_COMMON_EXTENSIONS_EXTENSION_TEST_UTIL_H_
