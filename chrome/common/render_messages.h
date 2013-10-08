// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Multiply-included file, no traditional include guard.
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/files/file_path.h"
#include "base/memory/shared_memory.h"
#include "base/process/process.h"
#include "base/strings/string16.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/common/autocomplete_match_type.h"
#include "chrome/common/common_param_traits.h"
#include "chrome/common/content_settings.h"
#include "chrome/common/content_settings_pattern.h"
#include "chrome/common/instant_types.h"
#include "chrome/common/omnibox_focus_state.h"
#include "chrome/common/search_provider.h"
#include "chrome/common/translate/language_detection_details.h"
#include "chrome/common/translate/translate_errors.h"
#include "components/nacl/common/nacl_types.h"
#include "content/public/common/common_param_traits.h"
#include "content/public/common/referrer.h"
#include "content/public/common/top_controls_state.h"
#include "ipc/ipc_channel_handle.h"
#include "ipc/ipc_message_macros.h"
#include "ipc/ipc_platform_file.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/WebKit/public/web/WebCache.h"
#include "third_party/WebKit/public/web/WebConsoleMessage.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/rect.h"

// Singly-included section for enums and custom IPC traits.
#ifndef CHROME_COMMON_RENDER_MESSAGES_H_
#define CHROME_COMMON_RENDER_MESSAGES_H_

class SkBitmap;

// Command values for the cmd parameter of the
// ViewHost_JavaScriptStressTestControl message. For each command the parameter
// passed has a different meaning:
// For the command kJavaScriptStressTestSetStressRunType the parameter it the
// type taken from the enumeration v8::Testing::StressType.
// For the command kJavaScriptStressTestPrepareStressRun the parameter it the
// number of the stress run about to take place.
enum ViewHostMsg_JavaScriptStressTestControl_Commands {
  kJavaScriptStressTestSetStressRunType = 0,
  kJavaScriptStressTestPrepareStressRun = 1,
};

// This enum is inside a struct so that we can forward-declare the struct in
// others headers without having to include this one.
struct ChromeViewHostMsg_GetPluginInfo_Status {
  enum Value {
    kAllowed,
    kBlocked,
    kClickToPlay,
    kDisabled,
    kNotFound,
    kNPAPINotSupported,
    kOutdatedBlocked,
    kOutdatedDisallowed,
    kUnauthorized,
  };

  ChromeViewHostMsg_GetPluginInfo_Status() : value(kAllowed) {}

  Value value;
};

namespace IPC {

#if defined(OS_POSIX) && !defined(USE_AURA) && !defined(OS_ANDROID)

// TODO(port): this shouldn't exist. However, the plugin stuff is really using
// HWNDS (NativeView), and making Windows calls based on them. I've not figured
// out the deal with plugins yet.
// TODO(android): a gfx::NativeView is the same as a gfx::NativeWindow.
template <>
struct ParamTraits<gfx::NativeView> {
  typedef gfx::NativeView param_type;
  static void Write(Message* m, const param_type& p) {
    NOTIMPLEMENTED();
  }

  static bool Read(const Message* m, PickleIterator* iter, param_type* p) {
    NOTIMPLEMENTED();
    *p = NULL;
    return true;
  }

  static void Log(const param_type& p, std::string* l) {
    l->append(base::StringPrintf("<gfx::NativeView>"));
  }
};

#endif  // defined(OS_POSIX) && !defined(USE_AURA) && !defined(OS_ANDROID)

template <>
struct ParamTraits<ContentSettingsPattern> {
  typedef ContentSettingsPattern param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, PickleIterator* iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};

}  // namespace IPC

#endif  // CHROME_COMMON_RENDER_MESSAGES_H_

#define IPC_MESSAGE_START ChromeMsgStart

