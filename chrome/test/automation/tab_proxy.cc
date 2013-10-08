// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/automation/tab_proxy.h"

#include <algorithm>

#include "base/json/json_string_value_serializer.h"
#include "base/logging.h"
#include "base/strings/string16.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/platform_thread.h"
#include "chrome/common/automation_constants.h"
#include "chrome/common/automation_messages.h"
#include "chrome/test/automation/automation_proxy.h"
#include "chrome/test/automation/browser_proxy.h"
#include "url/gurl.h"

TabProxy::TabProxy(AutomationMessageSender* sender,
                   AutomationHandleTracker* tracker,
                   int handle)
    : AutomationResourceProxy(tracker, sender, handle) {
}

bool TabProxy::GetTabTitle(std::wstring* title) const {
  if (!is_valid())
    return false;

  if (!title) {
    NOTREACHED();
    return false;
  }

  int tab_title_size_response = 0;

  bool succeeded = sender_->Send(
      new AutomationMsg_TabTitle(handle_, &tab_title_size_response, title));
  return succeeded;
}

bool TabProxy::GetTabIndex(int* index) const {
  if (!is_valid())
    return false;

  if (!index) {
    NOTREACHED();
    return false;
  }

  return sender_->Send(new AutomationMsg_TabIndex(handle_, index));
}

int TabProxy::FindInPage(const std::wstring& search_string,
                         FindInPageDirection forward,
                         FindInPageCase match_case,
                         bool find_next,
                         int* ordinal) {
  if (!is_valid())
    return -1;

  AutomationMsg_Find_Params params;
  params.search_string = WideToUTF16Hack(search_string);
  params.find_next = find_next;
  params.match_case = (match_case == CASE_SENSITIVE);
  params.forward = (forward == FWD);

  int matches = 0;
  int ordinal2 = 0;
  bool succeeded = sender_->Send(new AutomationMsg_Find(handle_,
                                                        params,
                                                        &ordinal2,
                                                        &matches));
  if (!succeeded)
    return -1;
  if (ordinal)
    *ordinal = ordinal2;
  return matches;
}

AutomationMsg_NavigationResponseValues TabProxy::NavigateToURL(
    const GURL& url) {
  return NavigateToURLBlockUntilNavigationsComplete(url, 1);
}

AutomationMsg_NavigationResponseValues
    TabProxy::NavigateToURLBlockUntilNavigationsComplete(
        const GURL& url, int number_of_navigations) {
  if (!is_valid())
    return AUTOMATION_MSG_NAVIGATION_ERROR;

  AutomationMsg_NavigationResponseValues navigate_response =
      AUTOMATION_MSG_NAVIGATION_ERROR;

 sender_->Send(new AutomationMsg_NavigateToURLBlockUntilNavigationsComplete(
      handle_, url, number_of_navigations, &navigate_response));

  return navigate_response;
}

AutomationMsg_NavigationResponseValues TabProxy::GoBack() {
  return GoBackBlockUntilNavigationsComplete(1);
}

AutomationMsg_NavigationResponseValues
    TabProxy::GoBackBlockUntilNavigationsComplete(int number_of_navigations) {
  if (!is_valid())
    return AUTOMATION_MSG_NAVIGATION_ERROR;

  AutomationMsg_NavigationResponseValues navigate_response =
      AUTOMATION_MSG_NAVIGATION_ERROR;
  sender_->Send(new AutomationMsg_GoBackBlockUntilNavigationsComplete(
      handle_, number_of_navigations, &navigate_response));
  return navigate_response;
}

AutomationMsg_NavigationResponseValues TabProxy::GoForward() {
  return GoForwardBlockUntilNavigationsComplete(1);
}

AutomationMsg_NavigationResponseValues
    TabProxy::GoForwardBlockUntilNavigationsComplete(
        int number_of_navigations) {
  if (!is_valid())
    return AUTOMATION_MSG_NAVIGATION_ERROR;

  AutomationMsg_NavigationResponseValues navigate_response =
      AUTOMATION_MSG_NAVIGATION_ERROR;
  sender_->Send(new AutomationMsg_GoForwardBlockUntilNavigationsComplete(
      handle_, number_of_navigations, &navigate_response));
  return navigate_response;
}

AutomationMsg_NavigationResponseValues TabProxy::Reload() {
  if (!is_valid())
    return AUTOMATION_MSG_NAVIGATION_ERROR;

  AutomationMsg_NavigationResponseValues navigate_response =
      AUTOMATION_MSG_NAVIGATION_ERROR;
  sender_->Send(new AutomationMsg_Reload(handle_, &navigate_response));
  return navigate_response;
}

bool TabProxy::GetCurrentURL(GURL* url) const {
  if (!is_valid())
    return false;

  if (!url) {
    NOTREACHED();
    return false;
  }

  bool succeeded = false;
  sender_->Send(new AutomationMsg_TabURL(handle_, &succeeded, url));
  return succeeded;
}

