// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// IPC messages for extensions.
// Multiply-included message file, hence no include guard.

#include <string>
#include <vector>

#include "base/memory/shared_memory.h"
#include "base/values.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/permissions/bluetooth_permission_data.h"
#include "chrome/common/extensions/permissions/media_galleries_permission_data.h"
#include "chrome/common/extensions/permissions/permission_set.h"
#include "chrome/common/extensions/permissions/socket_permission_data.h"
#include "chrome/common/extensions/permissions/usb_device_permission_data.h"
#include "chrome/common/web_application_info.h"
#include "content/public/common/common_param_traits.h"
#include "content/public/common/socket_permission_request.h"
#include "extensions/common/draggable_region.h"
#include "extensions/common/url_pattern.h"
#include "extensions/common/url_pattern_set.h"
#include "extensions/common/view_type.h"
#include "ipc/ipc_message_macros.h"
#include "url/gurl.h"

#define IPC_MESSAGE_START ExtensionMsgStart

IPC_ENUM_TRAITS(extensions::ViewType)

// Parameters structure for ExtensionHostMsg_AddAPIActionToActivityLog and
// ExtensionHostMsg_AddEventToActivityLog.
IPC_STRUCT_BEGIN(ExtensionHostMsg_APIActionOrEvent_Params)
  // API name.
  IPC_STRUCT_MEMBER(std::string, api_call)

  // List of arguments.
  IPC_STRUCT_MEMBER(base::ListValue, arguments)

  // Extra logging information.
  IPC_STRUCT_MEMBER(std::string, extra)
IPC_STRUCT_END()

// Parameters structure for ExtensionHostMsg_AddDOMActionToActivityLog.
IPC_STRUCT_BEGIN(ExtensionHostMsg_DOMAction_Params)
  // URL of the page.
  IPC_STRUCT_MEMBER(GURL, url)

  // Title of the page.
  IPC_STRUCT_MEMBER(string16, url_title)

  // API name.
  IPC_STRUCT_MEMBER(std::string, api_call)

  // List of arguments.
  IPC_STRUCT_MEMBER(base::ListValue, arguments)

  // Type of DOM API call.
  IPC_STRUCT_MEMBER(int, call_type)
IPC_STRUCT_END()

// Parameters structure for ExtensionHostMsg_Request.
IPC_STRUCT_BEGIN(ExtensionHostMsg_Request_Params)
  // Message name.
  IPC_STRUCT_MEMBER(std::string, name)

  // List of message arguments.
  IPC_STRUCT_MEMBER(base::ListValue, arguments)

  // Extension ID this request was sent from. This can be empty, in the case
  // where we expose APIs to normal web pages using the extension function
  // system.
  IPC_STRUCT_MEMBER(std::string, extension_id)

  // URL of the frame the request was sent from. This isn't necessarily an
  // extension url. Extension requests can also originate from content scripts,
  // in which case extension_id will indicate the ID of the associated
  // extension. Or, they can originate from hosted apps or normal web pages.
  IPC_STRUCT_MEMBER(GURL, source_url)

  // Unique request id to match requests and responses.
  IPC_STRUCT_MEMBER(int, request_id)

  // True if request has a callback specified.
  IPC_STRUCT_MEMBER(bool, has_callback)

  // True if request is executed in response to an explicit user gesture.
  IPC_STRUCT_MEMBER(bool, user_gesture)
IPC_STRUCT_END()

// Allows an extension to execute code in a tab.
IPC_STRUCT_BEGIN(ExtensionMsg_ExecuteCode_Params)
  // The extension API request id, for responding.
  IPC_STRUCT_MEMBER(int, request_id)

  // The ID of the requesting extension. To know which isolated world to
  // execute the code inside of.
  IPC_STRUCT_MEMBER(std::string, extension_id)

  // Whether the code is JavaScript or CSS.
  IPC_STRUCT_MEMBER(bool, is_javascript)

  // String of code to execute.
  IPC_STRUCT_MEMBER(std::string, code)

  // Whether to inject into all frames, or only the root frame.
  IPC_STRUCT_MEMBER(bool, all_frames)

  // When to inject the code.
  IPC_STRUCT_MEMBER(int, run_at)

  // Whether to execute code in the main world (as opposed to an isolated
  // world).
  IPC_STRUCT_MEMBER(bool, in_main_world)

  // Whether the request is coming from a <webview>.
  IPC_STRUCT_MEMBER(bool, is_web_view)
IPC_STRUCT_END()