IPC_ENUM_TRAITS(ChromeViewHostMsg_GetPluginInfo_Status::Value)
IPC_ENUM_TRAITS(OmniboxFocusChangeReason)
IPC_ENUM_TRAITS(OmniboxFocusState)
IPC_ENUM_TRAITS(search_provider::OSDDType)
IPC_ENUM_TRAITS(search_provider::InstallState)
IPC_ENUM_TRAITS(ThemeBackgroundImageAlignment)
IPC_ENUM_TRAITS(ThemeBackgroundImageTiling)
IPC_ENUM_TRAITS(TranslateErrors::Type)
IPC_ENUM_TRAITS(WebKit::WebConsoleMessage::Level)
IPC_ENUM_TRAITS(content::TopControlsState)

IPC_STRUCT_TRAITS_BEGIN(ChromeViewHostMsg_GetPluginInfo_Status)
IPC_STRUCT_TRAITS_MEMBER(value)
IPC_STRUCT_TRAITS_END()

// Output parameters for ChromeViewHostMsg_GetPluginInfo message.
IPC_STRUCT_BEGIN(ChromeViewHostMsg_GetPluginInfo_Output)
  IPC_STRUCT_MEMBER(ChromeViewHostMsg_GetPluginInfo_Status, status)
  IPC_STRUCT_MEMBER(content::WebPluginInfo, plugin)
  IPC_STRUCT_MEMBER(std::string, actual_mime_type)
  IPC_STRUCT_MEMBER(std::string, group_identifier)
  IPC_STRUCT_MEMBER(string16, group_name)
IPC_STRUCT_END()

IPC_STRUCT_TRAITS_BEGIN(ContentSettingsPattern::PatternParts)
  IPC_STRUCT_TRAITS_MEMBER(scheme)
  IPC_STRUCT_TRAITS_MEMBER(is_scheme_wildcard)
  IPC_STRUCT_TRAITS_MEMBER(host)
  IPC_STRUCT_TRAITS_MEMBER(has_domain_wildcard)
  IPC_STRUCT_TRAITS_MEMBER(port)
  IPC_STRUCT_TRAITS_MEMBER(is_port_wildcard)
  IPC_STRUCT_TRAITS_MEMBER(path)
  IPC_STRUCT_TRAITS_MEMBER(is_path_wildcard)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(ContentSettingPatternSource)
  IPC_STRUCT_TRAITS_MEMBER(primary_pattern)
  IPC_STRUCT_TRAITS_MEMBER(secondary_pattern)
  IPC_STRUCT_TRAITS_MEMBER(setting)
  IPC_STRUCT_TRAITS_MEMBER(source)
  IPC_STRUCT_TRAITS_MEMBER(incognito)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(InstantMostVisitedItem)
  IPC_STRUCT_TRAITS_MEMBER(url)
  IPC_STRUCT_TRAITS_MEMBER(title)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(RendererContentSettingRules)
  IPC_STRUCT_TRAITS_MEMBER(image_rules)
  IPC_STRUCT_TRAITS_MEMBER(script_rules)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(RGBAColor)
  IPC_STRUCT_TRAITS_MEMBER(r)
  IPC_STRUCT_TRAITS_MEMBER(g)
  IPC_STRUCT_TRAITS_MEMBER(b)
  IPC_STRUCT_TRAITS_MEMBER(a)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(ThemeBackgroundInfo)
  IPC_STRUCT_TRAITS_MEMBER(using_default_theme)
  IPC_STRUCT_TRAITS_MEMBER(background_color)
  IPC_STRUCT_TRAITS_MEMBER(text_color)
  IPC_STRUCT_TRAITS_MEMBER(link_color)
  IPC_STRUCT_TRAITS_MEMBER(text_color_light)
  IPC_STRUCT_TRAITS_MEMBER(header_color)
  IPC_STRUCT_TRAITS_MEMBER(section_border_color)
  IPC_STRUCT_TRAITS_MEMBER(theme_id)
  IPC_STRUCT_TRAITS_MEMBER(image_horizontal_alignment)
  IPC_STRUCT_TRAITS_MEMBER(image_vertical_alignment)
  IPC_STRUCT_TRAITS_MEMBER(image_tiling)
  IPC_STRUCT_TRAITS_MEMBER(image_height)
  IPC_STRUCT_TRAITS_MEMBER(has_attribution)
  IPC_STRUCT_TRAITS_MEMBER(logo_alternate)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(WebKit::WebCache::ResourceTypeStat)
  IPC_STRUCT_TRAITS_MEMBER(count)
  IPC_STRUCT_TRAITS_MEMBER(size)
  IPC_STRUCT_TRAITS_MEMBER(liveSize)
  IPC_STRUCT_TRAITS_MEMBER(decodedSize)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(WebKit::WebCache::ResourceTypeStats)
  IPC_STRUCT_TRAITS_MEMBER(images)
  IPC_STRUCT_TRAITS_MEMBER(cssStyleSheets)
  IPC_STRUCT_TRAITS_MEMBER(scripts)
  IPC_STRUCT_TRAITS_MEMBER(xslStyleSheets)
  IPC_STRUCT_TRAITS_MEMBER(fonts)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(WebKit::WebCache::UsageStats)
  IPC_STRUCT_TRAITS_MEMBER(minDeadCapacity)
  IPC_STRUCT_TRAITS_MEMBER(maxDeadCapacity)
  IPC_STRUCT_TRAITS_MEMBER(capacity)
  IPC_STRUCT_TRAITS_MEMBER(liveSize)
  IPC_STRUCT_TRAITS_MEMBER(deadSize)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(LanguageDetectionDetails)
  IPC_STRUCT_TRAITS_MEMBER(time)
  IPC_STRUCT_TRAITS_MEMBER(url)
  IPC_STRUCT_TRAITS_MEMBER(content_language)
  IPC_STRUCT_TRAITS_MEMBER(cld_language)
  IPC_STRUCT_TRAITS_MEMBER(is_cld_reliable)
  IPC_STRUCT_TRAITS_MEMBER(html_root_language)
  IPC_STRUCT_TRAITS_MEMBER(adopted_language)
  IPC_STRUCT_TRAITS_MEMBER(contents)
