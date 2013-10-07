// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/webdriver/commands/execute_async_script_command.h"

#include "base/values.h"
#include "chrome/test/webdriver/commands/response.h"
#include "chrome/test/webdriver/webdriver_error.h"
#include "chrome/test/webdriver/webdriver_session.h"

namespace webdriver {

ExecuteAsyncScriptCommand::ExecuteAsyncScriptCommand(
    const std::vector<std::string>& path_segments,
    const DictionaryValue* const parameters)
    : WebDriverCommand(path_segments, parameters) {}

ExecuteAsyncScriptCommand::~ExecuteAsyncScriptCommand() {}

bool ExecuteAsyncScriptCommand::DoesPost() {
  return true;
}

void ExecuteAsyncScriptCommand::ExecutePost(Response* const response) {
  std::string script;
  if (!GetStringParameter("script", &script)) {
    response->SetError(new Error(
        kBadRequest, "No script to execute specified"));
    return;
  }

  const ListValue* args;
  if (!GetListParameter("args", &args)) {
    response->SetError(new Error(
        kBadRequest, "No script arguments specified"));
    return;
  }

  Value* result = NULL;
  Error* error = session_->ExecuteAsyncScript(
      session_->current_target(), script, args, &result);
  if (error) {
    error->AddDetails("Script execution failed. Script: " + script);
    response->SetError(error);
    return;
  }
  response->SetValue(result);
}

}  // namspace webdriver
