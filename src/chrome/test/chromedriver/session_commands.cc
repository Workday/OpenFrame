// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/chromedriver/session_commands.h"

#include <list>

#include "base/bind.h"
#include "base/callback.h"
#include "base/file_util.h"
#include "base/logging.h"  // For CHECK macros.
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/values.h"
#include "chrome/test/chromedriver/basic_types.h"
#include "chrome/test/chromedriver/chrome/automation_extension.h"
#include "chrome/test/chromedriver/chrome/chrome.h"
#include "chrome/test/chromedriver/chrome/geoposition.h"
#include "chrome/test/chromedriver/chrome/status.h"
#include "chrome/test/chromedriver/chrome/web_view.h"
#include "chrome/test/chromedriver/logging.h"
#include "chrome/test/chromedriver/session.h"
#include "chrome/test/chromedriver/util.h"

namespace {

const char kWindowHandlePrefix[] = "CDwindow-";

std::string WebViewIdToWindowHandle(const std::string& web_view_id) {
  return kWindowHandlePrefix + web_view_id;
}

bool WindowHandleToWebViewId(const std::string& window_handle,
                             std::string* web_view_id) {
  if (window_handle.find(kWindowHandlePrefix) != 0u)
    return false;
  *web_view_id = window_handle.substr(
      std::string(kWindowHandlePrefix).length());
  return true;
}

}  // namespace

Status ExecuteQuit(
    bool allow_detach,
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  if (allow_detach && session->detach) {
    return Status(kOk);
  } else {
    session->quit = true;
    return session->chrome->Quit();
  }
}

Status ExecuteGetSessionCapabilities(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  value->reset(session->capabilities->DeepCopy());
  return Status(kOk);
}

Status ExecuteGetCurrentWindowHandle(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  WebView* web_view = NULL;
  Status status = session->GetTargetWindow(&web_view);
  if (status.IsError())
    return status;

  value->reset(new StringValue(WebViewIdToWindowHandle(web_view->GetId())));
  return Status(kOk);
}

Status ExecuteClose(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  std::list<std::string> web_view_ids;
  Status status = session->chrome->GetWebViewIds(&web_view_ids);
  if (status.IsError())
    return status;
  bool is_last_web_view = web_view_ids.size() == 1u;
  web_view_ids.clear();

  WebView* web_view = NULL;
  status = session->GetTargetWindow(&web_view);
  if (status.IsError())
    return status;

  status = session->chrome->CloseWebView(web_view->GetId());
  if (status.IsError())
    return status;

  status = session->chrome->GetWebViewIds(&web_view_ids);
  if ((status.code() == kChromeNotReachable && is_last_web_view) ||
      (status.IsOk() && web_view_ids.empty())) {
    // If no window is open, close is the equivalent of calling "quit".
    session->quit = true;
    return session->chrome->Quit();
  }

  return status;
}

Status ExecuteGetWindowHandles(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  std::list<std::string> web_view_ids;
  Status status = session->chrome->GetWebViewIds(&web_view_ids);
  if (status.IsError())
    return status;
  scoped_ptr<base::ListValue> window_ids(new base::ListValue());
  for (std::list<std::string>::const_iterator it = web_view_ids.begin();
       it != web_view_ids.end(); ++it) {
    window_ids->AppendString(WebViewIdToWindowHandle(*it));
  }
  value->reset(window_ids.release());
  return Status(kOk);
}