IPC_STRUCT_TRAITS_END()

//-----------------------------------------------------------------------------
// RenderView messages
// These are messages sent from the browser to the renderer process.

// Tells the renderer to set its maximum cache size to the supplied value.
IPC_MESSAGE_CONTROL3(ChromeViewMsg_SetCacheCapacities,
                     size_t /* min_dead_capacity */,
                     size_t /* max_dead_capacity */,
                     size_t /* capacity */)

// Tells the renderer to clear the cache.
IPC_MESSAGE_CONTROL1(ChromeViewMsg_ClearCache,
                     bool /* on_navigation */)

// Tells the renderer to dump as much memory as it can, perhaps because we
// have memory pressure or the renderer is (or will be) paged out.  This
// should only result in purging objects we can recalculate, e.g. caches or
// JS garbage, not in purging irreplaceable objects.
IPC_MESSAGE_CONTROL0(ChromeViewMsg_PurgeMemory)

// For WebUI testing, this message stores parameters to do ScriptEvalRequest at
// a time which is late enough to not be thrown out, and early enough to be
// before onload events are fired.
IPC_MESSAGE_ROUTED4(ChromeViewMsg_WebUIJavaScript,
                    string16,  /* frame_xpath */
                    string16,  /* jscript_url */
                    int,  /* ID */
                    bool  /* If true, result is sent back. */)

// Set the content setting rules stored by the renderer.
IPC_MESSAGE_CONTROL1(ChromeViewMsg_SetContentSettingRules,
                     RendererContentSettingRules /* rules */)

// Tells the render view to load all blocked plugins with the given identifier.
IPC_MESSAGE_ROUTED1(ChromeViewMsg_LoadBlockedPlugins,
                    std::string /* identifier */)

// Asks the renderer to send back stats on the WebCore cache broken down by
// resource types.
IPC_MESSAGE_CONTROL0(ChromeViewMsg_GetCacheResourceStats)

// Tells the renderer to create a FieldTrial, and by using a 100% probability
// for the FieldTrial, forces the FieldTrial to have assigned group name.
IPC_MESSAGE_CONTROL2(ChromeViewMsg_SetFieldTrialGroup,
                     std::string /* field trial name */,
                     std::string /* group name that was assigned. */)

