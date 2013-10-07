// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/feedback_private/feedback_private_api.h"

#include "base/lazy_instance.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/feedback_private/feedback_service.h"
#include "chrome/browser/extensions/event_names.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_system.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/webui/web_ui_util.h"
#include "url/url_util.h"


namespace extensions {

using api::feedback_private::SystemInformation;
using api::feedback_private::FeedbackInfo;

static base::LazyInstance<ProfileKeyedAPIFactory<FeedbackPrivateAPI> >
    g_factory = LAZY_INSTANCE_INITIALIZER;

// static
ProfileKeyedAPIFactory<FeedbackPrivateAPI>*
    FeedbackPrivateAPI::GetFactoryInstance() {
  return &g_factory.Get();
}

FeedbackPrivateAPI::FeedbackPrivateAPI(Profile* profile)
    : profile_(profile),
      service_(FeedbackService::CreateInstance()) {
}

FeedbackPrivateAPI::~FeedbackPrivateAPI() {
  delete service_;
  service_ = NULL;
}

FeedbackService* FeedbackPrivateAPI::GetService() const {
  return service_;
}

void FeedbackPrivateAPI::RequestFeedback(
    const std::string& description_template,
    const std::string& category_tag,
    const GURL& page_url) {
  if (profile_ && ExtensionSystem::Get(profile_)->event_router()) {
    FeedbackInfo info;
    info.description = description_template;
    info.category_tag = make_scoped_ptr(new std::string(category_tag));
    info.page_url = make_scoped_ptr(new std::string(page_url.spec()));

    scoped_ptr<base::ListValue> args(new base::ListValue());
    args->Append(info.ToValue().release());

    scoped_ptr<Event> event(
        new Event(event_names::kOnFeedbackRequested, args.Pass()));
    ExtensionSystem::Get(profile_)->event_router()->BroadcastEvent(
        event.Pass());
  }
}

bool FeedbackPrivateGetUserEmailFunction::RunImpl() {
  FeedbackService* service =
      FeedbackPrivateAPI::GetFactoryInstance()->GetForProfile(
          profile())->GetService();
  DCHECK(service);
  SetResult(base::Value::CreateStringValue(service->GetUserEmail()));
  return true;
}

bool FeedbackPrivateGetSystemInformationFunction::RunImpl() {
  FeedbackService* service =
      FeedbackPrivateAPI::GetFactoryInstance()->GetForProfile(
          profile())->GetService();
  DCHECK(service);
  service->GetSystemInformation(
      base::Bind(
          &FeedbackPrivateGetSystemInformationFunction::OnCompleted, this));
  return true;
}

void FeedbackPrivateGetSystemInformationFunction::OnCompleted(
    const SystemInformationList& sys_info) {
  results_ = api::feedback_private::GetSystemInformation::Results::Create(
      sys_info);
  SendResponse(true);
}

bool FeedbackPrivateSendFeedbackFunction::RunImpl() {
  scoped_ptr<api::feedback_private::SendFeedback::Params> params(
      api::feedback_private::SendFeedback::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  const FeedbackInfo &feedback_info = params->feedback;

  std::string attached_file_url, screenshot_url;
  if (feedback_info.attached_file.get() &&
      feedback_info.attached_file_blob_url.get() &&
      !feedback_info.attached_file_blob_url->empty()) {
    attached_file_url = *feedback_info.attached_file_blob_url;
  }

  if (feedback_info.screenshot.get() &&
      feedback_info.screenshot_blob_url.get() &&
      !feedback_info.screenshot_blob_url->empty()) {
    screenshot_url = *feedback_info.screenshot_blob_url;
  }

  // Populate feedback data.
  scoped_refptr<FeedbackData> feedback_data(new FeedbackData());
  feedback_data->set_profile(profile_);
  feedback_data->set_description(feedback_info.description);

  if (feedback_info.category_tag.get())
    feedback_data->set_category_tag(*feedback_info.category_tag.get());
  if (feedback_info.page_url.get())
    feedback_data->set_page_url(*feedback_info.page_url.get());
  if (feedback_info.email.get())
    feedback_data->set_user_email(*feedback_info.email.get());

  if (!attached_file_url.empty()) {
    feedback_data->set_attached_filename(
        (*feedback_info.attached_file.get()).name);
    feedback_data->set_attached_file_url(GURL(attached_file_url));
  }

  if (!screenshot_url.empty())
    feedback_data->set_screenshot_url(GURL(screenshot_url));

  // TODO(rkc): Take this out of OS_CHROMEOS once we have FeedbackData and
  // FeedbackUtil migrated to handle system logs for both Chrome and ChromeOS.
#if defined(OS_CHROMEOS)
  scoped_ptr<chromeos::SystemLogsResponse> sys_logs(
      new chromeos::SystemLogsResponse);
  SystemInformationList* sys_info = feedback_info.system_information.get();
  if (sys_info) {
    for (SystemInformationList::iterator it = sys_info->begin();
         it != sys_info->end(); ++it)
      (*sys_logs.get())[it->get()->key] = it->get()->value;
  }
  feedback_data->set_sys_info(sys_logs.Pass());
#endif

  FeedbackService* service = FeedbackPrivateAPI::GetFactoryInstance()->
      GetForProfile(profile())->GetService();
  DCHECK(service);
  service->SendFeedback(profile(),
      feedback_data, base::Bind(
          &FeedbackPrivateSendFeedbackFunction::OnCompleted,
          this));
  return true;
}

void FeedbackPrivateSendFeedbackFunction::OnCompleted(
    bool success) {
  results_ = api::feedback_private::SendFeedback::Results::Create(
      success ? api::feedback_private::STATUS_SUCCESS :
                api::feedback_private::STATUS_DELAYED);
  SendResponse(true);
}

}  // namespace extensions
