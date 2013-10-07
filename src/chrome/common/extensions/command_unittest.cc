// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/command.h"

#include "base/memory/scoped_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

class CommandTest : public testing::Test {
};

TEST(CommandTest, ExtensionCommandParsing) {
  const ui::Accelerator none = ui::Accelerator();
  const ui::Accelerator shift_f = ui::Accelerator(ui::VKEY_F,
                                                  ui::EF_SHIFT_DOWN);
#if defined(OS_MACOSX)
  int ctrl = ui::EF_COMMAND_DOWN;
#else
  int ctrl = ui::EF_CONTROL_DOWN;
#endif

  const ui::Accelerator ctrl_f = ui::Accelerator(ui::VKEY_F, ctrl);
  const ui::Accelerator alt_f = ui::Accelerator(ui::VKEY_F, ui::EF_ALT_DOWN);
  const ui::Accelerator ctrl_shift_f =
      ui::Accelerator(ui::VKEY_F, ctrl | ui::EF_SHIFT_DOWN);
  const ui::Accelerator alt_shift_f =
      ui::Accelerator(ui::VKEY_F, ui::EF_ALT_DOWN | ui::EF_SHIFT_DOWN);
  const ui::Accelerator ctrl_1 = ui::Accelerator(ui::VKEY_1, ctrl);
  const ui::Accelerator ctrl_comma = ui::Accelerator(ui::VKEY_OEM_COMMA, ctrl);
  const ui::Accelerator ctrl_dot = ui::Accelerator(ui::VKEY_OEM_PERIOD, ctrl);
  const ui::Accelerator ctrl_left = ui::Accelerator(ui::VKEY_LEFT, ctrl);
  const ui::Accelerator ctrl_right = ui::Accelerator(ui::VKEY_RIGHT, ctrl);
  const ui::Accelerator ctrl_up = ui::Accelerator(ui::VKEY_UP, ctrl);
  const ui::Accelerator ctrl_down = ui::Accelerator(ui::VKEY_DOWN, ctrl);
  const ui::Accelerator ctrl_ins = ui::Accelerator(ui::VKEY_INSERT, ctrl);
  const ui::Accelerator ctrl_del = ui::Accelerator(ui::VKEY_DELETE, ctrl);
  const ui::Accelerator ctrl_home = ui::Accelerator(ui::VKEY_HOME, ctrl);
  const ui::Accelerator ctrl_end = ui::Accelerator(ui::VKEY_END, ctrl);
  const ui::Accelerator ctrl_pgup = ui::Accelerator(ui::VKEY_PRIOR, ctrl);
  const ui::Accelerator ctrl_pgdwn = ui::Accelerator(ui::VKEY_NEXT, ctrl);

  const struct {
    bool expected_result;
    ui::Accelerator accelerator;
    const char* command_name;
    const char* key;
    const char* description;
  } kTests[] = {
    // Negative test (one or more missing required fields). We don't need to
    // test |command_name| being blank as it is used as a key in the manifest,
    // so it can't be blank (and we CHECK() when it is). A blank shortcut is
    // permitted.
    { false, none, "command", "",       "" },
    { false, none, "command", "Ctrl+f", "" },
    // Ctrl+Alt is not permitted, see MSDN link in comments in Parse function.
    { false, none, "command", "Ctrl+Alt+F", "description" },
    // Unsupported shortcuts/too many, or missing modifier.
    { false, none, "command", "A",                "description" },
    { false, none, "command", "F10",              "description" },
    { false, none, "command", "Ctrl+F+G",         "description" },
    { false, none, "command", "Ctrl+Alt+Shift+G", "description" },
    // Shift on its own is not supported.
    { false, shift_f, "command", "Shift+F",       "description" },
    { false, shift_f, "command", "F+Shift",       "description" },
    // Basic tests.
    { true, none,         "command", "",             "description" },
    { true, ctrl_f,       "command", "Ctrl+F",       "description" },
    { true, alt_f,        "command", "Alt+F",        "description" },
    { true, ctrl_shift_f, "command", "Ctrl+Shift+F", "description" },
    { true, alt_shift_f,  "command", "Alt+Shift+F",  "description" },
    { true, ctrl_1,       "command", "Ctrl+1",       "description" },
    // Shortcut token order tests.
    { true, ctrl_f,       "command", "F+Ctrl",       "description" },
    { true, alt_f,        "command", "F+Alt",        "description" },
    { true, ctrl_shift_f, "command", "F+Ctrl+Shift", "description" },
    { true, ctrl_shift_f, "command", "F+Shift+Ctrl", "description" },
    { true, alt_shift_f,  "command", "F+Alt+Shift",  "description" },
    { true, alt_shift_f,  "command", "F+Shift+Alt",  "description" },
    // Case insensitivity is not OK.
    { false, ctrl_f, "command", "Ctrl+f", "description" },
    { false, ctrl_f, "command", "cTrL+F", "description" },
    // Skipping description is OK for browser- and pageActions.
    { true, ctrl_f, "_execute_browser_action", "Ctrl+F", "" },
    { true, ctrl_f, "_execute_page_action",    "Ctrl+F", "" },
    // Home, End, Arrow keys, etc.
    { true, ctrl_comma, "_execute_browser_action", "Ctrl+Comma", "" },
    { true, ctrl_dot, "_execute_browser_action", "Ctrl+Period", "" },
    { true, ctrl_left, "_execute_browser_action", "Ctrl+Left", "" },
    { true, ctrl_right, "_execute_browser_action", "Ctrl+Right", "" },
    { true, ctrl_up, "_execute_browser_action", "Ctrl+Up", "" },
    { true, ctrl_down, "_execute_browser_action", "Ctrl+Down", "" },
    { true, ctrl_ins, "_execute_browser_action", "Ctrl+Insert", "" },
    { true, ctrl_del, "_execute_browser_action", "Ctrl+Delete", "" },
    { true, ctrl_home, "_execute_browser_action", "Ctrl+Home", "" },
    { true, ctrl_end, "_execute_browser_action", "Ctrl+End", "" },
    { true, ctrl_pgup, "_execute_browser_action", "Ctrl+PageUp", "" },
    { true, ctrl_pgdwn, "_execute_browser_action", "Ctrl+PageDown", "" },
  };

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(kTests); ++i) {
    // First parse the command as a simple string.
    scoped_ptr<base::DictionaryValue> input(new base::DictionaryValue);
    input->SetString("suggested_key", kTests[i].key);
    input->SetString("description", kTests[i].description);

    SCOPED_TRACE(std::string("Command name: |") + kTests[i].command_name +
                 "| key: |" + kTests[i].key +
                 "| description: |" + kTests[i].description +
                 "| index: " + base::IntToString(i));

    extensions::Command command;
    string16 error;
    bool result =
        command.Parse(input.get(), kTests[i].command_name, i, &error);

    EXPECT_EQ(kTests[i].expected_result, result);
    if (result) {
      EXPECT_STREQ(kTests[i].description,
                   UTF16ToASCII(command.description()).c_str());
      EXPECT_STREQ(kTests[i].command_name, command.command_name().c_str());
      EXPECT_EQ(kTests[i].accelerator, command.accelerator());
    }

    // Now parse the command as a dictionary of multiple values.
    if (kTests[i].key[0] != '\0') {
      input.reset(new base::DictionaryValue);
      base::DictionaryValue* key_dict = new base::DictionaryValue();
      key_dict->SetString("default", kTests[i].key);
      key_dict->SetString("windows", kTests[i].key);
      key_dict->SetString("mac", kTests[i].key);
      input->Set("suggested_key", key_dict);
      input->SetString("description", kTests[i].description);

      result = command.Parse(input.get(), kTests[i].command_name, i, &error);

      EXPECT_EQ(kTests[i].expected_result, result);
      if (result) {
        EXPECT_STREQ(kTests[i].description,
                     UTF16ToASCII(command.description()).c_str());
        EXPECT_STREQ(kTests[i].command_name, command.command_name().c_str());
        EXPECT_EQ(kTests[i].accelerator, command.accelerator());
      }
    }
  }
}