// Asks the renderer to send back V8 heap stats.
IPC_MESSAGE_CONTROL0(ChromeViewMsg_GetV8HeapStats)

// Posts a message to the renderer.
IPC_MESSAGE_ROUTED3(ChromeViewMsg_HandleMessageFromExternalHost,
                    std::string /* The message */,
                    std::string /* The origin */,
                    std::string /* The target*/)

IPC_MESSAGE_ROUTED0(ChromeViewMsg_DetermineIfPageSupportsInstant)

IPC_MESSAGE_ROUTED2(ChromeViewMsg_SearchBoxFocusChanged,
                    OmniboxFocusState /* new_focus_state */,
                    OmniboxFocusChangeReason /* reason */)

IPC_MESSAGE_ROUTED2(ChromeViewMsg_SearchBoxFontInformation,
                    string16 /* omnibox_font */,
                    size_t /* omnibox_font_size */)

IPC_MESSAGE_ROUTED2(ChromeViewMsg_SearchBoxMarginChange,
                    int /* start */,
                    int /* width */)

IPC_MESSAGE_ROUTED1(ChromeViewMsg_SearchBoxMostVisitedItemsChanged,
                    std::vector<InstantMostVisitedItem> /* items */)

IPC_MESSAGE_ROUTED1(ChromeViewMsg_SearchBoxPromoInformation,
                    bool /* is_app_launcher_enabled */)

IPC_MESSAGE_ROUTED1(ChromeViewMsg_SearchBoxSetInputInProgress,
                    bool /* input_in_progress */)

IPC_MESSAGE_ROUTED1(ChromeViewMsg_SearchBoxSubmit,
                    string16 /* value */)

IPC_MESSAGE_ROUTED1(ChromeViewMsg_SearchBoxThemeChanged,
                    ThemeBackgroundInfo /* value */)

IPC_MESSAGE_ROUTED0(ChromeViewMsg_SearchBoxToggleVoiceSearch)

// Toggles visual muting of the render view area. This is on when a constrained
// window is showing.
IPC_MESSAGE_ROUTED1(ChromeViewMsg_SetVisuallyDeemphasized,
                    bool /* deemphazied */)

// Tells the renderer to translate the page contents from one language to
// another.
IPC_MESSAGE_ROUTED4(ChromeViewMsg_TranslatePage,
                    int /* page id */,
                    std::string, /* the script injected in the page */
                    std::string, /* BCP 47/RFC 5646 language code the page
                                    is in */
                    std::string /* BCP 47/RFC 5646 language code to translate
                                   to */)

// Tells the renderer to revert the text of translated page to its original
// contents.
IPC_MESSAGE_ROUTED1(ChromeViewMsg_RevertTranslation,
                    int /* page id */)

// Sent on process startup to indicate whether this process is running in
// incognito mode.
IPC_MESSAGE_CONTROL1(ChromeViewMsg_SetIsIncognitoProcess,
                     bool /* is_incognito_processs */)

// Sent on process startup to indicate whether the extension activity
// log is enabled.
IPC_MESSAGE_CONTROL1(ChromeViewMsg_SetExtensionActivityLogEnabled,
                     bool /* extension_activity_log_enabled */)

// Sent in response to ViewHostMsg_DidBlockDisplayingInsecureContent.
IPC_MESSAGE_ROUTED1(ChromeViewMsg_SetAllowDisplayingInsecureContent,
                    bool /* allowed */)

// Sent in response to ViewHostMsg_DidBlockRunningInsecureContent.
IPC_MESSAGE_ROUTED1(ChromeViewMsg_SetAllowRunningInsecureContent,
                    bool /* allowed */)

// Tells renderer to always enforce mixed content blocking for this host.
IPC_MESSAGE_ROUTED1(ChromeViewMsg_AddStrictSecurityHost,
                    std::string /* host */)

// Sent when the profile changes the kSafeBrowsingEnabled preference.
IPC_MESSAGE_ROUTED1(ChromeViewMsg_SetClientSidePhishingDetection,
                    bool /* enable_phishing_detection */)