Status ExecuteSwitchToWindow(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  std::string name;
  if (!params.GetString("name", &name) || name.empty())
    return Status(kUnknownError, "'name' must be a nonempty string");

  std::list<std::string> web_view_ids;
  Status status = session->chrome->GetWebViewIds(&web_view_ids);
  if (status.IsError())
    return status;

  std::string web_view_id;
  bool found = false;
  if (WindowHandleToWebViewId(name, &web_view_id)) {
    // Check if any web_view matches |web_view_id|.
    for (std::list<std::string>::const_iterator it = web_view_ids.begin();
         it != web_view_ids.end(); ++it) {
      if (*it == web_view_id) {
        found = true;
        break;
      }
    }
  } else {
    // Check if any of the tab window names match |name|.
    const char* kGetWindowNameScript = "function() { return window.name; }";
    base::ListValue args;
    for (std::list<std::string>::const_iterator it = web_view_ids.begin();
         it != web_view_ids.end(); ++it) {
      scoped_ptr<base::Value> result;
      WebView* web_view;
      status = session->chrome->GetWebViewById(*it, &web_view);
      if (status.IsError())
        return status;
      status = web_view->ConnectIfNecessary();
      if (status.IsError())
        return status;
      status = web_view->CallFunction(
          std::string(), kGetWindowNameScript, args, &result);
      if (status.IsError())
        return status;
      std::string window_name;
      if (!result->GetAsString(&window_name))
        return Status(kUnknownError, "failed to get window name");
      if (window_name == name) {
        web_view_id = *it;
        found = true;
        break;
      }
    }
  }

  if (!found)
    return Status(kNoSuchWindow);

  if (session->overridden_geoposition) {
    WebView* web_view;
    status = session->chrome->GetWebViewById(web_view_id, &web_view);
    if (status.IsError())
      return status;
    status = web_view->ConnectIfNecessary();
    if (status.IsError())
      return status;
    status = web_view->OverrideGeolocation(*session->overridden_geoposition);
    if (status.IsError())
      return status;
  }

  session->window = web_view_id;
  session->SwitchToTopFrame();
  session->mouse_position = WebPoint(0, 0);
  return Status(kOk);
}

Status ExecuteSetTimeout(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  double ms_double;
  if (!params.GetDouble("ms", &ms_double))
    return Status(kUnknownError, "'ms' must be a double");
  std::string type;
  if (!params.GetString("type", &type))
    return Status(kUnknownError, "'type' must be a string");

  int ms = static_cast<int>(ms_double);
  // TODO(frankf): implicit and script timeout should be cleared
  // if negative timeout is specified.
  if (type == "implicit")
    session->implicit_wait = ms;
  else if (type == "script")
    session->script_timeout = ms;
  else if (type == "page load")
    session->page_load_timeout =
        ((ms < 0) ? Session::kDefaultPageLoadTimeoutMs : ms);
  else
    return Status(kUnknownError, "unknown type of timeout:" + type);
  return Status(kOk);
}

Status ExecuteSetScriptTimeout(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  double ms;
  if (!params.GetDouble("ms", &ms) || ms < 0)
    return Status(kUnknownError, "'ms' must be a non-negative number");
  session->script_timeout = static_cast<int>(ms);
  return Status(kOk);
}

Status ExecuteImplicitlyWait(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  double ms;
  if (!params.GetDouble("ms", &ms) || ms < 0)
    return Status(kUnknownError, "'ms' must be a non-negative number");
  session->implicit_wait = static_cast<int>(ms);
  return Status(kOk);
}

Status ExecuteIsLoading(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  WebView* web_view = NULL;
  Status status = session->GetTargetWindow(&web_view);
  if (status.IsError())
    return status;

  status = web_view->ConnectIfNecessary();
  if (status.IsError())
    return status;

  bool is_pending;
  status = web_view->IsPendingNavigation(
      session->GetCurrentFrameId(), &is_pending);
  if (status.IsError())
    return status;
  value->reset(new base::FundamentalValue(is_pending));
  return Status(kOk);
}

Status ExecuteGetLocation(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  if (!session->overridden_geoposition) {
    return Status(kUnknownError,
                  "Location must be set before it can be retrieved");
  }
  base::DictionaryValue location;
  location.SetDouble("latitude", session->overridden_geoposition->latitude);
  location.SetDouble("longitude", session->overridden_geoposition->longitude);
  location.SetDouble("accuracy", session->overridden_geoposition->accuracy);
  // Set a dummy altitude to make WebDriver clients happy.
  // https://code.google.com/p/chromedriver/issues/detail?id=281
  location.SetDouble("altitude", 0);
  value->reset(location.DeepCopy());
  return Status(kOk);
}

