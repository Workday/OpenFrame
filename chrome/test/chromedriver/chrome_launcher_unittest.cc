// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/base64.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/values.h"
#include "chrome/test/chromedriver/chrome/status.h"
#include "chrome/test/chromedriver/chrome_launcher.h"
#include "testing/gtest/include/gtest/gtest.h"

TEST(ProcessExtensions, NoExtension) {
  CommandLine command(CommandLine::NO_PROGRAM);
  std::vector<std::string> extensions;
  base::FilePath extension_dir;
  Status status = internal::ProcessExtensions(extensions, extension_dir,
                                              false, &command);
  ASSERT_TRUE(status.IsOk());
  ASSERT_FALSE(command.HasSwitch("load-extension"));
}

TEST(ProcessExtensions, SingleExtension) {
  base::FilePath source_root;
  PathService::Get(base::DIR_SOURCE_ROOT, &source_root);
  base::FilePath crx_file_path = source_root.AppendASCII(
      "chrome/test/data/chromedriver/ext_test_1.crx");
  std::string crx_contents;
  ASSERT_TRUE(file_util::ReadFileToString(crx_file_path, &crx_contents));

  std::vector<std::string> extensions;
  std::string crx_encoded;
  ASSERT_TRUE(base::Base64Encode(crx_contents, &crx_encoded));
  extensions.push_back(crx_encoded);

  base::ScopedTempDir extension_dir;
  ASSERT_TRUE(extension_dir.CreateUniqueTempDir());

  CommandLine command(CommandLine::NO_PROGRAM);
  Status status = internal::ProcessExtensions(extensions, extension_dir.path(),
                                              false, &command);
  ASSERT_TRUE(status.IsOk());
  ASSERT_TRUE(command.HasSwitch("load-extension"));
  base::FilePath temp_ext_path = command.GetSwitchValuePath("load-extension");
  ASSERT_TRUE(base::PathExists(temp_ext_path));
}

TEST(ProcessExtensions, MultipleExtensions) {
  base::FilePath source_root;
  PathService::Get(base::DIR_SOURCE_ROOT, &source_root);
  base::FilePath test_ext_path = source_root.AppendASCII(
      "chrome/test/data/chromedriver");
  base::FilePath test_crx_1 = test_ext_path.AppendASCII("ext_test_1.crx");
  base::FilePath test_crx_2 = test_ext_path.AppendASCII("ext_test_2.crx");

  std::string crx_1_contents, crx_2_contents;
  ASSERT_TRUE(file_util::ReadFileToString(test_crx_1, &crx_1_contents));
  ASSERT_TRUE(file_util::ReadFileToString(test_crx_2, &crx_2_contents));

  std::vector<std::string> extensions;
  std::string crx_1_encoded, crx_2_encoded;
  ASSERT_TRUE(base::Base64Encode(crx_1_contents, &crx_1_encoded));
  ASSERT_TRUE(base::Base64Encode(crx_2_contents, &crx_2_encoded));
  extensions.push_back(crx_1_encoded);
  extensions.push_back(crx_2_encoded);

  base::ScopedTempDir extension_dir;
  ASSERT_TRUE(extension_dir.CreateUniqueTempDir());

  CommandLine command(CommandLine::NO_PROGRAM);
  Status status = internal::ProcessExtensions(extensions, extension_dir.path(),
                                              false, &command);
  ASSERT_TRUE(status.IsOk());
  ASSERT_TRUE(command.HasSwitch("load-extension"));
  CommandLine::StringType ext_paths = command.GetSwitchValueNative(
      "load-extension");
  std::vector<CommandLine::StringType> ext_path_list;
  base::SplitString(ext_paths, FILE_PATH_LITERAL(','), &ext_path_list);
  ASSERT_EQ(2u, ext_path_list.size());
  ASSERT_TRUE(base::PathExists(base::FilePath(ext_path_list[0])));
  ASSERT_TRUE(base::PathExists(base::FilePath(ext_path_list[1])));
}

namespace {

void AssertEQ(const base::DictionaryValue& dict, const std::string& key,
              const char* expected_value) {
  std::string value;
  ASSERT_TRUE(dict.GetString(key, &value));
  ASSERT_STREQ(value.c_str(), expected_value);
}

}  // namespace

TEST(PrepareUserDataDir, CustomPrefs) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  CommandLine command(CommandLine::NO_PROGRAM);
  base::DictionaryValue prefs;
  prefs.SetString("myPrefsKey", "ok");
  prefs.SetStringWithoutPathExpansion("pref.sub", "1");
  base::DictionaryValue local_state;
  local_state.SetString("myLocalKey", "ok");
  local_state.SetStringWithoutPathExpansion("local.state.sub", "2");
  Status status = internal::PrepareUserDataDir(
      temp_dir.path(), &prefs, &local_state);
  ASSERT_EQ(kOk, status.code());

  base::FilePath prefs_file =
      temp_dir.path().AppendASCII("Default").AppendASCII("Preferences");
  std::string prefs_str;
  ASSERT_TRUE(file_util::ReadFileToString(prefs_file, &prefs_str));
  scoped_ptr<base::Value> prefs_value(base::JSONReader::Read(prefs_str));
  const base::DictionaryValue* prefs_dict = NULL;
  ASSERT_TRUE(prefs_value->GetAsDictionary(&prefs_dict));
  AssertEQ(*prefs_dict, "myPrefsKey", "ok");
  AssertEQ(*prefs_dict, "pref.sub", "1");

  base::FilePath local_state_file = temp_dir.path().AppendASCII("Local State");
  std::string local_state_str;
  ASSERT_TRUE(file_util::ReadFileToString(local_state_file, &local_state_str));
  scoped_ptr<base::Value> local_state_value(
      base::JSONReader::Read(local_state_str));
  const base::DictionaryValue* local_state_dict = NULL;
  ASSERT_TRUE(local_state_value->GetAsDictionary(&local_state_dict));
  AssertEQ(*local_state_dict, "myLocalKey", "ok");
  AssertEQ(*local_state_dict, "local.state.sub", "2");
}