// This message asks frame sniffer start.
IPC_MESSAGE_ROUTED1(ChromeViewMsg_StartFrameSniffer,
                    string16 /* frame-name */)

// Asks the renderer for a thumbnail of the image selected by the most
// recently opened context menu, if there is one. If the image's area
// is greater than thumbnail_min_area it will be downscaled to
// be within thumbnail_max_size. The possibly downsampled image will be
// returned in a ChromeViewHostMsg_RequestThumbnailForContextNode_ACK message.
IPC_MESSAGE_ROUTED2(ChromeViewMsg_RequestThumbnailForContextNode,
                    int /* thumbnail_min_area_pixels */,
                    gfx::Size /* thumbnail_max_size_pixels */)

// Notifies the renderer whether hiding/showing the top controls is enabled,
// what the current state should be, and whether or not to animate to the
// proper state.
IPC_MESSAGE_ROUTED3(ChromeViewMsg_UpdateTopControlsState,
                    content::TopControlsState /* constraints */,
                    content::TopControlsState /* current */,
                    bool /* animate */)


// Updates the window features of the render view.
IPC_MESSAGE_ROUTED1(ChromeViewMsg_SetWindowFeatures,
                    WebKit::WebWindowFeatures /* window_features */)

IPC_MESSAGE_ROUTED1(ChromeViewHostMsg_RequestThumbnailForContextNode_ACK,
                    SkBitmap /* thumbnail */)


// JavaScript related messages -----------------------------------------------

// Notify the JavaScript engine in the render to change its parameters
// while performing stress testing.
IPC_MESSAGE_ROUTED2(ChromeViewMsg_JavaScriptStressTestControl,
                    int /* cmd */,
                    int /* param */)

// Asks the renderer to send back FPS.
IPC_MESSAGE_ROUTED0(ChromeViewMsg_GetFPS)

// Tells the view it is displaying an interstitial page.
IPC_MESSAGE_ROUTED0(ChromeViewMsg_SetAsInterstitial)

// Provides the renderer with the results of the browser's investigation into
// why a recent main frame load failed (currently, just DNS probe result).
// NetErrorHelper will receive this mesage and replace or update the error
// page with more specific troubleshooting suggestions.
IPC_MESSAGE_ROUTED1(ChromeViewMsg_NetErrorInfo,
                    int /* DNS probe status */)

//-----------------------------------------------------------------------------
// Misc messages
// These are messages sent from the renderer to the browser process.

// Notification that the language for the tab has been determined.
IPC_MESSAGE_ROUTED2(ChromeViewHostMsg_TranslateLanguageDetermined,
                    LanguageDetectionDetails /* details about lang detection */,
                    bool /* whether the page needs translation */)

IPC_MESSAGE_CONTROL1(ChromeViewHostMsg_UpdatedCacheStats,
                     WebKit::WebCache::UsageStats /* stats */)

// Tells the browser that content in the current page was blocked due to the
// user's content settings.
IPC_MESSAGE_ROUTED2(ChromeViewHostMsg_ContentBlocked,
                    ContentSettingsType, /* type of blocked content */
                    std::string /* resource identifier */)

// Sent by the renderer process to check whether access to web databases is
// granted by content settings.
IPC_SYNC_MESSAGE_CONTROL5_1(ChromeViewHostMsg_AllowDatabase,
                            int /* render_view_id */,
                            GURL /* origin_url */,
                            GURL /* top origin url */,
                            string16 /* database name */,
                            string16 /* database display name */,
                            bool /* allowed */)

// Sent by the renderer process to check whether access to DOM Storage is
// granted by content settings.
IPC_SYNC_MESSAGE_CONTROL4_1(ChromeViewHostMsg_AllowDOMStorage,
                            int /* render_view_id */,
                            GURL /* origin_url */,
                            GURL /* top origin url */,
                            bool /* if true local storage, otherwise session */,
                            bool /* allowed */)

// Sent by the renderer process to check whether access to FileSystem is
// granted by content settings.
IPC_SYNC_MESSAGE_CONTROL3_1(ChromeViewHostMsg_AllowFileSystem,
                            int /* render_view_id */,
                            GURL /* origin_url */,
                            GURL /* top origin url */,
                            bool /* allowed */)

