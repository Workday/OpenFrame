// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/execute_code_function.h"

#include "chrome/browser/extensions/api/tabs/tabs_constants.h"
#include "chrome/browser/extensions/script_executor.h"
#include "chrome/common/extensions/api/i18n/default_locale_handler.h"
#include "chrome/common/extensions/extension_file_util.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/common/extensions/message_bundle.h"
#include "extensions/browser/file_reader.h"
#include "extensions/common/error_utils.h"

namespace extensions {

namespace keys = tabs_constants;
using api::tabs::InjectDetails;

ExecuteCodeFunction::ExecuteCodeFunction() {
}

ExecuteCodeFunction::~ExecuteCodeFunction() {
}

void ExecuteCodeFunction::DidLoadFile(bool success,
                                      const std::string& data) {
  const Extension* extension = GetExtension();

  // Check if the file is CSS and needs localization.
  if (success &&
      ShouldInsertCSS() &&
      extension != NULL &&
      data.find(MessageBundle::kMessageBegin) != std::string::npos) {
    content::BrowserThread::PostTask(
        content::BrowserThread::FILE, FROM_HERE,
        base::Bind(&ExecuteCodeFunction::LocalizeCSS, this,
                   data,
                   extension->id(),
                   extension->path(),
                   LocaleInfo::GetDefaultLocale(extension)));
  } else {
    DidLoadAndLocalizeFile(success, data);
  }
}

void ExecuteCodeFunction::LocalizeCSS(
    const std::string& data,
    const std::string& extension_id,
    const base::FilePath& extension_path,
    const std::string& extension_default_locale) {
  scoped_ptr<SubstitutionMap> localization_messages(
      extension_file_util::LoadMessageBundleSubstitutionMap(
          extension_path, extension_id, extension_default_locale));

  // We need to do message replacement on the data, so it has to be mutable.
  std::string css_data = data;
  std::string error;
  MessageBundle::ReplaceMessagesWithExternalDictionary(*localization_messages,
                                                       &css_data,
                                                       &error);

  // Call back DidLoadAndLocalizeFile on the UI thread. The success parameter
  // is always true, because if loading had failed, we wouldn't have had
  // anything to localize.
  content::BrowserThread::PostTask(
      content::BrowserThread::UI, FROM_HERE,
      base::Bind(&ExecuteCodeFunction::DidLoadAndLocalizeFile, this,
                 true, css_data));
}

void ExecuteCodeFunction::DidLoadAndLocalizeFile(bool success,
                                                 const std::string& data) {
  if (success) {
    if (!Execute(data))
      SendResponse(false);
  } else {
    // TODO(viettrungluu): bug: there's no particular reason the path should be
    // UTF-8, in which case this may fail.
    error_ = ErrorUtils::FormatErrorMessage(keys::kLoadFileError,
        resource_.relative_path().AsUTF8Unsafe());
    SendResponse(false);
  }
}

bool ExecuteCodeFunction::Execute(const std::string& code_string) {
  ScriptExecutor* executor = GetScriptExecutor();
  if (!executor)
    return false;

  const Extension* extension = GetExtension();
  if (!extension)
    return false;

  ScriptExecutor::ScriptType script_type = ScriptExecutor::JAVASCRIPT;
  if (ShouldInsertCSS())
    script_type = ScriptExecutor::CSS;

  ScriptExecutor::FrameScope frame_scope =
      details_->all_frames.get() && *details_->all_frames ?
          ScriptExecutor::ALL_FRAMES :
          ScriptExecutor::TOP_FRAME;

  UserScript::RunLocation run_at =
      UserScript::UNDEFINED;
  switch (details_->run_at) {
    case InjectDetails::RUN_AT_NONE:
    case InjectDetails::RUN_AT_DOCUMENT_IDLE:
      run_at = UserScript::DOCUMENT_IDLE;
      break;
    case InjectDetails::RUN_AT_DOCUMENT_START:
      run_at = UserScript::DOCUMENT_START;
      break;
    case InjectDetails::RUN_AT_DOCUMENT_END:
      run_at = UserScript::DOCUMENT_END;
      break;
  }
  CHECK_NE(UserScript::UNDEFINED, run_at);

  executor->ExecuteScript(
      extension->id(),
      script_type,
      code_string,
      frame_scope,
      run_at,
      ScriptExecutor::ISOLATED_WORLD,
      IsWebView(),
      base::Bind(&ExecuteCodeFunction::OnExecuteCodeFinished, this));
  return true;
}

bool ExecuteCodeFunction::HasPermission() {
  return true;
}

bool ExecuteCodeFunction::RunImpl() {
  EXTENSION_FUNCTION_VALIDATE(Init());

  if (!details_->code.get() && !details_->file.get()) {
    error_ = keys::kNoCodeOrFileToExecuteError;
    return false;
  }
  if (details_->code.get() && details_->file.get()) {
    error_ = keys::kMoreThanOneValuesError;
    return false;
  }

  if (!CanExecuteScriptOnPage())
    return false;

  if (details_->code.get())
    return Execute(*details_->code);

  if (!details_->file.get())
    return false;
  resource_ = GetExtension()->GetResource(*details_->file);

  if (resource_.extension_root().empty() || resource_.relative_path().empty()) {
    error_ = keys::kNoCodeOrFileToExecuteError;
    return false;
  }

  scoped_refptr<FileReader> file_reader(new FileReader(
      resource_, base::Bind(&ExecuteCodeFunction::DidLoadFile, this)));
  file_reader->Start();

  return true;
}

void ExecuteCodeFunction::OnExecuteCodeFinished(
    const std::string& error,
    int32 on_page_id,
    const GURL& on_url,
    const base::ListValue& result) {
  if (!error.empty())
    SetError(error);

  SendResponse(error.empty());
}

}  // namespace extensions