bool TabProxy::NavigateToURLAsync(const GURL& url) {
  if (!is_valid())
    return false;

  bool status = false;
  sender_->Send(new AutomationMsg_NavigationAsync(handle_,
                                                  url,
                                                  &status));
  return status;
}

bool TabProxy::ExecuteAndExtractString(const std::wstring& frame_xpath,
                                       const std::wstring& jscript,
                                       std::wstring* string_value) {
  scoped_ptr<Value> root(ExecuteAndExtractValue(frame_xpath, jscript));
  if (root == NULL)
    return false;

  DCHECK(root->IsType(Value::TYPE_LIST));
  Value* value = NULL;
  bool succeeded = static_cast<ListValue*>(root.get())->Get(0, &value);
  if (succeeded) {
    string16 read_value;
    succeeded = value->GetAsString(&read_value);
    if (succeeded) {
      // TODO(viettrungluu): remove conversion. (But should |jscript| be UTF-8?)
      *string_value = UTF16ToWideHack(read_value);
    }
  }
  return succeeded;
}

bool TabProxy::ExecuteAndExtractBool(const std::wstring& frame_xpath,
                                     const std::wstring& jscript,
                                     bool* bool_value) {
  scoped_ptr<Value> root(ExecuteAndExtractValue(frame_xpath, jscript));
  if (root == NULL)
    return false;

  bool read_value = false;
  DCHECK(root->IsType(Value::TYPE_LIST));
  Value* value = NULL;
  bool succeeded = static_cast<ListValue*>(root.get())->Get(0, &value);
  if (succeeded) {
    succeeded = value->GetAsBoolean(&read_value);
    if (succeeded) {
      *bool_value = read_value;
    }
  }
  return succeeded;
}

bool TabProxy::ExecuteAndExtractInt(const std::wstring& frame_xpath,
                                    const std::wstring& jscript,
                                    int* int_value) {
  scoped_ptr<Value> root(ExecuteAndExtractValue(frame_xpath, jscript));
  if (root == NULL)
    return false;

  int read_value = 0;
  DCHECK(root->IsType(Value::TYPE_LIST));
  Value* value = NULL;
  bool succeeded = static_cast<ListValue*>(root.get())->Get(0, &value);
  if (succeeded) {
    succeeded = value->GetAsInteger(&read_value);
    if (succeeded) {
      *int_value = read_value;
    }
  }
  return succeeded;
}

Value* TabProxy::ExecuteAndExtractValue(const std::wstring& frame_xpath,
                                        const std::wstring& jscript) {
  if (!is_valid())
    return NULL;

  std::string json;
  if (!sender_->Send(new AutomationMsg_DomOperation(handle_, frame_xpath,
                                                    jscript, &json))) {
    return NULL;
  }
  // Wrap |json| in an array before deserializing because valid JSON has an
  // array or an object as the root.
  json.insert(0, "[");
  json.append("]");

  JSONStringValueSerializer deserializer(json);
  return deserializer.Deserialize(NULL, NULL);
}

bool TabProxy::GetCookies(const GURL& url, std::string* cookies) {
  if (!is_valid())
    return false;

  int size = 0;
  return sender_->Send(new AutomationMsg_GetCookies(url, handle_, &size,
                                                    cookies));
}

bool TabProxy::GetCookieByName(const GURL& url,
                               const std::string& name,
                               std::string* cookie) {
  std::string cookies;
  if (!GetCookies(url, &cookies))
    return false;

  std::string namestr = name + "=";
  std::string::size_type idx = cookies.find(namestr);
  if (idx != std::string::npos) {
    cookies.erase(0, idx + namestr.length());
    *cookie = cookies.substr(0, cookies.find(";"));
  } else {
    cookie->clear();
  }

  return true;
}

bool TabProxy::Close() {
  return Close(false);
}

bool TabProxy::Close(bool wait_until_closed) {
  if (!is_valid())
    return false;

  bool succeeded = false;
  sender_->Send(new AutomationMsg_CloseTab(handle_, wait_until_closed,
                                           &succeeded));
  return succeeded;
}

#if defined(OS_WIN)
bool TabProxy::ProcessUnhandledAccelerator(const MSG& msg) {
  if (!is_valid())
    return false;

  return sender_->Send(
      new AutomationMsg_ProcessUnhandledAccelerator(handle_, msg));
  // This message expects no response
}

bool TabProxy::SetInitialFocus(bool reverse, bool restore_focus_to_view) {
  if (!is_valid())
    return false;

  return sender_->Send(
      new AutomationMsg_SetInitialFocus(handle_, reverse,
                                        restore_focus_to_view));
  // This message expects no response
}