// Sent by the renderer process to check whether access to Indexed DBis
// granted by content settings.
IPC_SYNC_MESSAGE_CONTROL4_1(ChromeViewHostMsg_AllowIndexedDB,
                            int /* render_view_id */,
                            GURL /* origin_url */,
                            GURL /* top origin url */,
                            string16 /* database name */,
                            bool /* allowed */)

// Return information about a plugin for the given URL and MIME type.
// In contrast to ViewHostMsg_GetPluginInfo in content/, this IPC call knows
// about specific reasons why a plug-in can't be used, for example because it's
// disabled.
IPC_SYNC_MESSAGE_CONTROL4_1(ChromeViewHostMsg_GetPluginInfo,
                            int /* render_view_id */,
                            GURL /* url */,
                            GURL /* top origin url */,
                            std::string /* mime_type */,
                            ChromeViewHostMsg_GetPluginInfo_Output /* output */)

#if defined(ENABLE_PLUGIN_INSTALLATION)
// Tells the browser to search for a plug-in that can handle the given MIME
// type. The result will be sent asynchronously to the routing ID
// |placeholder_id|.
IPC_MESSAGE_ROUTED2(ChromeViewHostMsg_FindMissingPlugin,
                    int /* placeholder_id */,
                    std::string /* mime_type */)

// Notifies the browser that a missing plug-in placeholder has been removed, so
// the corresponding PluginPlaceholderHost can be deleted.
IPC_MESSAGE_ROUTED1(ChromeViewHostMsg_RemovePluginPlaceholderHost,
                    int /* placeholder_id */)

// Notifies a missing plug-in placeholder that a plug-in with name |plugin_name|
// has been found.
IPC_MESSAGE_ROUTED1(ChromeViewMsg_FoundMissingPlugin,
                    string16 /* plugin_name */)

// Notifies a missing plug-in placeholder that no plug-in has been found.
IPC_MESSAGE_ROUTED0(ChromeViewMsg_DidNotFindMissingPlugin)

// Notifies a missing plug-in placeholder that we have started downloading
// the plug-in.
IPC_MESSAGE_ROUTED0(ChromeViewMsg_StartedDownloadingPlugin)

// Notifies a missing plug-in placeholder that we have finished downloading
// the plug-in.
IPC_MESSAGE_ROUTED0(ChromeViewMsg_FinishedDownloadingPlugin)

// Notifies a missing plug-in placeholder that there was an error downloading
// the plug-in.
IPC_MESSAGE_ROUTED1(ChromeViewMsg_ErrorDownloadingPlugin,
                    std::string /* message */)
#endif  // defined(ENABLE_PLUGIN_INSTALLATION)

// Notifies a missing plug-in placeholder that the user cancelled downloading
// the plug-in.
IPC_MESSAGE_ROUTED0(ChromeViewMsg_CancelledDownloadingPlugin)

// Tells the browser to open chrome://plugins in a new tab. We use a separate
// message because renderer processes aren't allowed to directly navigate to
// chrome:// URLs.
IPC_MESSAGE_ROUTED0(ChromeViewHostMsg_OpenAboutPlugins)

// Tells the browser that there was an error loading a plug-in.
IPC_MESSAGE_ROUTED1(ChromeViewHostMsg_CouldNotLoadPlugin,
                    base::FilePath /* plugin_path */)

// Tells the browser that we blocked a plug-in because NPAPI is not supported.
IPC_MESSAGE_ROUTED1(ChromeViewHostMsg_NPAPINotSupported,
                    std::string /* identifer */)

// Tells the renderer that the NPAPI cannot be used. For example Ash on windows.
IPC_MESSAGE_ROUTED0(ChromeViewMsg_NPAPINotSupported)

// A message for an external host.
IPC_MESSAGE_ROUTED3(ChromeViewHostMsg_ForwardMessageToExternalHost,
                    std::string  /* message */,
                    std::string  /* origin */,
                    std::string  /* target */)