// Struct containing the data for external connections to extensions. Used to
// handle the IPCs initiated by both connect() and onConnect().
IPC_STRUCT_BEGIN(ExtensionMsg_ExternalConnectionInfo)
  // The ID of the extension that is the target of the request.
  IPC_STRUCT_MEMBER(std::string, target_id)

  // The ID of the extension that initiated the request. May be empty if it
  // wasn't initiated by an extension.
  IPC_STRUCT_MEMBER(std::string, source_id)

  // The URL of the frame that initiated the request.
  IPC_STRUCT_MEMBER(GURL, source_url)
IPC_STRUCT_END()

IPC_STRUCT_TRAITS_BEGIN(WebApplicationInfo::IconInfo)
  IPC_STRUCT_TRAITS_MEMBER(url)
  IPC_STRUCT_TRAITS_MEMBER(width)
  IPC_STRUCT_TRAITS_MEMBER(height)
  IPC_STRUCT_TRAITS_MEMBER(data)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(WebApplicationInfo)
  IPC_STRUCT_TRAITS_MEMBER(title)
  IPC_STRUCT_TRAITS_MEMBER(description)
  IPC_STRUCT_TRAITS_MEMBER(app_url)
  IPC_STRUCT_TRAITS_MEMBER(icons)
  IPC_STRUCT_TRAITS_MEMBER(permissions)
  IPC_STRUCT_TRAITS_MEMBER(launch_container)
  IPC_STRUCT_TRAITS_MEMBER(is_offline_enabled)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(extensions::DraggableRegion)
  IPC_STRUCT_TRAITS_MEMBER(draggable)
  IPC_STRUCT_TRAITS_MEMBER(bounds)
IPC_STRUCT_TRAITS_END()

IPC_ENUM_TRAITS(content::SocketPermissionRequest::OperationType)

IPC_STRUCT_TRAITS_BEGIN(content::SocketPermissionRequest)
  IPC_STRUCT_TRAITS_MEMBER(type)
  IPC_STRUCT_TRAITS_MEMBER(host)
  IPC_STRUCT_TRAITS_MEMBER(port)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(extensions::SocketPermissionData)
  IPC_STRUCT_TRAITS_MEMBER(pattern())
  IPC_STRUCT_TRAITS_MEMBER(match_subdomains())
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(extensions::UsbDevicePermissionData)
  IPC_STRUCT_TRAITS_MEMBER(vendor_id())
  IPC_STRUCT_TRAITS_MEMBER(product_id())
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(extensions::MediaGalleriesPermissionData)
  IPC_STRUCT_TRAITS_MEMBER(permission())
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(extensions::BluetoothPermissionData)
  IPC_STRUCT_TRAITS_MEMBER(uuid())
IPC_STRUCT_TRAITS_END()

// Singly-included section for custom IPC traits.
#ifndef CHROME_COMMON_EXTENSIONS_EXTENSION_MESSAGES_H_
#define CHROME_COMMON_EXTENSIONS_EXTENSION_MESSAGES_H_

// IPC_MESSAGE macros choke on extra , in the std::map, when expanding. We need
// to typedef it to avoid that.
// Substitution map for l10n messages.
typedef std::map<std::string, std::string> SubstitutionMap;

// Map of extensions IDs to the executing script paths.
typedef std::map<std::string, std::set<std::string> > ExecutingScriptsMap;

struct ExtensionMsg_Loaded_Params {
  ExtensionMsg_Loaded_Params();
  ~ExtensionMsg_Loaded_Params();
  explicit ExtensionMsg_Loaded_Params(const extensions::Extension* extension);

  // Creates a new extension from the data in this object.
  scoped_refptr<extensions::Extension> ConvertToExtension(
      std::string* error) const;

  // The subset of the extension manifest data we send to renderers.
  linked_ptr<base::DictionaryValue> manifest;

  // The location the extension was installed from.
  extensions::Manifest::Location location;

  // The path the extension was loaded from. This is used in the renderer only
  // to generate the extension ID for extensions that are loaded unpacked.
  base::FilePath path;

  // The extension's active permissions.
  extensions::APIPermissionSet apis;
  extensions::URLPatternSet explicit_hosts;
  extensions::URLPatternSet scriptable_hosts;

  // We keep this separate so that it can be used in logging.
  std::string id;

  // Send creation flags so extension is initialized identically.
  int creation_flags;
};