Status ExecuteGetWindowPosition(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  AutomationExtension* extension = NULL;
  Status status = session->chrome->GetAutomationExtension(&extension);
  if (status.IsError())
    return status;

  int x, y;
  status = extension->GetWindowPosition(&x, &y);
  if (status.IsError())
    return status;

  base::DictionaryValue position;
  position.SetInteger("x", x);
  position.SetInteger("y", y);
  value->reset(position.DeepCopy());
  return Status(kOk);
}

Status ExecuteSetWindowPosition(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  double x, y;
  if (!params.GetDouble("x", &x) || !params.GetDouble("y", &y))
    return Status(kUnknownError, "missing or invalid 'x' or 'y'");
  AutomationExtension* extension = NULL;
  Status status = session->chrome->GetAutomationExtension(&extension);
  if (status.IsError())
    return status;

  return extension->SetWindowPosition(static_cast<int>(x), static_cast<int>(y));
}

Status ExecuteGetWindowSize(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  AutomationExtension* extension = NULL;
  Status status = session->chrome->GetAutomationExtension(&extension);
  if (status.IsError())
    return status;

  int width, height;
  status = extension->GetWindowSize(&width, &height);
  if (status.IsError())
    return status;

  base::DictionaryValue size;
  size.SetInteger("width", width);
  size.SetInteger("height", height);
  value->reset(size.DeepCopy());
  return Status(kOk);
}

Status ExecuteSetWindowSize(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  double width, height;
  if (!params.GetDouble("width", &width) ||
      !params.GetDouble("height", &height))
    return Status(kUnknownError, "missing or invalid 'width' or 'height'");
  AutomationExtension* extension = NULL;
  Status status = session->chrome->GetAutomationExtension(&extension);
  if (status.IsError())
    return status;

  return extension->SetWindowSize(
      static_cast<int>(width), static_cast<int>(height));
}

Status ExecuteMaximizeWindow(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  AutomationExtension* extension = NULL;
  Status status = session->chrome->GetAutomationExtension(&extension);
  if (status.IsError())
    return status;

  return extension->MaximizeWindow();
}

Status ExecuteGetAvailableLogTypes(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  scoped_ptr<base::ListValue> types(new base::ListValue());
  for (ScopedVector<WebDriverLog>::const_iterator log
       = session->devtools_logs.begin();
       log != session->devtools_logs.end(); ++log) {
    types->AppendString((*log)->GetType());
  }
  value->reset(types.release());
  return Status(kOk);
}

Status ExecuteGetLog(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  std::string log_type;
  if (!params.GetString("type", &log_type)) {
    return Status(kUnknownError, "missing or invalid 'type'");
  }
  for (ScopedVector<WebDriverLog>::const_iterator log
       = session->devtools_logs.begin();
       log != session->devtools_logs.end(); ++log) {
    if (log_type == (*log)->GetType()) {
      scoped_ptr<base::ListValue> log_entries = (*log)->GetAndClearEntries();
      value->reset(log_entries.release());
      return Status(kOk);
    }
  }
  return Status(kUnknownError, "log type '" + log_type + "' not found");
}

Status ExecuteUploadFile(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
    std::string base64_zip_data;
  if (!params.GetString("file", &base64_zip_data))
    return Status(kUnknownError, "missing or invalid 'file'");
  std::string zip_data;
  if (!Base64Decode(base64_zip_data, &zip_data))
    return Status(kUnknownError, "unable to decode 'file'");

  if (!session->temp_dir.IsValid()) {
    if (!session->temp_dir.CreateUniqueTempDir())
      return Status(kUnknownError, "unable to create temp dir");
  }
  base::FilePath upload_dir;
  if (!file_util::CreateTemporaryDirInDir(
          session->temp_dir.path(), FILE_PATH_LITERAL("upload"), &upload_dir)) {
    return Status(kUnknownError, "unable to create temp dir");
  }
  std::string error_msg;
  base::FilePath upload;
  Status status = UnzipSoleFile(upload_dir, zip_data, &upload);
  if (status.IsError())
    return Status(kUnknownError, "unable to unzip 'file'", status);

  value->reset(new base::StringValue(upload.value()));
  return Status(kOk);
}
