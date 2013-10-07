// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/api/commands/commands_handler.h"

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "extensions/common/error_utils.h"

namespace keys = extension_manifest_keys;

namespace extensions {

namespace {
// The maximum number of commands (including page action/browser actions) with a
// keybinding an extension can have.
const int kMaxCommandsWithKeybindingPerExtension = 4;
}  // namespace

CommandsInfo::CommandsInfo() {
}

CommandsInfo::~CommandsInfo() {
}

// static
const Command* CommandsInfo::GetBrowserActionCommand(
   const Extension* extension) {
  CommandsInfo* info = static_cast<CommandsInfo*>(
      extension->GetManifestData(keys::kCommands));
  return info ? info->browser_action_command.get() : NULL;
}

// static
const Command* CommandsInfo::GetPageActionCommand(const Extension* extension) {
  CommandsInfo* info = static_cast<CommandsInfo*>(
      extension->GetManifestData(keys::kCommands));
  return info ? info->page_action_command.get() : NULL;
}

// static
const Command* CommandsInfo::GetScriptBadgeCommand(const Extension* extension) {
  CommandsInfo* info = static_cast<CommandsInfo*>(
      extension->GetManifestData(keys::kCommands));
  return info ? info->script_badge_command.get() : NULL;
}

// static
const CommandMap* CommandsInfo::GetNamedCommands(const Extension* extension) {
  CommandsInfo* info = static_cast<CommandsInfo*>(
      extension->GetManifestData(keys::kCommands));
  return info ? &info->named_commands : NULL;
}

CommandsHandler::CommandsHandler() {
}

CommandsHandler::~CommandsHandler() {
}

bool CommandsHandler::Parse(Extension* extension, string16* error) {
  if (!extension->manifest()->HasKey(keys::kCommands)) {
    scoped_ptr<CommandsInfo> commands_info(new CommandsInfo);
    MaybeSetBrowserActionDefault(extension, commands_info.get());
    extension->SetManifestData(keys::kCommands,
                               commands_info.release());
    return true;
  }

  const base::DictionaryValue* dict = NULL;
  if (!extension->manifest()->GetDictionary(keys::kCommands, &dict)) {
    *error = ASCIIToUTF16(extension_manifest_errors::kInvalidCommandsKey);
    return false;
  }

  scoped_ptr<CommandsInfo> commands_info(new CommandsInfo);

  int command_index = 0;
  int keybindings_found = 0;
  for (DictionaryValue::Iterator iter(*dict); !iter.IsAtEnd();
       iter.Advance()) {
    ++command_index;

    const DictionaryValue* command = NULL;
    if (!iter.value().GetAsDictionary(&command)) {
      *error = ErrorUtils::FormatErrorMessageUTF16(
          extension_manifest_errors::kInvalidKeyBindingDictionary,
          base::IntToString(command_index));
      return false;
    }

    scoped_ptr<extensions::Command> binding(new Command());
    if (!binding->Parse(command, iter.key(), command_index, error))
      return false;  // |error| already set.

    if (binding->accelerator().key_code() != ui::VKEY_UNKNOWN) {
      if (++keybindings_found > kMaxCommandsWithKeybindingPerExtension) {
        *error = ErrorUtils::FormatErrorMessageUTF16(
            extension_manifest_errors::kInvalidKeyBindingTooMany,
            base::IntToString(kMaxCommandsWithKeybindingPerExtension));
        return false;
      }
    }

    std::string command_name = binding->command_name();
    if (command_name == extension_manifest_values::kBrowserActionCommandEvent) {
      commands_info->browser_action_command.reset(binding.release());
    } else if (command_name ==
                   extension_manifest_values::kPageActionCommandEvent) {
      commands_info->page_action_command.reset(binding.release());
    } else if (command_name ==
                   extension_manifest_values::kScriptBadgeCommandEvent) {
      commands_info->script_badge_command.reset(binding.release());
    } else {
      if (command_name[0] != '_')  // All commands w/underscore are reserved.
        commands_info->named_commands[command_name] = *binding.get();
    }
  }

  MaybeSetBrowserActionDefault(extension, commands_info.get());

  extension->SetManifestData(keys::kCommands,
                             commands_info.release());
  return true;
}

bool CommandsHandler::AlwaysParseForType(Manifest::Type type) const {
  return type == Manifest::TYPE_EXTENSION ||
      type == Manifest::TYPE_LEGACY_PACKAGED_APP ||
      type == Manifest::TYPE_PLATFORM_APP;
}

void CommandsHandler::MaybeSetBrowserActionDefault(const Extension* extension,
                                                   CommandsInfo* info) {
  if (extension->manifest()->HasKey(keys::kBrowserAction) &&
      !info->browser_action_command.get()) {
    info->browser_action_command.reset(
        new Command(extension_manifest_values::kBrowserActionCommandEvent,
                    string16(),
                    std::string()));
  }
}

const std::vector<std::string> CommandsHandler::Keys() const {
  return SingleKey(keys::kCommands);
}

}  // namespace extensions