TEST(CommandTest, ExtensionCommandParsingFallback) {
  std::string description = "desc";
  std::string command_name = "foo";

  // Test that platform specific keys are honored on each platform, despite
  // fallback being given.
  scoped_ptr<base::DictionaryValue> input(new base::DictionaryValue);
  base::DictionaryValue* key_dict = new base::DictionaryValue();
  key_dict->SetString("default",  "Ctrl+Shift+D");
  key_dict->SetString("windows",  "Ctrl+Shift+W");
  key_dict->SetString("mac",      "Ctrl+Shift+M");
  key_dict->SetString("linux",    "Ctrl+Shift+L");
  key_dict->SetString("chromeos", "Ctrl+Shift+C");
  input->Set("suggested_key", key_dict);
  input->SetString("description", description);

  extensions::Command command;
  string16 error;
  EXPECT_TRUE(command.Parse(input.get(), command_name, 0, &error));
  EXPECT_STREQ(description.c_str(),
               UTF16ToASCII(command.description()).c_str());
  EXPECT_STREQ(command_name.c_str(), command.command_name().c_str());

#if defined(OS_WIN)
  ui::Accelerator accelerator(ui::VKEY_W,
                              ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN);
#elif defined(OS_MACOSX)
  ui::Accelerator accelerator(ui::VKEY_M,
                              ui::EF_SHIFT_DOWN | ui::EF_COMMAND_DOWN);
#elif defined(OS_CHROMEOS)
  ui::Accelerator accelerator(ui::VKEY_C,
                              ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN);
#elif defined(OS_LINUX)
  ui::Accelerator accelerator(ui::VKEY_L,
                              ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN);
#else
  ui::Accelerator accelerator(ui::VKEY_D,
                              ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN);
#endif
  EXPECT_EQ(accelerator, command.accelerator());

  // Misspell a platform.
  key_dict->SetString("windosw", "Ctrl+M");
  EXPECT_FALSE(command.Parse(input.get(), command_name, 0, &error));
  EXPECT_TRUE(key_dict->Remove("windosw", NULL));

  // Now remove platform specific keys (leaving just "default") and make sure
  // every platform falls back to the default.
  EXPECT_TRUE(key_dict->Remove("windows", NULL));
  EXPECT_TRUE(key_dict->Remove("mac", NULL));
  EXPECT_TRUE(key_dict->Remove("linux", NULL));
  EXPECT_TRUE(key_dict->Remove("chromeos", NULL));
  EXPECT_TRUE(command.Parse(input.get(), command_name, 0, &error));
  EXPECT_EQ(ui::VKEY_D, command.accelerator().key_code());

  // Now remove "default", leaving no option but failure. Or, in the words of
  // the immortal Adam Savage: "Failure is always an option".
  EXPECT_TRUE(key_dict->Remove("default", NULL));
  EXPECT_FALSE(command.Parse(input.get(), command_name, 0, &error));

  // Make sure Command is not supported for non-Mac platforms.
  key_dict->SetString("default", "Command+M");
  EXPECT_FALSE(command.Parse(input.get(), command_name, 0, &error));
  EXPECT_TRUE(key_dict->Remove("default", NULL));
  key_dict->SetString("windows", "Command+M");
  EXPECT_FALSE(command.Parse(input.get(), command_name, 0, &error));
  EXPECT_TRUE(key_dict->Remove("windows", NULL));

  // Now add only a valid platform that we are not running on to make sure devs
  // are notified of errors on other platforms.
#if defined(OS_WIN)
  key_dict->SetString("mac", "Ctrl+Shift+M");
#else
  key_dict->SetString("windows", "Ctrl+Shift+W");
#endif
  EXPECT_FALSE(command.Parse(input.get(), command_name, 0, &error));

  // Make sure Mac specific keys are not processed on other platforms.
#if !defined(OS_MACOSX)
  key_dict->SetString("windows", "Command+Shift+M");
  EXPECT_FALSE(command.Parse(input.get(), command_name, 0, &error));
#endif
}
