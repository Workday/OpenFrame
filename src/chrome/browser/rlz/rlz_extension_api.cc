// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/rlz/rlz_extension_api.h"

#include "base/bind.h"
#include "base/memory/scoped_ptr.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/extensions/extension.h"
#include "rlz/lib/lib_values.h"
#include "rlz/lib/rlz_lib.h"

namespace {

bool GetProductFromName(const std::string& product_name,
                        rlz_lib::Product* product) {
  bool success = true;
  switch (product_name[0]) {
    case 'B':
      *product = rlz_lib::FF_TOOLBAR;
      break;
    case 'C':
      *product = rlz_lib::CHROME;
      break;
    case 'D':
      *product = rlz_lib::DESKTOP;
      break;
    case 'K':
      *product = rlz_lib::QSB_WIN;
      break;
    case 'N':
      *product = rlz_lib::PINYIN_IME;
      break;
    case 'P':
      *product = rlz_lib::TOOLBAR_NOTIFIER;
      break;
    case 'T':
      *product = rlz_lib::IE_TOOLBAR;
      break;
    case 'U':
      *product = rlz_lib::PACK;
      break;
    case 'W':
      *product = rlz_lib::WEBAPPS;
      break;
    default:
      success = false;
      break;
  }

  return success;
}

bool GetEventFromName(const std::string& event_name,
                      rlz_lib::Event* event_id) {
  *event_id = rlz_lib::INVALID_EVENT;

  if (event_name == "install") {
    *event_id = rlz_lib::INSTALL;
  } else if (event_name == "set-to-google") {
    *event_id = rlz_lib::SET_TO_GOOGLE;
  } else if (event_name == "first-search") {
    *event_id = rlz_lib::FIRST_SEARCH;
  } else if (event_name == "activate") {
    *event_id = rlz_lib::ACTIVATE;
  }

  return *event_id != rlz_lib::INVALID_EVENT;
}

}  // namespace

bool RlzRecordProductEventFunction::RunImpl() {
  // This can be slow if registry access goes to disk. Should preferably
  // perform registry operations on the File thread.
  //   http://code.google.com/p/chromium/issues/detail?id=62098
  base::ThreadRestrictions::ScopedAllowIO allow_io;

  std::string product_name;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &product_name));
  rlz_lib::Product product;
  EXTENSION_FUNCTION_VALIDATE(GetProductFromName(product_name, &product));

  std::string ap_name;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(1, &ap_name));
  rlz_lib::AccessPoint access_point;
  EXTENSION_FUNCTION_VALIDATE(rlz_lib::GetAccessPointFromName(ap_name.c_str(),
                                                              &access_point));

  std::string event_name;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(2, &event_name));
  rlz_lib::Event event_id;
  EXTENSION_FUNCTION_VALIDATE(GetEventFromName(event_name, &event_id));

  return rlz_lib::RecordProductEvent(product, access_point, event_id);
}

bool RlzGetAccessPointRlzFunction::RunImpl() {
  // This can be slow if registry access goes to disk. Should preferably
  // perform registry operations on the File thread.
  //   http://code.google.com/p/chromium/issues/detail?id=62098
  base::ThreadRestrictions::ScopedAllowIO allow_io;

  std::string ap_name;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &ap_name));
  rlz_lib::AccessPoint access_point;
  EXTENSION_FUNCTION_VALIDATE(rlz_lib::GetAccessPointFromName(ap_name.c_str(),
                                                              &access_point));

  char rlz[rlz_lib::kMaxRlzLength + 1];
  rlz_lib::GetAccessPointRlz(access_point, rlz, rlz_lib::kMaxRlzLength);
  SetResult(Value::CreateStringValue(rlz));
  return true;
}

RlzSendFinancialPingFunction::RlzSendFinancialPingFunction()
    : product_(rlz_lib::CHROME),
      exclude_machine_id_(true) {
}

RlzSendFinancialPingFunction::~RlzSendFinancialPingFunction() {
}

