// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome_frame/test/ie_event_sink.h"

#include <shlguid.h>
#include <shobjidl.h>

#include <map>
#include <utility>

#include "base/lazy_instance.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/test_timeouts.h"
#include "base/time/time.h"
#include "base/win/scoped_bstr.h"
#include "base/win/scoped_handle.h"
#include "base/win/scoped_variant.h"
#include "chrome_frame/test/chrome_frame_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::win::ScopedBstr;

namespace {

// A lookup table from DISPID to DWebBrowserEvents and/or DWebBrowserEvents2
// method name.
class DispIdNameTable {
 public:
  DispIdNameTable();
  ~DispIdNameTable();

  // Returns the method name corresponding to |dispid| or, if none is known,
  // the string "DISPID |dispid|".
  std::string Lookup(DISPID dispid) const;

 private:
  std::map<DISPID,const char*> dispid_to_name_;
  DISALLOW_COPY_AND_ASSIGN(DispIdNameTable);
};

DispIdNameTable::DispIdNameTable() {
  static const struct {
    DISPID dispid;
    const char* name;
  } kIdToName[] = {
    // DWebBrowserEvents
    { 100, "BeforeNavigate" },
    { 101, "NavigateComplete" },
    { 102, "StatusTextChange" },
    { 108, "ProgressChange" },
    { 104, "DownloadComplete" },
    { 105, "CommandStateChange" },
    { 106, "DownloadBegin" },
    { 107, "NewWindow" },
    { 113, "TitleChange" },
    { 200, "FrameBeforeNavigate" },
    { 201, "FrameNavigateComplete" },
    { 204, "FrameNewWindow" },
    { 103, "Quit" },
    { 109, "WindowMove" },
    { 110, "WindowResize" },
    { 111, "WindowActivate" },
    { 112, "PropertyChange" },
    // DWebBrowserEvents2
    { 250, "BeforeNavigate2" },
    { 251, "NewWindow2" },
    { 252, "NavigateComplete2" },
    { 259, "DocumentComplete" },
    { 253, "OnQuit" },
    { 254, "OnVisible" },
    { 255, "OnToolBar" },
    { 256, "OnMenuBar" },
    { 257, "OnStatusBar" },
    { 258, "OnFullScreen" },
    { 260, "OnTheaterMode" },
    { 262, "WindowSetResizable" },
    { 264, "WindowSetLeft" },
    { 265, "WindowSetTop" },
    { 266, "WindowSetWidth" },
    { 267, "WindowSetHeight" },
    { 263, "WindowClosing" },
    { 268, "ClientToHostWindow" },
    { 269, "SetSecureLockIcon" },
    { 270, "FileDownload" },
    { 271, "NavigateError" },
    { 225, "PrintTemplateInstantiation" },
    { 226, "PrintTemplateTeardown" },
    { 227, "UpdatePageStatus" },
    { 272, "PrivacyImpactedStateChange" },
    { 273, "NewWindow3" },
    { 282, "SetPhishingFilterStatus" },
    { 283, "WindowStateChanged" },
    { 284, "NewProcess" },
    { 285, "ThirdPartyUrlBlocked" },
    { 286, "RedirectXDomainBlocked" },
    // Present in ExDispid.h but not ExDisp.idl
    { 114, "TitleIconChange" },
    { 261, "OnAddressBar" },
    { 281, "ViewUpdate" },
  };
  size_t index_of_duplicate = 0;
  DISPID duplicate_dispid = 0;
  for (size_t i = 0; i < arraysize(kIdToName); ++i) {
    if (!dispid_to_name_.insert(std::make_pair(kIdToName[i].dispid,
                                               kIdToName[i].name)).second &&
        index_of_duplicate == 0) {
      index_of_duplicate = i;
      duplicate_dispid = kIdToName[i].dispid;
    }
  }
  DCHECK_EQ(static_cast<size_t>(0), index_of_duplicate)
      << "Duplicate name for DISPID " << duplicate_dispid
      << " at kIdToName[" << index_of_duplicate << "]";
}

DispIdNameTable::~DispIdNameTable() {
}

std::string DispIdNameTable::Lookup(DISPID dispid) const {
  std::map<DISPID,const char*>::const_iterator it =
      dispid_to_name_.find(dispid);
  if (it != dispid_to_name_.end())
    return it->second;
  return std::string("DISPID ").append(base::IntToString(dispid));
}

base::LazyInstance<DispIdNameTable> g_dispIdToName = LAZY_INSTANCE_INITIALIZER;

}  // namespace