AutomationMsg_NavigationResponseValues TabProxy::NavigateInExternalTab(
    const GURL& url, const GURL& referrer) {
  if (!is_valid())
    return AUTOMATION_MSG_NAVIGATION_ERROR;

  AutomationMsg_NavigationResponseValues rv = AUTOMATION_MSG_NAVIGATION_ERROR;
  sender_->Send(new AutomationMsg_NavigateInExternalTab(handle_, url,
                                                        referrer, &rv));
  return rv;
}

AutomationMsg_NavigationResponseValues TabProxy::NavigateExternalTabAtIndex(
    int index) {
  if (!is_valid())
    return AUTOMATION_MSG_NAVIGATION_ERROR;

  AutomationMsg_NavigationResponseValues rv = AUTOMATION_MSG_NAVIGATION_ERROR;
  sender_->Send(new AutomationMsg_NavigateExternalTabAtIndex(handle_, index,
                                                             &rv));
  return rv;
}

void TabProxy::HandleMessageFromExternalHost(const std::string& message,
                                             const std::string& origin,
                                             const std::string& target) {
  if (!is_valid())
    return;

  sender_->Send(
      new AutomationMsg_HandleMessageFromExternalHost(
          handle_, message, origin, target));
}
#endif  // defined(OS_WIN)

bool TabProxy::PrintAsync() {
  if (!is_valid())
    return false;

  return sender_->Send(new AutomationMsg_PrintAsync(handle_));
}

bool TabProxy::WaitForInfoBarCount(size_t target_count) {
  if (!is_valid())
    return false;

  bool success = false;
  return sender_->Send(new AutomationMsg_WaitForInfoBarCount(
      handle_, target_count, &success)) && success;
}

bool TabProxy::OverrideEncoding(const std::string& encoding) {
  if (!is_valid())
    return false;

  bool succeeded = false;
  sender_->Send(new AutomationMsg_OverrideEncoding(handle_, encoding,
                                                   &succeeded));
  return succeeded;
}

#if defined(OS_WIN)
void TabProxy::Reposition(HWND window, HWND window_insert_after, int left,
                          int top, int width, int height, int flags,
                          HWND parent_window) {
  Reposition_Params params;
  params.window = window;
  params.window_insert_after = window_insert_after;
  params.left = left;
  params.top = top;
  params.width = width;
  params.height = height;
  params.flags = flags;
  params.set_parent = (::IsWindow(parent_window) ? true : false);
  params.parent_window = parent_window;
  sender_->Send(new AutomationMsg_TabReposition(handle_, params));
}

void TabProxy::SendContextMenuCommand(int selected_command) {
  sender_->Send(new AutomationMsg_ForwardContextMenuCommandToChrome(
      handle_, selected_command));
}

void TabProxy::OnHostMoved() {
  sender_->Send(new AutomationMsg_BrowserMove(handle_));
}
#endif  // defined(OS_WIN)

void TabProxy::SelectAll() {
  sender_->Send(new AutomationMsg_SelectAll(handle_));
}

void TabProxy::Cut() {
  sender_->Send(new AutomationMsg_Cut(handle_));
}

void TabProxy::Copy() {
  sender_->Send(new AutomationMsg_Copy(handle_));
}

void TabProxy::Paste() {
  sender_->Send(new AutomationMsg_Paste(handle_));
}

void TabProxy::ReloadAsync() {
  sender_->Send(new AutomationMsg_ReloadAsync(handle_));
}

void TabProxy::StopAsync() {
  sender_->Send(new AutomationMsg_StopAsync(handle_));
}

void TabProxy::SaveAsAsync() {
  sender_->Send(new AutomationMsg_SaveAsAsync(handle_));
}

void TabProxy::JavaScriptStressTestControl(int cmd, int param) {
  if (!is_valid())
    return;

  sender_->Send(new AutomationMsg_JavaScriptStressTestControl(
      handle_, cmd, param));
}

void TabProxy::AddObserver(TabProxyDelegate* observer) {
  base::AutoLock lock(list_lock_);
  observers_list_.AddObserver(observer);
}

void TabProxy::RemoveObserver(TabProxyDelegate* observer) {
  base::AutoLock lock(list_lock_);
  observers_list_.RemoveObserver(observer);
}

// Called on Channel background thread, if TabMessages filter is installed.
bool TabProxy::OnMessageReceived(const IPC::Message& message) {
  base::AutoLock lock(list_lock_);
  FOR_EACH_OBSERVER(TabProxyDelegate, observers_list_,
                    OnMessageReceived(this, message));
  return true;
}

void TabProxy::OnChannelError() {
  base::AutoLock lock(list_lock_);
  FOR_EACH_OBSERVER(TabProxyDelegate, observers_list_, OnChannelError(this));
}

TabProxy::~TabProxy() {}

void TabProxy::FirstObjectAdded() {
  AddRef();
}

void TabProxy::LastObjectRemoved() {
  Release();
}