bool RlzSendFinancialPingFunction::RunImpl() {
  std::string product_name;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &product_name));
  EXTENSION_FUNCTION_VALIDATE(GetProductFromName(product_name, &product_));

  ListValue* access_points_list;
  EXTENSION_FUNCTION_VALIDATE(args_->GetList(1, &access_points_list));
  if (access_points_list->GetSize() < 1) {
    EXTENSION_FUNCTION_ERROR("Access point array should not be empty.");
  }

  // Allocate an access point array to pass to ClearProductState().  The array
  // must be terminated with the value rlz_lib::NO_ACCESS_POINT, hence + 1
  // when allocating the array.
  access_points_.reset(
      new rlz_lib::AccessPoint[access_points_list->GetSize() + 1]);

  size_t i;
  for (i = 0; i < access_points_list->GetSize(); ++i) {
    std::string ap_name;
    EXTENSION_FUNCTION_VALIDATE(access_points_list->GetString(i, &ap_name));
    EXTENSION_FUNCTION_VALIDATE(rlz_lib::GetAccessPointFromName(
        ap_name.c_str(), &access_points_[i]));
  }
  access_points_[i] = rlz_lib::NO_ACCESS_POINT;

  EXTENSION_FUNCTION_VALIDATE(args_->GetString(2, &signature_));
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(3, &brand_));
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(4, &id_));
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(5, &lang_));
  EXTENSION_FUNCTION_VALIDATE(args_->GetBoolean(6, &exclude_machine_id_));

  // |system_request_context| needs to run on the UI thread.
  rlz_lib::SetURLRequestContext(g_browser_process->system_request_context());

  content::BrowserThread::GetBlockingPool()->PostTask(
      FROM_HERE,
      base::Bind(&RlzSendFinancialPingFunction::WorkOnWorkerThread, this));

  return true;
}

void RlzSendFinancialPingFunction::WorkOnWorkerThread() {
  // rlz_lib::SendFinancialPing() will not send a ping more often than once in
  // any 24-hour period.  Calling it more often has no effect.  If a ping is
  // not sent false is returned, but this is not an error, so we should not
  // use the return value of rlz_lib::SendFinancialPing() as the return value
  // of this function.  Callers interested in the return value can register
  // an optional callback function.
  bool sent = rlz_lib::SendFinancialPing(product_, access_points_.get(),
                                         signature_.c_str(), brand_.c_str(),
                                         id_.c_str(), lang_.c_str(),
                                         exclude_machine_id_);

  SetResult(Value::CreateBooleanValue(sent));

  bool post_task_result = content::BrowserThread::PostTask(
      content::BrowserThread::UI, FROM_HERE,
      base::Bind(&RlzSendFinancialPingFunction::RespondOnUIThread, this));
  DCHECK(post_task_result);
}

void RlzSendFinancialPingFunction::RespondOnUIThread() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  SendResponse(true);
}

bool RlzClearProductStateFunction::RunImpl() {
  // This can be slow if registry access goes to disk. Should preferably
  // perform registry operations on the File thread.
  //   http://code.google.com/p/chromium/issues/detail?id=62098
  base::ThreadRestrictions::ScopedAllowIO allow_io;

  std::string product_name;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &product_name));
  rlz_lib::Product product;
  EXTENSION_FUNCTION_VALIDATE(GetProductFromName(product_name, &product));

  ListValue* access_points_list;
  EXTENSION_FUNCTION_VALIDATE(args_->GetList(1, &access_points_list));
  if (access_points_list->GetSize() < 1) {
    EXTENSION_FUNCTION_ERROR("Access point array should not be empty.");
  }

  // Allocate an access point array to pass to ClearProductState().  The array
  // must be termindated with the value rlz_lib::NO_ACCESS_POINT, hence + 1
  // when allocating the array.
  scoped_ptr<rlz_lib::AccessPoint[]> access_points(
      new rlz_lib::AccessPoint[access_points_list->GetSize() + 1]);

  size_t i;
  for (i = 0; i < access_points_list->GetSize(); ++i) {
    std::string ap_name;
    EXTENSION_FUNCTION_VALIDATE(access_points_list->GetString(i, &ap_name));
    EXTENSION_FUNCTION_VALIDATE(rlz_lib::GetAccessPointFromName(
        ap_name.c_str(), &access_points[i]));
  }
  access_points[i] = rlz_lib::NO_ACCESS_POINT;

  rlz_lib::ClearProductState(product, access_points.get());
  return true;
}