namespace chrome_frame_test {

const int kDefaultWaitForIEToTerminateMs = 10 * 1000;

_ATL_FUNC_INFO IEEventSink::kNavigateErrorInfo = {
  CC_STDCALL, VT_EMPTY, 5, {
    VT_DISPATCH,
    VT_VARIANT | VT_BYREF,
    VT_VARIANT | VT_BYREF,
    VT_VARIANT | VT_BYREF,
    VT_BOOL | VT_BYREF,
  }
};

_ATL_FUNC_INFO IEEventSink::kNavigateComplete2Info = {
  CC_STDCALL, VT_EMPTY, 2, {
    VT_DISPATCH,
    VT_VARIANT | VT_BYREF
  }
};

_ATL_FUNC_INFO IEEventSink::kBeforeNavigate2Info = {
  CC_STDCALL, VT_EMPTY, 7, {
    VT_DISPATCH,
    VT_VARIANT | VT_BYREF,
    VT_VARIANT | VT_BYREF,
    VT_VARIANT | VT_BYREF,
    VT_VARIANT | VT_BYREF,
    VT_VARIANT | VT_BYREF,
    VT_BOOL | VT_BYREF
  }
};

_ATL_FUNC_INFO IEEventSink::kNewWindow2Info = {
  CC_STDCALL, VT_EMPTY, 2, {
    VT_DISPATCH | VT_BYREF,
    VT_BOOL | VT_BYREF,
  }
};

_ATL_FUNC_INFO IEEventSink::kNewWindow3Info = {
  CC_STDCALL, VT_EMPTY, 5, {
    VT_DISPATCH | VT_BYREF,
    VT_BOOL | VT_BYREF,
    VT_UINT,
    VT_BSTR,
    VT_BSTR
  }
};

_ATL_FUNC_INFO IEEventSink::kVoidMethodInfo = {
    CC_STDCALL, VT_EMPTY, 0, {NULL}};

_ATL_FUNC_INFO IEEventSink::kDocumentCompleteInfo = {
  CC_STDCALL, VT_EMPTY, 2, {
    VT_DISPATCH,
    VT_VARIANT | VT_BYREF
  }
};

_ATL_FUNC_INFO IEEventSink::kFileDownloadInfo = {
  CC_STDCALL, VT_EMPTY, 2, {
    VT_BOOL,
    VT_BOOL | VT_BYREF
  }
};

bool IEEventSink::abnormal_shutdown_ = false;

IEEventSink::IEEventSink()
    : onmessage_(this, &IEEventSink::OnMessage),
      onloaderror_(this, &IEEventSink::OnLoadError),
      onload_(this, &IEEventSink::OnLoad),
      listener_(NULL),
      ie_process_id_(0),
      did_receive_on_quit_(false) {
}

IEEventSink::~IEEventSink() {
  Uninitialize();
}

void IEEventSink::SetAbnormalShutdown(bool abnormal_shutdown) {
  abnormal_shutdown_ = abnormal_shutdown;
}

// IEEventSink member defines
void IEEventSink::Attach(IDispatch* browser_disp) {
  EXPECT_TRUE(NULL != browser_disp);
  if (browser_disp) {
    EXPECT_HRESULT_SUCCEEDED(web_browser2_.QueryFrom(browser_disp));
    EXPECT_HRESULT_SUCCEEDED(Attach(web_browser2_.get()));
  }
}

HRESULT IEEventSink::Attach(IWebBrowser2* browser) {
  DCHECK(browser);
  HRESULT result;
  if (browser) {
    web_browser2_ = browser;
    FindIEProcessId();
    result = DispEventAdvise(web_browser2_, &DIID_DWebBrowserEvents2);
  }
  return result;
}

void IEEventSink::Uninitialize() {
  if (!abnormal_shutdown_) {
    DisconnectFromChromeFrame();
    if (web_browser2_.get()) {
      if (m_dwEventCookie != 0xFEFEFEFE) {
        DispEventUnadvise(web_browser2_);
        CoDisconnectObject(this, 0);
      }

      if (!did_receive_on_quit_) {
        // Log the browser window url for debugging purposes.
        ScopedBstr browser_url;
        web_browser2_->get_LocationURL(browser_url.Receive());
        std::wstring browser_url_wstring;
        browser_url_wstring.assign(browser_url, browser_url.Length());
        std::string browser_url_string = WideToUTF8(browser_url_wstring);
        LOG(ERROR) << "OnQuit was not received for browser with url "
                   << browser_url_string;
        web_browser2_->Quit();
      }

      base::win::ScopedHandle process;
      process.Set(OpenProcess(SYNCHRONIZE, FALSE, ie_process_id_));
      web_browser2_.Release();

      if (!process.IsValid()) {
        LOG_IF(WARNING, !process.IsValid())
            << base::StringPrintf("OpenProcess failed: %i", ::GetLastError());
        return;
      }
      // IE may not have closed yet. Wait here for the process to finish.
      // This is necessary at least on some browser/platform configurations.
      WaitForSingleObject(process, kDefaultWaitForIEToTerminateMs);
    }
  } else {
    LOG(ERROR) << "Terminating hung IE process";
  }
  chrome_frame_test::KillProcesses(chrome_frame_test::kIEImageName, 0,
                                   !abnormal_shutdown_);
  chrome_frame_test::KillProcesses(chrome_frame_test::kIEBrokerImageName, 0,
                                   !abnormal_shutdown_);
}

bool IEEventSink::IsCFRendering() {
  DCHECK(web_browser2_);

  if (web_browser2_) {
    base::win::ScopedComPtr<IDispatch> doc;
    web_browser2_->get_Document(doc.Receive());
    if (doc) {
      // Detect if CF is rendering based on whether the document is a
      // ChromeActiveDocument. Detecting based on hwnd is problematic as
      // the CF Active Document window may not have been created yet.
      base::win::ScopedComPtr<IChromeFrame> chrome_frame;
      chrome_frame.QueryFrom(doc);
      return chrome_frame.get();
    }
  }
  return false;
}

void IEEventSink::PostMessageToCF(const std::wstring& message,
                                  const std::wstring& target) {
  EXPECT_TRUE(chrome_frame_ != NULL);
  if (!chrome_frame_)
    return;
  ScopedBstr message_bstr(message.c_str());
  base::win::ScopedVariant target_variant(target.c_str());
  EXPECT_HRESULT_SUCCEEDED(
      chrome_frame_->postMessage(message_bstr, target_variant));
}

void IEEventSink::SetFocusToRenderer() {
  simulate_input::SetKeyboardFocusToWindow(GetRendererWindow());
}

void IEEventSink::SendKeys(const char* input_string) {
  HWND window = GetRendererWindow();
  simulate_input::SetKeyboardFocusToWindow(window);
  const base::TimeDelta kMessageSleep = TestTimeouts::tiny_timeout();
  const base::StringPiece codes(input_string);
  for (size_t i = 0; i < codes.length(); ++i) {
    char character = codes[i];
    UINT virtual_key = 0;

    if (character >= 'a' && character <= 'z') {
      // VK_A - VK_Z are ASCII 'A' - 'Z'.
      virtual_key = 'A' + (character - 'a');
    } else if (character >= '0' && character <= '9') {
      // VK_0 - VK_9 are ASCII '0' - '9'.
      virtual_key = character;
    } else {
      FAIL() << "Character value out of range at position " << i
             << " of string \"" << input_string << "\"";
    }

    UINT scan_code = MapVirtualKey(virtual_key, MAPVK_VK_TO_VSC);
    EXPECT_NE(0U, scan_code) << "No translation for virtual key "
                             << virtual_key << " for character at position "
                             << i << " of string \"" << input_string << "\"";

    ::PostMessage(window, WM_KEYDOWN,
                  virtual_key, MAKELPARAM(1, scan_code));
    base::PlatformThread::Sleep(kMessageSleep);
    ::PostMessage(window, WM_KEYUP,
                  virtual_key, MAKELPARAM(1, scan_code | KF_UP | KF_REPEAT));
    base::PlatformThread::Sleep(kMessageSleep);
  }
}

void IEEventSink::SendMouseClick(int x, int y,
                                 simulate_input::MouseButton button) {
  simulate_input::SendMouseClick(GetRendererWindow(), x, y, button);
}

void IEEventSink::ExpectRendererWindowHasFocus() {
  HWND renderer_window = GetRendererWindow();
  EXPECT_TRUE(IsWindow(renderer_window));

  DWORD renderer_thread = 0;
  DWORD renderer_process = 0;
  renderer_thread = GetWindowThreadProcessId(renderer_window,
                                             &renderer_process);

  ASSERT_TRUE(AttachThreadInput(GetCurrentThreadId(), renderer_thread, TRUE));
  HWND focus_window = GetFocus();
  EXPECT_EQ(renderer_window, focus_window);
  EXPECT_TRUE(AttachThreadInput(GetCurrentThreadId(), renderer_thread, FALSE));
}

void IEEventSink::ExpectAddressBarUrl(
    const std::wstring& expected_url) {
  DCHECK(web_browser2_);
  if (web_browser2_) {
    ScopedBstr address_bar_url;
    EXPECT_EQ(S_OK, web_browser2_->get_LocationURL(address_bar_url.Receive()));
    EXPECT_EQ(expected_url, std::wstring(address_bar_url));
  }
}

void IEEventSink::Exec(const GUID* cmd_group_guid, DWORD command_id,
                               DWORD cmd_exec_opt, VARIANT* in_args,
                               VARIANT* out_args) {
  base::win::ScopedComPtr<IOleCommandTarget> shell_browser_cmd_target;
  DoQueryService(SID_STopLevelBrowser, web_browser2_,
                 shell_browser_cmd_target.Receive());
  ASSERT_TRUE(NULL != shell_browser_cmd_target);
  EXPECT_HRESULT_SUCCEEDED(shell_browser_cmd_target->Exec(cmd_group_guid,
      command_id, cmd_exec_opt, in_args, out_args));
}

HWND IEEventSink::GetBrowserWindow() {
  HWND browser_window = NULL;
  web_browser2_->get_HWND(reinterpret_cast<SHANDLE_PTR*>(&browser_window));
  EXPECT_TRUE(::IsWindow(browser_window));
  return browser_window;
}

HWND IEEventSink::GetRendererWindow() {
  HWND renderer_window = NULL;
  if (IsCFRendering()) {
    DCHECK(chrome_frame_);
    base::win::ScopedComPtr<IOleWindow> ole_window;
    ole_window.QueryFrom(chrome_frame_);
    EXPECT_TRUE(ole_window.get());

    if (ole_window) {
      HWND activex_window = NULL;
      ole_window->GetWindow(&activex_window);
      EXPECT_TRUE(IsWindow(activex_window));

      wchar_t class_name[MAX_PATH] = {0};
      HWND child_window = NULL;
      // chrome tab window is the first (and the only) child of activex
      for (HWND first_child = activex_window; ::IsWindow(first_child);
           first_child = ::GetWindow(first_child, GW_CHILD)) {
        child_window = first_child;
        GetClassName(child_window, class_name, arraysize(class_name));
#if defined(USE_AURA)
        static const wchar_t kWndClassPrefix[] = L"Chrome_WidgetWin_";
#else
        static const wchar_t kWndClassPrefix[] = L"Chrome_RenderWidgetHostHWND";
#endif
        if (!_wcsnicmp(class_name, kWndClassPrefix, wcslen(kWndClassPrefix))) {
          renderer_window = child_window;
          break;
        }
      }
    }
  } else {
    DCHECK(web_browser2_);
    base::win::ScopedComPtr<IDispatch> doc;
    HRESULT hr = web_browser2_->get_Document(doc.Receive());
    EXPECT_HRESULT_SUCCEEDED(hr);
    EXPECT_TRUE(doc);
    if (doc) {
      base::win::ScopedComPtr<IOleWindow> ole_window;
      ole_window.QueryFrom(doc);
      EXPECT_TRUE(ole_window);
      if (ole_window) {
        ole_window->GetWindow(&renderer_window);
      }
    }
  }

  EXPECT_TRUE(::IsWindow(renderer_window));
  return renderer_window;
}

HWND IEEventSink::GetRendererWindowSafe() {
  HWND renderer_window = NULL;
  if (IsCFRendering()) {
    DCHECK(chrome_frame_);
    base::win::ScopedComPtr<IOleWindow> ole_window;
    ole_window.QueryFrom(chrome_frame_);

    if (ole_window) {
      HWND activex_window = NULL;
      ole_window->GetWindow(&activex_window);

      // chrome tab window is the first (and the only) child of activex
      for (HWND first_child = activex_window; ::IsWindow(first_child);
           first_child = ::GetWindow(first_child, GW_CHILD)) {
        renderer_window = first_child;
      }
      wchar_t class_name[MAX_PATH] = {0};
      GetClassName(renderer_window, class_name, arraysize(class_name));
      if (_wcsicmp(class_name, L"Chrome_RenderWidgetHostHWND") != 0)
        renderer_window = NULL;
    }
  } else {
    DCHECK(web_browser2_);
    base::win::ScopedComPtr<IDispatch> doc;
    web_browser2_->get_Document(doc.Receive());
    if (doc) {
      base::win::ScopedComPtr<IOleWindow> ole_window;
      ole_window.QueryFrom(doc);
      if (ole_window) {
        ole_window->GetWindow(&renderer_window);
      }
    }
  }
  if (!::IsWindow(renderer_window))
    renderer_window = NULL;
  return renderer_window;
}

HRESULT IEEventSink::LaunchIEAndNavigate(const std::wstring& navigate_url,
                                         IEEventListener* listener) {
  listener_ = listener;
  HRESULT hr = LaunchIEAsComServer(web_browser2_.Receive());
  if (SUCCEEDED(hr)) {
    web_browser2_->put_Visible(VARIANT_TRUE);
    hr = Attach(web_browser2_);
    if (SUCCEEDED(hr)) {
      hr = Navigate(navigate_url);
      if (FAILED(hr)) {
        LOG(ERROR) << "Failed to navigate IE to " << navigate_url << ", hr = 0x"
                   << std::hex << hr;
      }
    } else {
      LOG(ERROR) << "Failed to attach to web browser event sink for "
                 << navigate_url << ", hr = 0x" << std::hex << hr;
    }
  } else {
    LOG(ERROR) << "Failed to Launch IE for " << navigate_url << ", hr = 0x"
               << std::hex << hr;
  }

  return hr;
}

HRESULT IEEventSink::Navigate(const std::wstring& navigate_url) {
  VARIANT empty = base::win::ScopedVariant::kEmptyVariant;
  base::win::ScopedVariant url;
  url.Set(navigate_url.c_str());

  HRESULT hr = S_OK;
  hr = web_browser2_->Navigate2(url.AsInput(), &empty, &empty, &empty, &empty);
  EXPECT_TRUE(hr == S_OK);
  return hr;
}

HRESULT IEEventSink::CloseWebBrowser() {
  if (!web_browser2_)
    return E_FAIL;

  DisconnectFromChromeFrame();
  EXPECT_HRESULT_SUCCEEDED(web_browser2_->Quit());
  return S_OK;
}

void IEEventSink::Refresh() {
  base::win::ScopedVariant refresh_level(REFRESH_COMPLETELY);
  web_browser2_->Refresh2(refresh_level.AsInput());
}

// private methods
void IEEventSink::ConnectToChromeFrame() {
  DCHECK(web_browser2_);
  if (chrome_frame_.get())
    return;
  base::win::ScopedComPtr<IShellBrowser> shell_browser;
  DoQueryService(SID_STopLevelBrowser, web_browser2_,
                 shell_browser.Receive());

  if (shell_browser) {
    base::win::ScopedComPtr<IShellView> shell_view;
    shell_browser->QueryActiveShellView(shell_view.Receive());
    if (shell_view) {
      shell_view->GetItemObject(SVGIO_BACKGROUND, __uuidof(IChromeFrame),
           reinterpret_cast<void**>(chrome_frame_.Receive()));
    }

    if (chrome_frame_) {
      base::win::ScopedVariant onmessage(onmessage_.ToDispatch());
      base::win::ScopedVariant onloaderror(onloaderror_.ToDispatch());
      base::win::ScopedVariant onload(onload_.ToDispatch());
      EXPECT_HRESULT_SUCCEEDED(chrome_frame_->put_onmessage(onmessage));
      EXPECT_HRESULT_SUCCEEDED(chrome_frame_->put_onloaderror(onloaderror));
      EXPECT_HRESULT_SUCCEEDED(chrome_frame_->put_onload(onload));
    }
  }
}

void IEEventSink::DisconnectFromChromeFrame() {
  if (chrome_frame_) {
    // Use a local ref counted copy of the IChromeFrame interface as the
    // outgoing calls could cause the interface to be deleted due to a message
    // pump running in the context of the outgoing call.
    base::win::ScopedComPtr<IChromeFrame> chrome_frame(chrome_frame_);
    chrome_frame_.Release();
    base::win::ScopedVariant dummy(static_cast<IDispatch*>(NULL));
    chrome_frame->put_onmessage(dummy);
    chrome_frame->put_onload(dummy);
    chrome_frame->put_onloaderror(dummy);
  }
}

void IEEventSink::FindIEProcessId() {
  HWND hwnd = NULL;
  web_browser2_->get_HWND(reinterpret_cast<SHANDLE_PTR*>(&hwnd));
  EXPECT_TRUE(::IsWindow(hwnd));
  if (::IsWindow(hwnd))
    ::GetWindowThreadProcessId(hwnd, &ie_process_id_);
  EXPECT_NE(static_cast<DWORD>(0), ie_process_id_);
}

// Event callbacks
STDMETHODIMP_(void) IEEventSink::OnDownloadBegin() {
  if (listener_)
    listener_->OnDownloadBegin();
}

STDMETHODIMP_(void) IEEventSink::OnNewWindow2(IDispatch** dispatch,
                                              VARIANT_BOOL* s) {
  VLOG(1) << __FUNCTION__;

  EXPECT_TRUE(dispatch);
  if (!dispatch)
    return;

  if (listener_)
    listener_->OnNewWindow2(dispatch, s);

  // Note that |dispatch| is an [in/out] argument. IE is asking listeners if
  // they want to use a IWebBrowser2 of their choice for the new window.
  // Since we need to listen on events on the new browser, we create one
  // if needed.
  if (!*dispatch) {
    base::win::ScopedComPtr<IDispatch> new_browser;
    HRESULT hr = new_browser.CreateInstance(CLSID_InternetExplorer, NULL,
                                            CLSCTX_LOCAL_SERVER);
    DCHECK(SUCCEEDED(hr) && new_browser);
    *dispatch = new_browser.Detach();
  }

  if (*dispatch && listener_)
    listener_->OnNewBrowserWindow(*dispatch, ScopedBstr());
}

STDMETHODIMP_(void) IEEventSink::OnNavigateError(IDispatch* dispatch,
    VARIANT* url, VARIANT* frame_name, VARIANT* status_code, VARIANT* cancel) {
  VLOG(1) << __FUNCTION__;
  if (listener_)
    listener_->OnNavigateError(dispatch, url, frame_name, status_code, cancel);
}

STDMETHODIMP IEEventSink::OnBeforeNavigate2(
    IDispatch* dispatch, VARIANT* url, VARIANT* flags,
    VARIANT* target_frame_name, VARIANT* post_data, VARIANT* headers,
    VARIANT_BOOL* cancel) {
  VLOG(1) << __FUNCTION__ << " "
          << base::StringPrintf("%ls - 0x%08X", url->bstrVal, this);
  // Reset any existing reference to chrome frame since this is a new
  // navigation.
  DisconnectFromChromeFrame();
  if (listener_)
    listener_->OnBeforeNavigate2(dispatch, url, flags, target_frame_name,
                                 post_data, headers, cancel);
  return S_OK;
}

STDMETHODIMP_(void) IEEventSink::OnNavigateComplete2(
    IDispatch* dispatch, VARIANT* url) {
  VLOG(1) << __FUNCTION__;
  ConnectToChromeFrame();
  if (listener_)
    listener_->OnNavigateComplete2(dispatch, url);
}

STDMETHODIMP_(void) IEEventSink::OnDocumentComplete(
    IDispatch* dispatch, VARIANT* url) {
  VLOG(1) << __FUNCTION__;
  EXPECT_TRUE(url);
  if (!url)
    return;
  if (listener_)
    listener_->OnDocumentComplete(dispatch, url);
}

STDMETHODIMP_(void) IEEventSink::OnFileDownload(
    VARIANT_BOOL active_doc, VARIANT_BOOL* cancel) {
  VLOG(1) << __FUNCTION__ << " "
          << base::StringPrintf(" 0x%08X ad=%i", this, active_doc);
  if (listener_) {
    listener_->OnFileDownload(active_doc, cancel);
  } else {
    *cancel = VARIANT_TRUE;
  }
}

STDMETHODIMP_(void) IEEventSink::OnNewWindow3(
    IDispatch** dispatch, VARIANT_BOOL* cancel, DWORD flags, BSTR url_context,
    BSTR url) {
  VLOG(1) << __FUNCTION__;
  EXPECT_TRUE(dispatch);
  if (!dispatch)
    return;

  if (listener_)
    listener_->OnNewWindow3(dispatch, cancel, flags, url_context, url);

  // Note that |dispatch| is an [in/out] argument. IE is asking listeners if
  // they want to use a IWebBrowser2 of their choice for the new window.
  // Since we need to listen on events on the new browser, we create one
  // if needed.
  if (!*dispatch) {
    base::win::ScopedComPtr<IDispatch> new_browser;
    HRESULT hr = new_browser.CreateInstance(CLSID_InternetExplorer, NULL,
                                            CLSCTX_LOCAL_SERVER);
    DCHECK(SUCCEEDED(hr) && new_browser);
    *dispatch = new_browser.Detach();
  }

  if (*dispatch && listener_)
    listener_->OnNewBrowserWindow(*dispatch, url);
}

STDMETHODIMP_(void) IEEventSink::OnQuit() {
  VLOG(1) << __FUNCTION__;

  did_receive_on_quit_ = true;

  DispEventUnadvise(web_browser2_);
  CoDisconnectObject(this, 0);

  if (listener_)
    listener_->OnQuit();
}

STDMETHODIMP IEEventSink::Invoke(DISPID dispid, REFIID riid, LCID lcid,
                                 WORD flags, DISPPARAMS* params,
                                 VARIANT* result, EXCEPINFO* except_info,
                                 UINT* arg_error) {
  VLOG(1) << __FUNCTION__ << L" event: " << g_dispIdToName.Get().Lookup(dispid);
  return DispEventsImpl::Invoke(dispid, riid, lcid, flags, params, result,
                                except_info, arg_error);
}

HRESULT IEEventSink::OnLoad(const VARIANT* param) {
  VLOG(1) << __FUNCTION__ << " " << param->bstrVal;
  base::win::ScopedVariant stack_object(*param);
  if (chrome_frame_) {
    if (listener_)
      listener_->OnLoad(param->bstrVal);
  } else {
    LOG(WARNING) << "Invalid chrome frame pointer";
  }
  return S_OK;
}

HRESULT IEEventSink::OnLoadError(const VARIANT* param) {
  VLOG(1) << __FUNCTION__ << " " << param->bstrVal;
  if (chrome_frame_) {
    if (listener_)
      listener_->OnLoadError(param->bstrVal);
  } else {
    LOG(WARNING) << "Invalid chrome frame pointer";
  }
  return S_OK;
}

HRESULT IEEventSink::OnMessage(const VARIANT* param) {
  VLOG(1) << __FUNCTION__ << " " << param;
  if (!chrome_frame_.get()) {
    LOG(WARNING) << "Invalid chrome frame pointer";
    return S_OK;
  }

  base::win::ScopedVariant data, origin, source;
  if (param && (V_VT(param) == VT_DISPATCH)) {
    wchar_t* properties[] = { L"data", L"origin", L"source" };
    const int prop_count = arraysize(properties);
    DISPID ids[prop_count] = {0};

    HRESULT hr = param->pdispVal->GetIDsOfNames(IID_NULL, properties,
        prop_count, LOCALE_SYSTEM_DEFAULT, ids);
    if (SUCCEEDED(hr)) {
      DISPPARAMS params = { 0 };
      EXPECT_HRESULT_SUCCEEDED(param->pdispVal->Invoke(ids[0], IID_NULL,
          LOCALE_SYSTEM_DEFAULT, DISPATCH_PROPERTYGET, &params,
          data.Receive(), NULL, NULL));
      EXPECT_HRESULT_SUCCEEDED(param->pdispVal->Invoke(ids[1], IID_NULL,
          LOCALE_SYSTEM_DEFAULT, DISPATCH_PROPERTYGET, &params,
          origin.Receive(), NULL, NULL));
      EXPECT_HRESULT_SUCCEEDED(param->pdispVal->Invoke(ids[2], IID_NULL,
          LOCALE_SYSTEM_DEFAULT, DISPATCH_PROPERTYGET, &params,
          source.Receive(), NULL, NULL));
    }
  }

  if (listener_)
    listener_->OnMessage(V_BSTR(&data), V_BSTR(&origin), V_BSTR(&source));
  return S_OK;
}

}  // namespace chrome_frame_test