// Notification that the page has an OpenSearch description document
// associated with it.
IPC_MESSAGE_ROUTED3(ChromeViewHostMsg_PageHasOSDD,
                    int32 /* page_id */,
                    GURL /* url of OS description document */,
                    search_provider::OSDDType)

// Find out if the given url's security origin is installed as a search
// provider.
IPC_SYNC_MESSAGE_ROUTED2_1(ChromeViewHostMsg_GetSearchProviderInstallState,
                           GURL /* page url */,
                           GURL /* inquiry url */,
                           search_provider::InstallState /* install */)

// Sends back stats about the V8 heap.
IPC_MESSAGE_CONTROL2(ChromeViewHostMsg_V8HeapStats,
                     int /* size of heap (allocated from the OS) */,
                     int /* bytes in use */)

// Request for a DNS prefetch of the names in the array.
// NameList is typedef'ed std::vector<std::string>
IPC_MESSAGE_CONTROL1(ChromeViewHostMsg_DnsPrefetch,
                     std::vector<std::string> /* hostnames */)

// Request for preconnect to host providing resource specified by URL
IPC_MESSAGE_CONTROL1(ChromeViewHostMsg_Preconnect,
                     GURL /* preconnect target url */)

// Notifies when a plugin couldn't be loaded because it's outdated.
IPC_MESSAGE_ROUTED2(ChromeViewHostMsg_BlockedOutdatedPlugin,
                    int /* placeholder ID */,
                    std::string /* plug-in group identifier */)

// Notifies when a plugin couldn't be loaded because it requires
// user authorization.
IPC_MESSAGE_ROUTED2(ChromeViewHostMsg_BlockedUnauthorizedPlugin,
                    string16 /* name */,
                    std::string /* plug-in group identifier */)

// Provide the browser process with information about the WebCore resource
// cache and current renderer framerate.
IPC_MESSAGE_CONTROL1(ChromeViewHostMsg_ResourceTypeStats,
                     WebKit::WebCache::ResourceTypeStats)

// Notifies the browser that a page has been translated.
IPC_MESSAGE_ROUTED4(ChromeViewHostMsg_PageTranslated,
                    int,                  /* page id */
                    std::string           /* the original language */,
                    std::string           /* the translated language */,
                    TranslateErrors::Type /* the error type if available */)

// Message sent from the renderer to the browser to notify it of events which
// may lead to the cancellation of a prerender. The message is sent only when
// the renderer is prerendering.
IPC_MESSAGE_ROUTED0(ChromeViewHostMsg_MaybeCancelPrerenderForHTML5Media)

// Message sent from the renderer to the browser to notify it of a
// window.print() call which should cancel the prerender. The message is sent
// only when the renderer is prerendering.
IPC_MESSAGE_ROUTED0(ChromeViewHostMsg_CancelPrerenderForPrinting)

// Sent by the renderer to check if a URL has permission to trigger a clipboard
// read/write operation from the DOM.
IPC_SYNC_MESSAGE_ROUTED1_1(ChromeViewHostMsg_CanTriggerClipboardRead,
                           GURL /* origin */,
                           bool /* allowed */)
IPC_SYNC_MESSAGE_ROUTED1_1(ChromeViewHostMsg_CanTriggerClipboardWrite,
                           GURL /* origin */,
                           bool /* allowed */)

// Sent when the renderer was prevented from displaying insecure content in
// a secure page by a security policy.  The page may appear incomplete.
IPC_MESSAGE_ROUTED0(ChromeViewHostMsg_DidBlockDisplayingInsecureContent)

// Sent when the renderer was prevented from running insecure content in
// a secure origin by a security policy.  The page may appear incomplete.
IPC_MESSAGE_ROUTED0(ChromeViewHostMsg_DidBlockRunningInsecureContent)

// Message sent from renderer to the browser when the element that is focused
// has been touched. A bool is passed in this message which indicates if the
// node is editable.
IPC_MESSAGE_ROUTED1(ChromeViewHostMsg_FocusedNodeTouched,
                    bool /* editable */)

