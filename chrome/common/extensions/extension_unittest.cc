// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_util.h"
#include "base/format_macros.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/extensions/command.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_file_util.h"
#include "chrome/common/extensions/extension_test_util.h"
#include "chrome/common/extensions/manifest.h"
#include "chrome/common/extensions/manifest_handlers/content_scripts_handler.h"
#include "chrome/common/extensions/permissions/permissions_data.h"
#include "chrome/common/url_constants.h"
#include "extensions/common/extension_resource.h"
#include "net/base/mime_sniffer.h"
#include "net/dns/mock_host_resolver.h"
#include "skia/ext/image_operations.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"
#include "url/gurl.h"

using extension_test_util::LoadManifest;
using extension_test_util::LoadManifestStrict;

namespace extensions {

// We persist location values in the preferences, so this is a sanity test that
// someone doesn't accidentally change them.
TEST(ExtensionTest, LocationValuesTest) {
  ASSERT_EQ(0, Manifest::INVALID_LOCATION);
  ASSERT_EQ(1, Manifest::INTERNAL);
  ASSERT_EQ(2, Manifest::EXTERNAL_PREF);
  ASSERT_EQ(3, Manifest::EXTERNAL_REGISTRY);
  ASSERT_EQ(4, Manifest::UNPACKED);
  ASSERT_EQ(5, Manifest::COMPONENT);
  ASSERT_EQ(6, Manifest::EXTERNAL_PREF_DOWNLOAD);
  ASSERT_EQ(7, Manifest::EXTERNAL_POLICY_DOWNLOAD);
  ASSERT_EQ(8, Manifest::COMMAND_LINE);
}

TEST(ExtensionTest, LocationPriorityTest) {
  for (int i = 0; i < Manifest::NUM_LOCATIONS; i++) {
    Manifest::Location loc = static_cast<Manifest::Location>(i);

    // INVALID is not a valid location.
    if (loc == Manifest::INVALID_LOCATION)
      continue;

    // Comparing a location that has no rank will hit a CHECK. Do a
    // compare with every valid location, to be sure each one is covered.

    // Check that no install source can override a componenet extension.
    ASSERT_EQ(Manifest::COMPONENT,
              Manifest::GetHigherPriorityLocation(Manifest::COMPONENT, loc));
    ASSERT_EQ(Manifest::COMPONENT,
              Manifest::GetHigherPriorityLocation(loc, Manifest::COMPONENT));

    // Check that any source can override a user install. This might change
    // in the future, in which case this test should be updated.
    ASSERT_EQ(loc,
              Manifest::GetHigherPriorityLocation(Manifest::INTERNAL, loc));
    ASSERT_EQ(loc,
              Manifest::GetHigherPriorityLocation(loc, Manifest::INTERNAL));
  }

  // Check a few interesting cases that we know can happen:
  ASSERT_EQ(Manifest::EXTERNAL_POLICY_DOWNLOAD,
            Manifest::GetHigherPriorityLocation(
                Manifest::EXTERNAL_POLICY_DOWNLOAD,
                Manifest::EXTERNAL_PREF));

  ASSERT_EQ(Manifest::EXTERNAL_PREF,
            Manifest::GetHigherPriorityLocation(
                Manifest::INTERNAL,
                Manifest::EXTERNAL_PREF));
}

TEST(ExtensionTest, GetResourceURLAndPath) {
  scoped_refptr<Extension> extension = LoadManifestStrict("empty_manifest",
      "empty.json");
  EXPECT_TRUE(extension.get());

  EXPECT_EQ(extension->url().spec() + "bar/baz.js",
            Extension::GetResourceURL(extension->url(), "bar/baz.js").spec());
  EXPECT_EQ(extension->url().spec() + "baz.js",
            Extension::GetResourceURL(extension->url(),
                                      "bar/../baz.js").spec());
  EXPECT_EQ(extension->url().spec() + "baz.js",
            Extension::GetResourceURL(extension->url(), "../baz.js").spec());

  // Test that absolute-looking paths ("/"-prefixed) are pasted correctly.
  EXPECT_EQ(extension->url().spec() + "test.html",
            extension->GetResourceURL("/test.html").spec());
}

TEST(ExtensionTest, GetAbsolutePathNoError) {
  scoped_refptr<Extension> extension = LoadManifestStrict("absolute_path",
      "absolute.json");
  EXPECT_TRUE(extension.get());
  std::string err;
  std::vector<InstallWarning> warnings;
  EXPECT_TRUE(extension_file_util::ValidateExtension(extension.get(),
                                                     &err, &warnings));
  EXPECT_EQ(0U, warnings.size());

  EXPECT_EQ(extension->path().AppendASCII("test.html").value(),
            extension->GetResource("test.html").GetFilePath().value());
  EXPECT_EQ(extension->path().AppendASCII("test.js").value(),
            extension->GetResource("test.js").GetFilePath().value());
}


TEST(ExtensionTest, IdIsValid) {
  EXPECT_TRUE(Extension::IdIsValid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  EXPECT_TRUE(Extension::IdIsValid("pppppppppppppppppppppppppppppppp"));
  EXPECT_TRUE(Extension::IdIsValid("abcdefghijklmnopabcdefghijklmnop"));
  EXPECT_TRUE(Extension::IdIsValid("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"));
  EXPECT_FALSE(Extension::IdIsValid("abcdefghijklmnopabcdefghijklmno"));
  EXPECT_FALSE(Extension::IdIsValid("abcdefghijklmnopabcdefghijklmnopa"));
  EXPECT_FALSE(Extension::IdIsValid("0123456789abcdef0123456789abcdef"));
  EXPECT_FALSE(Extension::IdIsValid("abcdefghijklmnopabcdefghijklmnoq"));
  EXPECT_FALSE(Extension::IdIsValid("abcdefghijklmnopabcdefghijklmno0"));
}


// This test ensures that the mimetype sniffing code stays in sync with the
// actual crx files that we test other parts of the system with.
TEST(ExtensionTest, MimeTypeSniffing) {
  base::FilePath path;
  ASSERT_TRUE(PathService::Get(chrome::DIR_TEST_DATA, &path));
  path = path.AppendASCII("extensions").AppendASCII("good.crx");

  std::string data;
  ASSERT_TRUE(file_util::ReadFileToString(path, &data));

  std::string result;
  EXPECT_TRUE(net::SniffMimeType(data.c_str(),
                                 data.size(),
                                 GURL("http://www.example.com/foo.crx"),
                                 std::string(),
                                 &result));
  EXPECT_EQ(std::string(Extension::kMimeType), result);

  data.clear();
  result.clear();
  path = path.DirName().AppendASCII("bad_magic.crx");
  ASSERT_TRUE(file_util::ReadFileToString(path, &data));
  EXPECT_TRUE(net::SniffMimeType(data.c_str(),
                                 data.size(),
                                 GURL("http://www.example.com/foo.crx"),
                                 std::string(),
                                 &result));
  EXPECT_EQ("application/octet-stream", result);
}

TEST(ExtensionTest, WantsFileAccess) {
  scoped_refptr<Extension> extension;
  GURL file_url("file:///etc/passwd");

  // Ignore the policy delegate for this test.
  PermissionsData::SetPolicyDelegate(NULL);

  // <all_urls> permission
  extension = LoadManifest("permissions", "permissions_all_urls.json");
  EXPECT_TRUE(extension->wants_file_access());
  EXPECT_FALSE(PermissionsData::CanExecuteScriptOnPage(
      extension.get(), file_url, file_url, -1, NULL, -1, NULL));
  extension = LoadManifest(
      "permissions", "permissions_all_urls.json", Extension::ALLOW_FILE_ACCESS);
  EXPECT_TRUE(extension->wants_file_access());
  EXPECT_TRUE(PermissionsData::CanExecuteScriptOnPage(
      extension.get(), file_url, file_url, -1, NULL, -1, NULL));

  // file:///* permission
  extension = LoadManifest("permissions", "permissions_file_scheme.json");
  EXPECT_TRUE(extension->wants_file_access());
  EXPECT_FALSE(PermissionsData::CanExecuteScriptOnPage(
      extension.get(), file_url, file_url, -1, NULL, -1, NULL));
  extension = LoadManifest("permissions",
                           "permissions_file_scheme.json",
                           Extension::ALLOW_FILE_ACCESS);
  EXPECT_TRUE(extension->wants_file_access());
  EXPECT_TRUE(PermissionsData::CanExecuteScriptOnPage(
      extension.get(), file_url, file_url, -1, NULL, -1, NULL));

  // http://* permission
  extension = LoadManifest("permissions", "permissions_http_scheme.json");
  EXPECT_FALSE(extension->wants_file_access());
  EXPECT_FALSE(PermissionsData::CanExecuteScriptOnPage(
      extension.get(), file_url, file_url, -1, NULL, -1, NULL));
  extension = LoadManifest("permissions",
                           "permissions_http_scheme.json",
                           Extension::ALLOW_FILE_ACCESS);
  EXPECT_FALSE(extension->wants_file_access());
  EXPECT_FALSE(PermissionsData::CanExecuteScriptOnPage(
      extension.get(), file_url, file_url, -1, NULL, -1, NULL));

  // <all_urls> content script match
  extension = LoadManifest("permissions", "content_script_all_urls.json");
  EXPECT_TRUE(extension->wants_file_access());
  EXPECT_FALSE(PermissionsData::CanExecuteScriptOnPage(
      extension.get(),
      file_url,
      file_url,
      -1,
      &ContentScriptsInfo::GetContentScripts(extension.get())[0],
      -1,
      NULL));
  extension = LoadManifest("permissions", "content_script_all_urls.json",
      Extension::ALLOW_FILE_ACCESS);
  EXPECT_TRUE(extension->wants_file_access());
  EXPECT_TRUE(PermissionsData::CanExecuteScriptOnPage(
      extension.get(),
      file_url,
      file_url,
      -1,
      &ContentScriptsInfo::GetContentScripts(extension.get())[0],
      -1,
      NULL));

  // file:///* content script match
  extension = LoadManifest("permissions", "content_script_file_scheme.json");
  EXPECT_TRUE(extension->wants_file_access());
  EXPECT_FALSE(PermissionsData::CanExecuteScriptOnPage(
      extension.get(),
      file_url,
      file_url,
      -1,
      &ContentScriptsInfo::GetContentScripts(extension.get())[0],
      -1,
      NULL));
  extension = LoadManifest("permissions", "content_script_file_scheme.json",
      Extension::ALLOW_FILE_ACCESS);
  EXPECT_TRUE(extension->wants_file_access());
  EXPECT_TRUE(PermissionsData::CanExecuteScriptOnPage(
      extension.get(),
      file_url,
      file_url,
      -1,
      &ContentScriptsInfo::GetContentScripts(extension.get())[0],
      -1,
      NULL));

  // http://* content script match
  extension = LoadManifest("permissions", "content_script_http_scheme.json");
  EXPECT_FALSE(extension->wants_file_access());
  EXPECT_FALSE(PermissionsData::CanExecuteScriptOnPage(
      extension.get(),
      file_url,
      file_url,
      -1,
      &ContentScriptsInfo::GetContentScripts(extension.get())[0],
      -1,
      NULL));
  extension = LoadManifest("permissions", "content_script_http_scheme.json",
      Extension::ALLOW_FILE_ACCESS);
  EXPECT_FALSE(extension->wants_file_access());
  EXPECT_FALSE(PermissionsData::CanExecuteScriptOnPage(
      extension.get(),
      file_url,
      file_url,
      -1,
      &ContentScriptsInfo::GetContentScripts(extension.get())[0],
      -1,
      NULL));
}

TEST(ExtensionTest, ExtraFlags) {
  scoped_refptr<Extension> extension;
  extension = LoadManifest("app", "manifest.json", Extension::FROM_WEBSTORE);
  EXPECT_TRUE(extension->from_webstore());

  extension = LoadManifest("app", "manifest.json", Extension::FROM_BOOKMARK);
  EXPECT_TRUE(extension->from_bookmark());

  extension = LoadManifest("app", "manifest.json", Extension::NO_FLAGS);
  EXPECT_FALSE(extension->from_bookmark());
  EXPECT_FALSE(extension->from_webstore());
}

}  // namespace extensions