namespace IPC {

template <>
struct ParamTraits<URLPattern> {
  typedef URLPattern param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, PickleIterator* iter, param_type* p);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<extensions::URLPatternSet> {
  typedef extensions::URLPatternSet param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, PickleIterator* iter, param_type* p);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<extensions::APIPermission::ID> {
  typedef extensions::APIPermission::ID param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, PickleIterator* iter, param_type* p);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<extensions::APIPermission*> {
  typedef extensions::APIPermission* param_type;
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<extensions::APIPermissionSet> {
  typedef extensions::APIPermissionSet param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, PickleIterator* iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<ExtensionMsg_Loaded_Params> {
  typedef ExtensionMsg_Loaded_Params param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, PickleIterator* iter, param_type* p);
  static void Log(const param_type& p, std::string* l);
};

}  // namespace IPC

#endif  // CHROME_COMMON_EXTENSIONS_EXTENSION_MESSAGES_H_

// Messages sent from the browser to the renderer.

// The browser sends this message in response to all extension api calls. The
// response data (if any) is one of the base::Value subclasses, wrapped as the
// first element in a ListValue.
IPC_MESSAGE_ROUTED4(ExtensionMsg_Response,
                    int /* request_id */,
                    bool /* success */,
                    base::ListValue /* response wrapper (see comment above) */,
                    std::string /* error */)

// This message is optionally routed.  If used as a control message, it will
// call a javascript function |function_name| from module |module_name| in
// every registered context in the target process.  If routed, it will be
// restricted to the contexts that are part of the target RenderView.
//
// If |extension_id| is non-empty, the function will be invoked only in
// contexts owned by the extension. |args| is a list of primitive Value types
// that are passed to the function.
IPC_MESSAGE_ROUTED5(ExtensionMsg_MessageInvoke,
                    std::string /* extension_id */,
                    std::string /* module_name */,
                    std::string /* function_name */,
                    base::ListValue /* args */,
                    bool /* delivered as part of a user gesture */)

// Tell the renderer process all known extension function names.
IPC_MESSAGE_CONTROL1(ExtensionMsg_SetFunctionNames,
                     std::vector<std::string>)

// Tell the renderer process the platforms system font.
IPC_MESSAGE_CONTROL2(ExtensionMsg_SetSystemFont,
                     std::string /* font_family */,
                     std::string /* font_size */)

// Marks an extension as 'active' in an extension process. 'Active' extensions
// have more privileges than other extension content that might end up running
// in the process (e.g. because of iframes or content scripts).
IPC_MESSAGE_CONTROL1(ExtensionMsg_ActivateExtension,
                     std::string /* extension_id */)

// Notifies the renderer that extensions were loaded in the browser.
IPC_MESSAGE_CONTROL1(ExtensionMsg_Loaded,
                     std::vector<ExtensionMsg_Loaded_Params>)

// Notifies the renderer that an extension was unloaded in the browser.
IPC_MESSAGE_CONTROL1(ExtensionMsg_Unloaded,
                     std::string)

// Updates the scripting whitelist for extensions in the render process. This is
// only used for testing.
IPC_MESSAGE_CONTROL1(ExtensionMsg_SetScriptingWhitelist,
                     // extension ids
                     extensions::Extension::ScriptingWhitelist)

// Notification that renderer should run some JavaScript code.
IPC_MESSAGE_ROUTED1(ExtensionMsg_ExecuteCode,
                    ExtensionMsg_ExecuteCode_Params)

// Notification that the user scripts have been updated. It has one
// SharedMemoryHandle argument consisting of the pickled script data. This
// handle is valid in the context of the renderer.
IPC_MESSAGE_CONTROL1(ExtensionMsg_UpdateUserScripts,
                     base::SharedMemoryHandle)

// Requests application info for the page. The renderer responds back with
// ExtensionHostMsg_DidGetApplicationInfo.
IPC_MESSAGE_ROUTED1(ExtensionMsg_GetApplicationInfo,
                    int32 /*page_id*/)

// Tell the render view which browser window it's being attached to.
IPC_MESSAGE_ROUTED1(ExtensionMsg_UpdateBrowserWindowId,
                    int /* id of browser window */)

// Tell the render view what its tab ID is.
IPC_MESSAGE_ROUTED1(ExtensionMsg_SetTabId,
                    int /* id of tab */)

// Tell the renderer to update an extension's permission set.
IPC_MESSAGE_CONTROL5(ExtensionMsg_UpdatePermissions,
                     int /* UpdateExtensionPermissionsInfo::REASON */,
                     std::string /* extension_id */,
                     extensions::APIPermissionSet /* permissions */,
                     extensions::URLPatternSet /* explicit_hosts */,
                     extensions::URLPatternSet /* scriptable_hosts */)

// Tell the renderer about new tab-specific permissions for an extension.
IPC_MESSAGE_CONTROL4(ExtensionMsg_UpdateTabSpecificPermissions,
                     int32 /* page_id (only relevant for the target tab) */,
                     int /* tab_id */,
                     std::string /* extension_id */,
                     extensions::URLPatternSet /* hosts */)

// Tell the renderer to clear tab-specific permissions for some extensions.
IPC_MESSAGE_CONTROL2(ExtensionMsg_ClearTabSpecificPermissions,
                     int /* tab_id */,
                     std::vector<std::string> /* extension_ids */)

// Tell the renderer which type this view is.
IPC_MESSAGE_ROUTED1(ExtensionMsg_NotifyRenderViewType,
                    extensions::ViewType /* view_type */)

// Deliver a message sent with ExtensionHostMsg_PostMessage.
IPC_MESSAGE_CONTROL3(ExtensionMsg_UsingWebRequestAPI,
                     bool /* adblock */,
                     bool /* adblock_plus */,
                     bool /* other_webrequest */)

// Ask the lazy background page if it is ready to be suspended. This is sent
// when the page is considered idle. The renderer will reply with the same
// sequence_id so that we can tell which message it is responding to.
IPC_MESSAGE_CONTROL2(ExtensionMsg_ShouldSuspend,
                     std::string /* extension_id */,
                     int /* sequence_id */)

// If we complete a round of ShouldSuspend->ShouldSuspendAck messages without
// the lazy background page becoming active again, we are ready to unload. This
// message tells the page to dispatch the suspend event.
IPC_MESSAGE_CONTROL1(ExtensionMsg_Suspend,
                     std::string /* extension_id */)

// The browser changed its mind about suspending this extension.
IPC_MESSAGE_CONTROL1(ExtensionMsg_CancelSuspend,
                     std::string /* extension_id */)

// Send to renderer once the installation mentioned on
// ExtensionHostMsg_InlineWebstoreInstall is complete.
IPC_MESSAGE_ROUTED3(ExtensionMsg_InlineWebstoreInstallResponse,
                    int32 /* install id */,
                    bool /* whether the install was successful */,
                    std::string /* error */)

// Response to the renderer for ExtensionHostMsg_GetAppInstallState.
IPC_MESSAGE_ROUTED2(ExtensionMsg_GetAppInstallStateResponse,
                    std::string /* state */,
                    int32 /* callback_id */)

// Dispatch the Port.onConnect event for message channels.
IPC_MESSAGE_ROUTED4(ExtensionMsg_DispatchOnConnect,
                    int /* target_port_id */,
                    std::string /* channel_name */,
                    base::DictionaryValue /* source_tab */,
                    ExtensionMsg_ExternalConnectionInfo)

// Deliver a message sent with ExtensionHostMsg_PostMessage.
IPC_MESSAGE_ROUTED2(ExtensionMsg_DeliverMessage,
                    int /* target_port_id */,
                    std::string /* message */)

// Dispatch the Port.onDisconnect event for message channels.
IPC_MESSAGE_ROUTED2(ExtensionMsg_DispatchOnDisconnect,
                    int /* port_id */,
                    std::string /* error_message */)

// Informs the renderer what channel (dev, beta, stable, etc) is running.
IPC_MESSAGE_CONTROL1(ExtensionMsg_SetChannel,
                     int /* channel */)

// Adds a logging message to the renderer's root frame DevTools console.
IPC_MESSAGE_ROUTED2(ExtensionMsg_AddMessageToConsole,
                    content::ConsoleMessageLevel /* level */,
                    std::string /* message */)

// Notify the renderer that its window has closed.
IPC_MESSAGE_ROUTED0(ExtensionMsg_AppWindowClosed)

// Notify the renderer that an extension wants notifications when certain
// searches match the active page.  This message replaces the old set of
// searches, and triggers ExtensionHostMsg_OnWatchedPageChange messages from
// each tab to keep the browser updated about changes.
IPC_MESSAGE_CONTROL1(ExtensionMsg_WatchPages,
                     std::vector<std::string> /* CSS selectors */)

// Messages sent from the renderer to the browser.

// A renderer sends this message when an extension process starts an API
// request. The browser will always respond with a ExtensionMsg_Response.
IPC_MESSAGE_ROUTED1(ExtensionHostMsg_Request,
                    ExtensionHostMsg_Request_Params)

// A renderer sends this message when an extension process starts an API
// request. The browser will always respond with a ExtensionMsg_Response.
IPC_MESSAGE_CONTROL2(ExtensionHostMsg_RequestForIOThread,
                     int /* routing_id */,
                     ExtensionHostMsg_Request_Params)

// Notify the browser that the given extension added a listener to an event.
IPC_MESSAGE_CONTROL2(ExtensionHostMsg_AddListener,
                     std::string /* extension_id */,
                     std::string /* name */)

// Notify the browser that the given extension removed a listener from an
// event.
IPC_MESSAGE_CONTROL2(ExtensionHostMsg_RemoveListener,
                     std::string /* extension_id */,
                     std::string /* name */)

// Notify the browser that the given extension added a listener to an event from
// a lazy background page.
IPC_MESSAGE_CONTROL2(ExtensionHostMsg_AddLazyListener,
                     std::string /* extension_id */,
                     std::string /* name */)

// Notify the browser that the given extension is no longer interested in
// receiving the given event from a lazy background page.
IPC_MESSAGE_CONTROL2(ExtensionHostMsg_RemoveLazyListener,
                     std::string /* extension_id */,
                     std::string /* name */)

// Notify the browser that the given extension added a listener to instances of
// the named event that satisfy the filter.
IPC_MESSAGE_CONTROL4(ExtensionHostMsg_AddFilteredListener,
                     std::string /* extension_id */,
                     std::string /* name */,
                     base::DictionaryValue /* filter */,
                     bool /* lazy */)

// Notify the browser that the given extension is no longer interested in
// instances of the named event that satisfy the filter.
IPC_MESSAGE_CONTROL4(ExtensionHostMsg_RemoveFilteredListener,
                     std::string /* extension_id */,
                     std::string /* name */,
                     base::DictionaryValue /* filter */,
                     bool /* lazy */)

// Notify the browser that an event has finished being dispatched.
IPC_MESSAGE_ROUTED0(ExtensionHostMsg_EventAck)

// Open a channel to all listening contexts owned by the extension with
// the given ID.  This always returns a valid port ID which can be used for
// sending messages.  If an error occurred, the opener will be notified
// asynchronously.
IPC_SYNC_MESSAGE_CONTROL3_1(ExtensionHostMsg_OpenChannelToExtension,
                            int /* routing_id */,
                            ExtensionMsg_ExternalConnectionInfo,
                            std::string /* channel_name */,
                            int /* port_id */)

IPC_SYNC_MESSAGE_CONTROL3_1(ExtensionHostMsg_OpenChannelToNativeApp,
                            int /* routing_id */,
                            std::string /* source_extension_id */,
                            std::string /* native_app_name */,
                            int /* port_id */)

// Get a port handle to the given tab.  The handle can be used for sending
// messages to the extension.
IPC_SYNC_MESSAGE_CONTROL4_1(ExtensionHostMsg_OpenChannelToTab,
                            int /* routing_id */,
                            int /* tab_id */,
                            std::string /* extension_id */,
                            std::string /* channel_name */,
                            int /* port_id */)

// Send a message to an extension process.  The handle is the value returned
// by ViewHostMsg_OpenChannelTo*.
IPC_MESSAGE_ROUTED2(ExtensionHostMsg_PostMessage,
                    int /* port_id */,
                    std::string /* message */)

// Send a message to an extension process.  The handle is the value returned
// by ViewHostMsg_OpenChannelTo*.
IPC_MESSAGE_CONTROL2(ExtensionHostMsg_CloseChannel,
                     int /* port_id */,
                     std::string /* error_message */)

// Used to get the extension message bundle.
IPC_SYNC_MESSAGE_CONTROL1_1(ExtensionHostMsg_GetMessageBundle,
                            std::string /* extension id */,
                            SubstitutionMap /* message bundle */)

// Sent from the renderer to the browser to return the script running result.
IPC_MESSAGE_ROUTED5(
    ExtensionHostMsg_ExecuteCodeFinished,
    int /* request id */,
    std::string /* error; empty implies success */,
    int32 /* page_id the code executed on.  May be -1 if unsuccessful */,
    GURL /* URL of the code executed on. May be empty if unsuccessful. */,
    base::ListValue /* result of the script */)

// Sent from the renderer to the browser to notify that content scripts are
// running in the renderer that the IPC originated from.
// Note that the page_id is for the parent (or more accurately the topmost)
// frame (e.g. if executing in an iframe this is the page ID of the parent,
// unless the parent is an iframe... etc).
IPC_MESSAGE_ROUTED3(ExtensionHostMsg_ContentScriptsExecuting,
                    ExecutingScriptsMap,
                    int32 /* page_id of the _topmost_ frame */,
                    GURL /* url of the _topmost_ frame */)

IPC_MESSAGE_ROUTED2(ExtensionHostMsg_DidGetApplicationInfo,
                    int32 /* page_id */,
                    WebApplicationInfo)

// Sent by the renderer to implement chrome.webstore.install().
IPC_MESSAGE_ROUTED4(ExtensionHostMsg_InlineWebstoreInstall,
                    int32 /* install id */,
                    int32 /* return route id */,
                    std::string /* Web Store item ID */,
                    GURL /* requestor URL */)

// Sent by the renderer when a web page is checking if its app is installed.
IPC_MESSAGE_ROUTED3(ExtensionHostMsg_GetAppInstallState,
                    GURL /* requestor_url */,
                    int32 /* return_route_id */,
                    int32 /* callback_id */)

// Optional Ack message sent to the browser to notify that the response to a
// function has been processed.
IPC_MESSAGE_ROUTED1(ExtensionHostMsg_ResponseAck,
                    int /* request_id */)

// Response to ExtensionMsg_ShouldSuspend.
IPC_MESSAGE_CONTROL2(ExtensionHostMsg_ShouldSuspendAck,
                     std::string /* extension_id */,
                     int /* sequence_id */)

// Response to ExtensionMsg_Suspend, after we dispatch the suspend event.
IPC_MESSAGE_CONTROL1(ExtensionHostMsg_SuspendAck,
                     std::string /* extension_id */)

// Informs the browser to increment the keepalive count for the lazy background
// page, keeping it alive.
IPC_MESSAGE_ROUTED0(ExtensionHostMsg_IncrementLazyKeepaliveCount)

// Informs the browser there is one less thing keeping the lazy background page
// alive.
IPC_MESSAGE_ROUTED0(ExtensionHostMsg_DecrementLazyKeepaliveCount)

// Fetches a globally unique ID (for the lifetime of the browser) from the
// browser process.
IPC_SYNC_MESSAGE_CONTROL0_1(ExtensionHostMsg_GenerateUniqueID,
                            int /* unique_id */)

// Resumes resource requests for a newly created app window.
IPC_MESSAGE_CONTROL1(ExtensionHostMsg_ResumeRequests, int /* route_id */)

// Sent by the renderer when the draggable regions are updated.
IPC_MESSAGE_ROUTED1(ExtensionHostMsg_UpdateDraggableRegions,
                    std::vector<extensions::DraggableRegion> /* regions */)

// Sent by the renderer to log an API action to the extension activity log.
IPC_MESSAGE_CONTROL2(ExtensionHostMsg_AddAPIActionToActivityLog,
                     std::string /* extension_id */,
                     ExtensionHostMsg_APIActionOrEvent_Params)

// Sent by the renderer to log a blocked API action to the activity log.
IPC_MESSAGE_CONTROL2(ExtensionHostMsg_AddBlockedCallToActivityLog,
                    std::string /* extension_id */,
                    std::string /* api call function name */)

// Sent by the renderer to log an event to the extension activity log.
IPC_MESSAGE_CONTROL2(ExtensionHostMsg_AddEventToActivityLog,
                    std::string /* extension_id */,
                    ExtensionHostMsg_APIActionOrEvent_Params)

// Sent by the renderer to log a DOM action to the extension activity log.
IPC_MESSAGE_CONTROL2(ExtensionHostMsg_AddDOMActionToActivityLog,
                     std::string /* extension_id */,
                     ExtensionHostMsg_DOMAction_Params)

// Notifies the browser process that a tab has started or stopped matching
// certain conditions.  This message is sent in response to several events:
//
// * ExtensionMsg_WatchPages was received, updating the set of conditions.
// * A new page is loaded.  This will be sent after ViewHostMsg_FrameNavigate.
//   Currently this only fires for the main frame.
// * Something changed on an existing frame causing the set of matching searches
//   to change.
IPC_MESSAGE_ROUTED1(ExtensionHostMsg_OnWatchedPageChange,
                    std::vector<std::string> /* Matching CSS selectors */)