// The currently displayed PDF has an unsupported feature.
IPC_MESSAGE_ROUTED0(ChromeViewHostMsg_PDFHasUnsupportedFeature)

// Brings up SaveAs... dialog to save specified URL.
IPC_MESSAGE_ROUTED2(ChromeViewHostMsg_PDFSaveURLAs,
                    GURL /* url */,
                    content::Referrer /* referrer */)

// Updates the content restrictions, i.e. to disable print/copy.
IPC_MESSAGE_ROUTED1(ChromeViewHostMsg_PDFUpdateContentRestrictions,
                    int /* restrictions */)

// This message indicates the error appeared in the frame.
IPC_MESSAGE_ROUTED1(ChromeViewHostMsg_FrameLoadingError,
                    int /* error */)

// This message indicates the monitored frame loading had completed.
IPC_MESSAGE_ROUTED0(ChromeViewHostMsg_FrameLoadingCompleted)

// The following messages are used to set and get cookies for ChromeFrame
// processes.
// Used to set a cookie. The cookie is set asynchronously, but will be
// available to a subsequent ChromeViewHostMsg_GetCookies request.
IPC_MESSAGE_ROUTED3(ChromeViewHostMsg_SetCookie,
                    GURL /* url */,
                    GURL /* first_party_for_cookies */,
                    std::string /* cookie */)

// Used to get cookies for the given URL. This may block waiting for a
// previous SetCookie message to be processed.
IPC_SYNC_MESSAGE_ROUTED2_1(ChromeViewHostMsg_GetCookies,
                           GURL /* url */,
                           GURL /* first_party_for_cookies */,
                           std::string /* cookies */)

// Provide the browser process with current renderer framerate.
IPC_MESSAGE_CONTROL2(ChromeViewHostMsg_FPS,
                     int /* routing id */,
                     float /* frames per second */)

// Counts a mouseover event on an InstantExtended most visited tile.
IPC_MESSAGE_ROUTED1(ChromeViewHostMsg_CountMouseover,
                    int /* page_id */)

// Tells InstantExtended to set the omnibox focus state.
IPC_MESSAGE_ROUTED2(ChromeViewHostMsg_FocusOmnibox,
                    int /* page_id */,
                    OmniboxFocusState /* state */)

// Tells InstantExtended to paste text into the omnibox.  If text is empty,
// the clipboard contents will be pasted. This causes the omnibox dropdown to
// open.
IPC_MESSAGE_ROUTED2(ChromeViewHostMsg_PasteAndOpenDropdown,
                    int /* page_id */,
                    string16 /* text to be pasted */)

// Tells InstantExtended whether the embedded search API is supported.
// See http://dev.chromium.org/embeddedsearch
IPC_MESSAGE_ROUTED2(ChromeViewHostMsg_InstantSupportDetermined,
                    int /* page_id */,
                    bool /* result */)

// Tells InstantExtended to delete a most visited item.
IPC_MESSAGE_ROUTED2(ChromeViewHostMsg_SearchBoxDeleteMostVisitedItem,
                    int /* page_id */,
                    GURL /* url */)

// Tells InstantExtended to navigate the active tab to a possibly priveleged
// URL.
IPC_MESSAGE_ROUTED5(ChromeViewHostMsg_SearchBoxNavigate,
                    int /* page_id */,
                    GURL /* destination */,
                    content::PageTransition /* transition */,
                    WindowOpenDisposition /* disposition */,
                    bool /* is_search_type */)

// Tells InstantExtended to undo all most visited item deletions.
IPC_MESSAGE_ROUTED1(ChromeViewHostMsg_SearchBoxUndoAllMostVisitedDeletions,
                    int /* page_id */)

// Tells InstantExtended to undo one most visited item deletion.
IPC_MESSAGE_ROUTED2(ChromeViewHostMsg_SearchBoxUndoMostVisitedDeletion,
                    int /* page_id */,
                    GURL /* url */)

// Tells InstantExtended whether the page supports voice search.
IPC_MESSAGE_ROUTED2(ChromeViewHostMsg_SetVoiceSearchSupported,
                    int /* page_id */,
                    bool /* supported */)
