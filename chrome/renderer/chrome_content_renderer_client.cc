// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/chrome_content_renderer_client.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/common/child_process_logging.h"
#include "chrome/common/chrome_content_client.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/content_settings_pattern.h"
#include "chrome/common/extensions/chrome_extensions_client.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "chrome/common/extensions/extension_process_policy.h"
#include "chrome/common/extensions/extension_set.h"
#include "chrome/common/external_ipc_fuzzer.h"
#include "chrome/common/localized_error.h"
#include "chrome/common/pepper_permission_util.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "chrome/renderer/benchmarking_extension.h"
#include "chrome/renderer/chrome_render_process_observer.h"
#include "chrome/renderer/chrome_render_view_observer.h"
#include "chrome/renderer/content_settings_observer.h"
#include "chrome/renderer/extensions/chrome_v8_context.h"
#include "chrome/renderer/extensions/chrome_v8_extension.h"
#include "chrome/renderer/extensions/dispatcher.h"
#include "chrome/renderer/extensions/extension_helper.h"
#include "chrome/renderer/extensions/renderer_permissions_policy_delegate.h"
#include "chrome/renderer/extensions/resource_request_policy.h"
#include "chrome/renderer/external_extension.h"
#include "chrome/renderer/loadtimes_extension_bindings.h"
#include "chrome/renderer/net/net_error_helper.h"
#include "chrome/renderer/net/prescient_networking_dispatcher.h"
#include "chrome/renderer/net/renderer_net_predictor.h"
#include "chrome/renderer/net_benchmarking_extension.h"
#include "chrome/renderer/one_click_signin_agent.h"
#include "chrome/renderer/page_load_histograms.h"
#include "chrome/renderer/pepper/pepper_helper.h"
#include "chrome/renderer/pepper/ppb_nacl_private_impl.h"
#include "chrome/renderer/pepper/ppb_pdf_impl.h"
#include "chrome/renderer/playback_extension.h"
#include "chrome/renderer/plugins/plugin_placeholder.h"
#include "chrome/renderer/plugins/plugin_uma.h"
#include "chrome/renderer/prerender/prerender_dispatcher.h"
#include "chrome/renderer/prerender/prerender_helper.h"
#include "chrome/renderer/prerender/prerender_media_load_deferrer.h"
#include "chrome/renderer/prerender/prerenderer_client.h"
#include "chrome/renderer/printing/print_web_view_helper.h"
#include "chrome/renderer/safe_browsing/malware_dom_details.h"
#include "chrome/renderer/safe_browsing/phishing_classifier_delegate.h"
#include "chrome/renderer/searchbox/searchbox.h"
#include "chrome/renderer/searchbox/searchbox_extension.h"
#include "chrome/renderer/tts_dispatcher.h"
#include "chrome/renderer/validation_message_agent.h"
#include "components/autofill/content/renderer/autofill_agent.h"
#include "components/autofill/content/renderer/password_autofill_agent.h"
#include "components/autofill/content/renderer/password_generation_manager.h"
#include "components/visitedlink/renderer/visitedlink_slave.h"
#include "content/public/common/content_constants.h"
#include "content/public/renderer/render_thread.h"
#include "content/public/renderer/render_view.h"
#include "content/public/renderer/render_view_visitor.h"
#include "extensions/common/constants.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/renderer_resources.h"
#include "ipc/ipc_sync_channel.h"
#include "net/base/net_errors.h"
#include "ppapi/c/private/ppb_nacl_private.h"
#include "ppapi/c/private/ppb_pdf.h"
#include "ppapi/shared_impl/ppapi_switches.h"
#include "third_party/WebKit/public/web/WebCache.h"
#include "third_party/WebKit/public/web/WebDataSource.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebElement.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/web/WebPluginContainer.h"
#include "third_party/WebKit/public/web/WebPluginParams.h"
#include "third_party/WebKit/public/web/WebSecurityOrigin.h"
#include "third_party/WebKit/public/web/WebSecurityPolicy.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "third_party/WebKit/public/platform/WebURLError.h"
#include "third_party/WebKit/public/platform/WebURLRequest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/webui/jstemplate_builder.h"
#include "widevine_cdm_version.h"  // In SHARED_INTERMEDIATE_DIR.

#if defined(ENABLE_WEBRTC)
#include "chrome/renderer/media/webrtc_logging_message_filter.h"
#endif

#if defined(ENABLE_SPELLCHECK)
#include "chrome/renderer/spellchecker/spellcheck.h"
#include "chrome/renderer/spellchecker/spellcheck_provider.h"
#endif

using autofill::AutofillAgent;
using autofill::PasswordAutofillAgent;
using autofill::PasswordGenerationManager;
using content::RenderThread;
using content::WebPluginInfo;
using extensions::Extension;
using WebKit::WebCache;
using WebKit::WebConsoleMessage;
using WebKit::WebDataSource;
using WebKit::WebDocument;
using WebKit::WebFrame;
using WebKit::WebPlugin;
using WebKit::WebPluginParams;
using WebKit::WebSecurityOrigin;
using WebKit::WebSecurityPolicy;
using WebKit::WebString;
using WebKit::WebURL;
using WebKit::WebURLError;
using WebKit::WebURLRequest;
using WebKit::WebURLResponse;
using WebKit::WebVector;

namespace {

const char kWebViewTagName[] = "WEBVIEW";
const char kAdViewTagName[] = "ADVIEW";

chrome::ChromeContentRendererClient* g_current_client;

static void AppendParams(const std::vector<string16>& additional_names,
                         const std::vector<string16>& additional_values,
                         WebVector<WebString>* existing_names,
                         WebVector<WebString>* existing_values) {
  DCHECK(additional_names.size() == additional_values.size());
  DCHECK(existing_names->size() == existing_values->size());

  size_t existing_size = existing_names->size();
  size_t total_size = existing_size + additional_names.size();

  WebVector<WebString> names(total_size);
  WebVector<WebString> values(total_size);

  for (size_t i = 0; i < existing_size; ++i) {
    names[i] = (*existing_names)[i];
    values[i] = (*existing_values)[i];
  }

  for (size_t i = 0; i < additional_names.size(); ++i) {
    names[existing_size + i] = additional_names[i];
    values[existing_size + i] = additional_values[i];
  }

  existing_names->swap(names);
  existing_values->swap(values);
}

#if defined(ENABLE_SPELLCHECK)
class SpellCheckReplacer : public content::RenderViewVisitor {
 public:
  explicit SpellCheckReplacer(SpellCheck* spellcheck)
      : spellcheck_(spellcheck) {}
  virtual bool Visit(content::RenderView* render_view) OVERRIDE;

 private:
  SpellCheck* spellcheck_;  // New shared spellcheck for all views. Weak Ptr.
  DISALLOW_COPY_AND_ASSIGN(SpellCheckReplacer);
};

bool SpellCheckReplacer::Visit(content::RenderView* render_view) {
  SpellCheckProvider* provider = SpellCheckProvider::Get(render_view);
  DCHECK(provider);
  provider->set_spellcheck(spellcheck_);
  return true;
}
#endif

// For certain sandboxed Pepper plugins, use the JavaScript Content Settings.
bool ShouldUseJavaScriptSettingForPlugin(const WebPluginInfo& plugin) {
  if (plugin.type != WebPluginInfo::PLUGIN_TYPE_PEPPER_IN_PROCESS &&
      plugin.type != WebPluginInfo::PLUGIN_TYPE_PEPPER_OUT_OF_PROCESS) {
    return false;
  }

  // Treat Native Client invocations like JavaScript.
  if (plugin.name == ASCIIToUTF16(chrome::ChromeContentClient::kNaClPluginName))
    return true;

#if defined(WIDEVINE_CDM_AVAILABLE) && defined(ENABLE_PEPPER_CDMS)
  // Treat CDM invocations like JavaScript.
  if (plugin.name == ASCIIToUTF16(kWidevineCdmDisplayName)) {
    DCHECK(plugin.type == WebPluginInfo::PLUGIN_TYPE_PEPPER_OUT_OF_PROCESS);
    return true;
  }
#endif  // defined(WIDEVINE_CDM_AVAILABLE) && defined(ENABLE_PEPPER_CDMS)

  return false;
}

#if defined(ENABLE_PLUGINS)
const char* kPredefinedAllowedFileHandleOrigins[] = {
  "6EAED1924DB611B6EEF2A664BD077BE7EAD33B8F",  // see crbug.com/234789
  "4EB74897CB187C7633357C2FE832E0AD6A44883A"   // see crbug.com/234789
};
#endif

}  // namespace

namespace chrome {

ChromeContentRendererClient::ChromeContentRendererClient() {
  g_current_client = this;

#if defined(ENABLE_PLUGINS)
  for (size_t i = 0; i < arraysize(kPredefinedAllowedFileHandleOrigins); ++i)
    allowed_file_handle_origins_.insert(kPredefinedAllowedFileHandleOrigins[i]);
#endif
}

ChromeContentRendererClient::~ChromeContentRendererClient() {
  g_current_client = NULL;
}

void ChromeContentRendererClient::RenderThreadStarted() {
  chrome_observer_.reset(new ChromeRenderProcessObserver(this));
  extension_dispatcher_.reset(new extensions::Dispatcher());
  permissions_policy_delegate_.reset(
      new extensions::RendererPermissionsPolicyDelegate(
          extension_dispatcher_.get()));
  prescient_networking_dispatcher_.reset(new PrescientNetworkingDispatcher());
  net_predictor_.reset(new RendererNetPredictor());
#if defined(ENABLE_SPELLCHECK)
  spellcheck_.reset(new SpellCheck());
#endif
  visited_link_slave_.reset(new visitedlink::VisitedLinkSlave());
#if defined(FULL_SAFE_BROWSING)
  phishing_classifier_.reset(safe_browsing::PhishingClassifierFilter::Create());
#endif
  prerender_dispatcher_.reset(new prerender::PrerenderDispatcher());
#if defined(ENABLE_WEBRTC)
  webrtc_logging_message_filter_ = new WebRtcLoggingMessageFilter(
      content::RenderThread::Get()->GetIOMessageLoopProxy());
#endif

  RenderThread* thread = RenderThread::Get();

  thread->AddObserver(chrome_observer_.get());
  thread->AddObserver(extension_dispatcher_.get());
#if defined(FULL_SAFE_BROWSING)
  thread->AddObserver(phishing_classifier_.get());
#endif
#if defined(ENABLE_SPELLCHECK)
  thread->AddObserver(spellcheck_.get());
#endif
  thread->AddObserver(visited_link_slave_.get());
  thread->AddObserver(prerender_dispatcher_.get());

#if defined(ENABLE_WEBRTC)
  thread->AddFilter(webrtc_logging_message_filter_.get());
#endif

  thread->RegisterExtension(extensions_v8::ExternalExtension::Get());
  thread->RegisterExtension(extensions_v8::LoadTimesExtension::Get());

  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kEnableBenchmarking))
    thread->RegisterExtension(extensions_v8::BenchmarkingExtension::Get());
  if (command_line->HasSwitch(switches::kEnableNetBenchmarking))
    thread->RegisterExtension(extensions_v8::NetBenchmarkingExtension::Get());
  if (command_line->HasSwitch(switches::kInstantProcess))
    thread->RegisterExtension(extensions_v8::SearchBoxExtension::Get());

  if (command_line->HasSwitch(switches::kPlaybackMode) ||
      command_line->HasSwitch(switches::kRecordMode) ||
      command_line->HasSwitch(switches::kNoJsRandomness)) {
    thread->RegisterExtension(extensions_v8::PlaybackExtension::Get());
  }

  if (command_line->HasSwitch(switches::kEnableIPCFuzzing)) {
    thread->GetChannel()->set_outgoing_message_filter(LoadExternalIPCFuzzer());
  }
  // chrome:, chrome-search:, chrome-devtools:, and chrome-internal: pages
  // should not be accessible by normal content, and should also be unable to
  // script anything but themselves (to help limit the damage that a corrupt
  // page could cause).
  WebString chrome_ui_scheme(ASCIIToUTF16(chrome::kChromeUIScheme));
  WebSecurityPolicy::registerURLSchemeAsDisplayIsolated(chrome_ui_scheme);

  WebString chrome_search_scheme(ASCIIToUTF16(chrome::kChromeSearchScheme));
  // The Instant process can only display the content but not read it.  Other
  // processes can't display it or read it.
  if (!command_line->HasSwitch(switches::kInstantProcess))
    WebSecurityPolicy::registerURLSchemeAsDisplayIsolated(chrome_search_scheme);

  WebString dev_tools_scheme(ASCIIToUTF16(chrome::kChromeDevToolsScheme));
  WebSecurityPolicy::registerURLSchemeAsDisplayIsolated(dev_tools_scheme);

  WebString internal_scheme(ASCIIToUTF16(chrome::kChromeInternalScheme));
  WebSecurityPolicy::registerURLSchemeAsDisplayIsolated(internal_scheme);

#if defined(OS_CHROMEOS)
  WebString drive_scheme(ASCIIToUTF16(chrome::kDriveScheme));
  WebSecurityPolicy::registerURLSchemeAsLocal(drive_scheme);
#endif

  // chrome: and chrome-search: pages should not be accessible by bookmarklets
  // or javascript: URLs typed in the omnibox.
  WebSecurityPolicy::registerURLSchemeAsNotAllowingJavascriptURLs(
      chrome_ui_scheme);
  WebSecurityPolicy::registerURLSchemeAsNotAllowingJavascriptURLs(
      chrome_search_scheme);

  // chrome:, chrome-search:, and chrome-extension: resources shouldn't trigger
  // insecure content warnings.
  WebSecurityPolicy::registerURLSchemeAsSecure(chrome_ui_scheme);
  WebSecurityPolicy::registerURLSchemeAsSecure(chrome_search_scheme);

  WebString extension_scheme(ASCIIToUTF16(extensions::kExtensionScheme));
  WebSecurityPolicy::registerURLSchemeAsSecure(extension_scheme);

  // chrome-extension: resources should be allowed to receive CORS requests.
  WebSecurityPolicy::registerURLSchemeAsCORSEnabled(extension_scheme);

  WebString extension_resource_scheme(
      ASCIIToUTF16(chrome::kExtensionResourceScheme));
  WebSecurityPolicy::registerURLSchemeAsSecure(extension_resource_scheme);

  // chrome-extension-resource: resources should be allowed to receive CORS
  // requests.
  WebSecurityPolicy::registerURLSchemeAsCORSEnabled(extension_resource_scheme);

  // chrome-extension: resources should bypass Content Security Policy checks
  // when included in protected resources.
  WebSecurityPolicy::registerURLSchemeAsBypassingContentSecurityPolicy(
      extension_scheme);
  WebSecurityPolicy::registerURLSchemeAsBypassingContentSecurityPolicy(
      extension_resource_scheme);

  extensions::ExtensionsClient::Set(
      extensions::ChromeExtensionsClient::GetInstance());
}

void ChromeContentRendererClient::RenderViewCreated(
    content::RenderView* render_view) {
  ContentSettingsObserver* content_settings =
      new ContentSettingsObserver(render_view);
  if (chrome_observer_.get()) {
    content_settings->SetContentSettingRules(
        chrome_observer_->content_setting_rules());
  }
  new extensions::ExtensionHelper(render_view, extension_dispatcher_.get());
  new PageLoadHistograms(render_view);
#if defined(ENABLE_PRINTING)
  new printing::PrintWebViewHelper(render_view);
#endif
#if defined(ENABLE_SPELLCHECK)
  new SpellCheckProvider(render_view, spellcheck_.get());
#endif
  new prerender::PrerendererClient(render_view);
#if defined(FULL_SAFE_BROWSING)
  safe_browsing::MalwareDOMDetails::Create(render_view);
#endif

  PasswordAutofillAgent* password_autofill_agent =
      new PasswordAutofillAgent(render_view);
  new AutofillAgent(render_view, password_autofill_agent);
  new ValidationMessageAgent(render_view);

  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kEnablePasswordGeneration))
    new PasswordGenerationManager(render_view);
  if (command_line->HasSwitch(switches::kInstantProcess))
    new SearchBox(render_view);

  new ChromeRenderViewObserver(
      render_view, content_settings, chrome_observer_.get(),
      extension_dispatcher_.get());

#if defined(ENABLE_PLUGINS)
  new PepperHelper(render_view);
#endif

  new NetErrorHelper(render_view);

#if defined(ENABLE_ONE_CLICK_SIGNIN)
  new OneClickSigninAgent(render_view);
#endif
}

void ChromeContentRendererClient::SetNumberOfViews(int number_of_views) {
  child_process_logging::SetNumberOfViews(number_of_views);
}

SkBitmap* ChromeContentRendererClient::GetSadPluginBitmap() {
  return const_cast<SkBitmap*>(ResourceBundle::GetSharedInstance().
      GetImageNamed(IDR_SAD_PLUGIN).ToSkBitmap());
}

SkBitmap* ChromeContentRendererClient::GetSadWebViewBitmap() {
  return const_cast<SkBitmap*>(ResourceBundle::GetSharedInstance().
      GetImageNamed(IDR_SAD_WEBVIEW).ToSkBitmap());
}

std::string ChromeContentRendererClient::GetDefaultEncoding() {
  return l10n_util::GetStringUTF8(IDS_DEFAULT_ENCODING);
}

const Extension* ChromeContentRendererClient::GetExtension(
    const WebSecurityOrigin& origin) const {
  if (!EqualsASCII(origin.protocol(), extensions::kExtensionScheme))
    return NULL;

  const std::string extension_id = origin.host().utf8().data();
  if (!extension_dispatcher_->IsExtensionActive(extension_id))
    return NULL;

  return extension_dispatcher_->extensions()->GetByID(extension_id);
}

bool ChromeContentRendererClient::OverrideCreatePlugin(
    content::RenderView* render_view,
    WebFrame* frame,
    const WebPluginParams& params,
    WebPlugin** plugin) {
  std::string orig_mime_type = params.mimeType.utf8();
  if (orig_mime_type == content::kBrowserPluginMimeType) {
    if (CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kEnableBrowserPluginForAllViewTypes))
      return false;
    WebDocument document = frame->document();
    const Extension* extension =
        GetExtension(document.securityOrigin());
    if (extension) {
      const extensions::APIPermission::ID perms[] = {
        extensions::APIPermission::kWebView,
        extensions::APIPermission::kAdView
      };
      for (size_t i = 0; i < arraysize(perms); ++i) {
        if (extension->HasAPIPermission(perms[i]))
          return false;
      }
    }
  }

  ChromeViewHostMsg_GetPluginInfo_Output output;
#if defined(ENABLE_PLUGINS)
  render_view->Send(new ChromeViewHostMsg_GetPluginInfo(
      render_view->GetRoutingID(), GURL(params.url),
      frame->top()->document().url(), orig_mime_type, &output));
#else
  output.status.value = ChromeViewHostMsg_GetPluginInfo_Status::kNotFound;
#endif
  *plugin = CreatePlugin(render_view, frame, params, output);
  return true;
}

WebPlugin* ChromeContentRendererClient::CreatePluginReplacement(
    content::RenderView* render_view,
    const base::FilePath& plugin_path) {
  PluginPlaceholder* placeholder =
      PluginPlaceholder::CreateErrorPlugin(render_view, plugin_path);
  return placeholder->plugin();
}

void ChromeContentRendererClient::DeferMediaLoad(
    content::RenderView* render_view,
    const base::Closure& closure) {
#if defined(OS_ANDROID)
  // Chromium for Android doesn't support prerender yet.
  closure.Run();
  return;
#else
  if (!prerender::PrerenderHelper::IsPrerendering(render_view)) {
    closure.Run();
    return;
  }

  // Lifetime is tied to |render_view| via content::RenderViewObserver.
  new prerender::PrerenderMediaLoadDeferrer(render_view, closure);
#endif
}

WebPlugin* ChromeContentRendererClient::CreatePlugin(
    content::RenderView* render_view,
    WebFrame* frame,
    const WebPluginParams& original_params,
    const ChromeViewHostMsg_GetPluginInfo_Output& output) {
  const ChromeViewHostMsg_GetPluginInfo_Status& status = output.status;
  const WebPluginInfo& plugin = output.plugin;
  const std::string& actual_mime_type = output.actual_mime_type;
  const string16& group_name = output.group_name;
  const std::string& identifier = output.group_identifier;
  ChromeViewHostMsg_GetPluginInfo_Status::Value status_value = status.value;
  GURL url(original_params.url);
  std::string orig_mime_type = original_params.mimeType.utf8();
  PluginPlaceholder* placeholder = NULL;

  // If the browser plugin is to be enabled, this should be handled by the
  // renderer, so the code won't reach here due to the early exit in
  // OverrideCreatePlugin.
  if (status_value == ChromeViewHostMsg_GetPluginInfo_Status::kNotFound ||
      orig_mime_type == content::kBrowserPluginMimeType) {
#if defined(ENABLE_MOBILE_YOUTUBE_PLUGIN)
    if (PluginPlaceholder::IsYouTubeURL(url, orig_mime_type))
      return PluginPlaceholder::CreateMobileYoutubePlugin(render_view, frame,
          original_params)->plugin();
#endif
    PluginUMAReporter::GetInstance()->ReportPluginMissing(orig_mime_type, url);
    placeholder = PluginPlaceholder::CreateMissingPlugin(
        render_view, frame, original_params);
  } else {
    // TODO(bauerb): This should be in content/.
    WebPluginParams params(original_params);
    for (size_t i = 0; i < plugin.mime_types.size(); ++i) {
      if (plugin.mime_types[i].mime_type == actual_mime_type) {
        AppendParams(plugin.mime_types[i].additional_param_names,
                     plugin.mime_types[i].additional_param_values,
                     &params.attributeNames,
                     &params.attributeValues);
        break;
      }
    }
    if (params.mimeType.isNull() && (actual_mime_type.size() > 0)) {
      // Webkit might say that mime type is null while we already know the
      // actual mime type via ChromeViewHostMsg_GetPluginInfo. In that case
      // we should use what we know since WebpluginDelegateProxy does some
      // specific initializations based on this information.
      params.mimeType = WebString::fromUTF8(actual_mime_type.c_str());
    }

    ContentSettingsObserver* observer =
        ContentSettingsObserver::Get(render_view);

    const ContentSettingsType content_type =
        ShouldUseJavaScriptSettingForPlugin(plugin) ?
            CONTENT_SETTINGS_TYPE_JAVASCRIPT :
            CONTENT_SETTINGS_TYPE_PLUGINS;

    if ((status_value ==
             ChromeViewHostMsg_GetPluginInfo_Status::kUnauthorized ||
         status_value == ChromeViewHostMsg_GetPluginInfo_Status::kClickToPlay ||
         status_value == ChromeViewHostMsg_GetPluginInfo_Status::kBlocked) &&
        observer->IsPluginTemporarilyAllowed(identifier)) {
      status_value = ChromeViewHostMsg_GetPluginInfo_Status::kAllowed;
    }

    // Allow full-page plug-ins for click-to-play.
    if (status_value == ChromeViewHostMsg_GetPluginInfo_Status::kClickToPlay &&
        !frame->parent() &&
        !frame->opener() &&
        frame->document().isPluginDocument()) {
      status_value = ChromeViewHostMsg_GetPluginInfo_Status::kAllowed;
    }

#if defined(USE_AURA) && defined(OS_WIN)
    // In Aura for Windows we need to check if we can load NPAPI plugins.
    // For example, if the render view is in the Ash desktop, we should not.
    if (status_value == ChromeViewHostMsg_GetPluginInfo_Status::kAllowed &&
        plugin.type == content::WebPluginInfo::PLUGIN_TYPE_NPAPI) {
        if (observer->AreNPAPIPluginsBlocked())
          status_value =
              ChromeViewHostMsg_GetPluginInfo_Status::kNPAPINotSupported;
    }
#endif

    switch (status_value) {
      case ChromeViewHostMsg_GetPluginInfo_Status::kNotFound: {
        NOTREACHED();
        break;
      }
      case ChromeViewHostMsg_GetPluginInfo_Status::kAllowed: {
        const char* kPnaclMimeType = "application/x-pnacl";
        if (actual_mime_type == kPnaclMimeType) {
          if (!CommandLine::ForCurrentProcess()->HasSwitch(
                  switches::kEnablePnacl)) {
            frame->addMessageToConsole(
                WebConsoleMessage(
                    WebConsoleMessage::LevelError,
                    "Portable Native Client must be enabled in about:flags."));
            placeholder = PluginPlaceholder::CreateBlockedPlugin(
                render_view, frame, params, plugin, identifier, group_name,
                IDR_BLOCKED_PLUGIN_HTML,
#if defined(OS_CHROMEOS)
                l10n_util::GetStringUTF16(IDS_NACL_PLUGIN_BLOCKED));
#else
                l10n_util::GetStringFUTF16(IDS_PLUGIN_BLOCKED, group_name));
#endif
              break;
          }
        } else {
          const char* kNaClMimeType = "application/x-nacl";
          const bool is_nacl_mime_type = actual_mime_type == kNaClMimeType;
          const bool is_nacl_plugin =
              plugin.name ==
              ASCIIToUTF16(chrome::ChromeContentClient::kNaClPluginName);
          bool is_nacl_unrestricted;
          if (is_nacl_plugin) {
            is_nacl_unrestricted = CommandLine::ForCurrentProcess()->HasSwitch(
                switches::kEnableNaCl);
          } else {
            // If this is an external plugin that handles the NaCl mime type, we
            // allow Native Client, so Native Client's integration tests work.
            is_nacl_unrestricted = true;
          }
          if (is_nacl_plugin || is_nacl_mime_type) {
            GURL manifest_url;
            GURL app_url;
            if (is_nacl_mime_type) {
              // Normal NaCl embed. The app URL is the page URL.
              manifest_url = url;
              app_url = frame->top()->document().url();
            } else {
              // NaCl is being invoked as a content handler. Look up the NaCl
              // module using the MIME type. The app URL is the manifest URL.
              manifest_url = GetNaClContentHandlerURL(actual_mime_type, plugin);
              app_url = manifest_url;
            }
            const Extension* extension =
                g_current_client->extension_dispatcher_->extensions()->
                    GetExtensionOrAppByURL(manifest_url);
            if (!IsNaClAllowed(manifest_url,
                               app_url,
                               is_nacl_unrestricted,
                               extension,
                               &params)) {
              frame->addMessageToConsole(
                  WebConsoleMessage(
                      WebConsoleMessage::LevelError,
                      "Only unpacked extensions and apps installed from the "
                      "Chrome Web Store can load NaCl modules without enabling "
                      "Native Client in about:flags."));
              placeholder = PluginPlaceholder::CreateBlockedPlugin(
                  render_view, frame, params, plugin, identifier, group_name,
                  IDR_BLOCKED_PLUGIN_HTML,
#if defined(OS_CHROMEOS)
                  l10n_util::GetStringUTF16(IDS_NACL_PLUGIN_BLOCKED));
#else
                  l10n_util::GetStringFUTF16(IDS_PLUGIN_BLOCKED, group_name));
#endif
              break;
            }
          }
        }

        // Delay loading plugins if prerendering.
        // TODO(mmenke):  In the case of prerendering, feed into
        //                ChromeContentRendererClient::CreatePlugin instead, to
        //                reduce the chance of future regressions.
        if (prerender::PrerenderHelper::IsPrerendering(render_view)) {
          placeholder = PluginPlaceholder::CreateBlockedPlugin(
              render_view, frame, params, plugin, identifier, group_name,
              IDR_CLICK_TO_PLAY_PLUGIN_HTML,
              l10n_util::GetStringFUTF16(IDS_PLUGIN_LOAD, group_name));
          placeholder->set_blocked_for_prerendering(true);
          placeholder->set_allow_loading(true);
          break;
        }

        return render_view->CreatePlugin(frame, plugin, params);
      }
      case ChromeViewHostMsg_GetPluginInfo_Status::kNPAPINotSupported: {
        RenderThread::Get()->RecordUserMetrics("Plugin_NPAPINotSupported");
        placeholder = PluginPlaceholder::CreateBlockedPlugin(
            render_view, frame, params, plugin, identifier, group_name,
            IDR_BLOCKED_PLUGIN_HTML,
            l10n_util::GetStringUTF16(IDS_PLUGIN_NOT_SUPPORTED_METRO));
        render_view->Send(new ChromeViewHostMsg_NPAPINotSupported(
            render_view->GetRoutingID(), identifier));
        break;
      }
      case ChromeViewHostMsg_GetPluginInfo_Status::kDisabled: {
        PluginUMAReporter::GetInstance()->ReportPluginDisabled(orig_mime_type,
                                                               url);
        placeholder = PluginPlaceholder::CreateBlockedPlugin(
            render_view, frame, params, plugin, identifier, group_name,
            IDR_DISABLED_PLUGIN_HTML,
            l10n_util::GetStringFUTF16(IDS_PLUGIN_DISABLED, group_name));
        break;
      }
      case ChromeViewHostMsg_GetPluginInfo_Status::kOutdatedBlocked: {
#if defined(ENABLE_PLUGIN_INSTALLATION)
        placeholder = PluginPlaceholder::CreateBlockedPlugin(
            render_view, frame, params, plugin, identifier, group_name,
            IDR_BLOCKED_PLUGIN_HTML,
            l10n_util::GetStringFUTF16(IDS_PLUGIN_OUTDATED, group_name));
        placeholder->set_allow_loading(true);
        render_view->Send(new ChromeViewHostMsg_BlockedOutdatedPlugin(
            render_view->GetRoutingID(), placeholder->CreateRoutingId(),
            identifier));
#else
        NOTREACHED();
#endif
        break;
      }
      case ChromeViewHostMsg_GetPluginInfo_Status::kOutdatedDisallowed: {
        placeholder = PluginPlaceholder::CreateBlockedPlugin(
            render_view, frame, params, plugin, identifier, group_name,
            IDR_BLOCKED_PLUGIN_HTML,
            l10n_util::GetStringFUTF16(IDS_PLUGIN_OUTDATED, group_name));
        break;
      }
      case ChromeViewHostMsg_GetPluginInfo_Status::kUnauthorized: {
        placeholder = PluginPlaceholder::CreateBlockedPlugin(
            render_view, frame, params, plugin, identifier, group_name,
            IDR_BLOCKED_PLUGIN_HTML,
            l10n_util::GetStringFUTF16(IDS_PLUGIN_NOT_AUTHORIZED, group_name));
        placeholder->set_allow_loading(true);
        render_view->Send(new ChromeViewHostMsg_BlockedUnauthorizedPlugin(
            render_view->GetRoutingID(),
            group_name,
            identifier));
        break;
      }
      case ChromeViewHostMsg_GetPluginInfo_Status::kClickToPlay: {
        placeholder = PluginPlaceholder::CreateBlockedPlugin(
            render_view, frame, params, plugin, identifier, group_name,
            IDR_CLICK_TO_PLAY_PLUGIN_HTML,
            l10n_util::GetStringFUTF16(IDS_PLUGIN_LOAD, group_name));
        placeholder->set_allow_loading(true);
        RenderThread::Get()->RecordUserMetrics("Plugin_ClickToPlay");
        observer->DidBlockContentType(content_type, identifier);
        break;
      }
      case ChromeViewHostMsg_GetPluginInfo_Status::kBlocked: {
        placeholder = PluginPlaceholder::CreateBlockedPlugin(
            render_view, frame, params, plugin, identifier, group_name,
            IDR_BLOCKED_PLUGIN_HTML,
            l10n_util::GetStringFUTF16(IDS_PLUGIN_BLOCKED, group_name));
        placeholder->set_allow_loading(true);
        RenderThread::Get()->RecordUserMetrics("Plugin_Blocked");
        observer->DidBlockContentType(content_type, identifier);
        break;
      }
    }
  }
  placeholder->SetStatus(status);
  return placeholder->plugin();
}

// For NaCl content handling plugins, the NaCl manifest is stored in an
// additonal 'nacl' param associated with the MIME type.
//  static
GURL ChromeContentRendererClient::GetNaClContentHandlerURL(
    const std::string& actual_mime_type,
    const content::WebPluginInfo& plugin) {
  // Look for the manifest URL among the MIME type's additonal parameters.
  const char* kNaClPluginManifestAttribute = "nacl";
  string16 nacl_attr = ASCIIToUTF16(kNaClPluginManifestAttribute);
  for (size_t i = 0; i < plugin.mime_types.size(); ++i) {
    if (plugin.mime_types[i].mime_type == actual_mime_type) {
      const content::WebPluginMimeType& content_type = plugin.mime_types[i];
      for (size_t i = 0; i < content_type.additional_param_names.size(); ++i) {
        if (content_type.additional_param_names[i] == nacl_attr)
          return GURL(content_type.additional_param_values[i]);
      }
      break;
    }
  }
  return GURL();
}

//  static
bool ChromeContentRendererClient::IsNaClAllowed(
    const GURL& manifest_url,
    const GURL& app_url,
    bool is_nacl_unrestricted,
    const Extension* extension,
    WebPluginParams* params) {
  // Temporarily allow these URLs to run NaCl apps, as long as the manifest is
  // also whitelisted. We should remove this code when PNaCl ships.
  bool is_whitelisted_url =
      app_url.SchemeIs("https") &&
      (app_url.host() == "plus.google.com" ||
       app_url.host() == "plus.sandbox.google.com") &&
      manifest_url.SchemeIs("https") &&
      manifest_url.host() == "ssl.gstatic.com" &&
      ((manifest_url.path().find("s2/oz/nacl/") == 1) ||
       (manifest_url.path().find("photos/nacl/") == 1));

  bool is_extension_from_webstore =
      extension && extension->from_webstore();

  bool is_invoked_by_hosted_app = extension &&
      extension->is_hosted_app() &&
      extension->web_extent().MatchesURL(app_url);

  // Allow built-in extensions and extensions under development.
  bool is_extension_unrestricted = extension &&
      (extension->location() == extensions::Manifest::COMPONENT ||
       extensions::Manifest::IsUnpackedLocation(extension->location()));

  bool is_invoked_by_extension = app_url.SchemeIs("chrome-extension");

  // The NaCl PDF viewer is always allowed and can use 'Dev' interfaces.
  bool is_nacl_pdf_viewer =
      (is_extension_from_webstore &&
       manifest_url.SchemeIs("chrome-extension") &&
       manifest_url.host() == "acadkphlmlegjaadjagenfimbpphcgnh");

  // Allow Chrome Web Store extensions, built-in extensions and extensions
  // under development if the invocation comes from a URL with an extension
  // scheme. Also allow invocations if they are from whitelisted URLs or
  // if --enable-nacl is set.
  bool is_nacl_allowed = is_nacl_unrestricted ||
                         is_whitelisted_url ||
                         is_nacl_pdf_viewer ||
                         is_invoked_by_hosted_app ||
                         (is_invoked_by_extension &&
                             (is_extension_from_webstore ||
                                 is_extension_unrestricted));
  if (is_nacl_allowed) {
    bool app_can_use_dev_interfaces = is_nacl_pdf_viewer;
    // Make sure that PPAPI 'dev' interfaces aren't available for production
    // apps unless they're whitelisted.
    WebString dev_attribute = WebString::fromUTF8("@dev");
    if ((!is_whitelisted_url && !is_extension_from_webstore) ||
        app_can_use_dev_interfaces) {
      // Add the special '@dev' attribute.
      std::vector<string16> param_names;
      std::vector<string16> param_values;
      param_names.push_back(dev_attribute);
      param_values.push_back(WebString());
      AppendParams(
          param_names,
          param_values,
          &params->attributeNames,
          &params->attributeValues);
    } else {
      // If the params somehow contain '@dev', remove it.
      size_t attribute_count = params->attributeNames.size();
      for (size_t i = 0; i < attribute_count; ++i) {
        if (params->attributeNames[i].equals(dev_attribute))
          params->attributeNames[i] = WebString();
      }
    }
  }
  return is_nacl_allowed;
}

bool ChromeContentRendererClient::HasErrorPage(int http_status_code,
                                               std::string* error_domain) {
  // Use an internal error page, if we have one for the status code.
  if (!LocalizedError::HasStrings(LocalizedError::kHttpErrorDomain,
                                  http_status_code)) {
    return false;
  }

  *error_domain = LocalizedError::kHttpErrorDomain;
  return true;
}

void ChromeContentRendererClient::GetNavigationErrorStrings(
    WebKit::WebFrame* frame,
    const WebKit::WebURLRequest& failed_request,
    const WebKit::WebURLError& error,
    std::string* error_html,
    string16* error_description) {
  const GURL failed_url = error.unreachableURL;
  const Extension* extension = NULL;

  if (failed_url.is_valid() &&
      !failed_url.SchemeIs(extensions::kExtensionScheme)) {
    extension = extension_dispatcher_->extensions()->GetExtensionOrAppByURL(
        failed_url);
  }

  bool is_post = EqualsASCII(failed_request.httpMethod(), "POST");

  if (error_html) {
    // Use a local error page.
    int resource_id;
    base::DictionaryValue error_strings;
    if (extension && !extension->from_bookmark()) {
      LocalizedError::GetAppErrorStrings(error, failed_url, extension,
                                         &error_strings);

      // TODO(erikkay): Should we use a different template for different
      // error messages?
      resource_id = IDR_ERROR_APP_HTML;
    } else {
      const std::string locale = RenderThread::Get()->GetLocale();
      if (!NetErrorHelper::GetErrorStringsForDnsProbe(
              frame, error, is_post, locale, &error_strings)) {
        // In most cases, the NetErrorHelper won't provide DNS-probe-specific
        // error pages, so fall back to LocalizedError.
        LocalizedError::GetStrings(error, is_post, locale, &error_strings);
      }
      resource_id = IDR_NET_ERROR_HTML;
    }

    const base::StringPiece template_html(
        ResourceBundle::GetSharedInstance().GetRawDataResource(
            resource_id));
    if (template_html.empty()) {
      NOTREACHED() << "unable to load template. ID: " << resource_id;
    } else {
      // "t" is the id of the templates root node.
      *error_html = webui::GetTemplatesHtml(template_html, &error_strings, "t");
    }
  }

  if (error_description) {
    if (!extension)
      *error_description = LocalizedError::GetErrorDetails(error, is_post);
  }
}

bool ChromeContentRendererClient::RunIdleHandlerWhenWidgetsHidden() {
  return !extension_dispatcher_->is_extension_process();
}

bool ChromeContentRendererClient::AllowPopup() {
  extensions::ChromeV8Context* current_context =
      extension_dispatcher_->v8_context_set().GetCurrent();
  return current_context && current_context->extension() &&
      (current_context->context_type() ==
       extensions::Feature::BLESSED_EXTENSION_CONTEXT ||
       current_context->context_type() ==
       extensions::Feature::CONTENT_SCRIPT_CONTEXT);
}

bool ChromeContentRendererClient::ShouldFork(WebFrame* frame,
                                             const GURL& url,
                                             const std::string& http_method,
                                             bool is_initial_navigation,
                                             bool is_server_redirect,
                                             bool* send_referrer) {
  DCHECK(!frame->parent());

  // If this is the Instant process, fork all navigations originating from the
  // renderer.  The destination page will then be bucketed back to this Instant
  // process if it is an Instant url, or to another process if not.
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kInstantProcess))
    return true;

  // For now, we skip the rest for POST submissions.  This is because
  // http://crbug.com/101395 is more likely to cause compatibility issues
  // with hosted apps and extensions than WebUI pages.  We will remove this
  // check when cross-process POST submissions are supported.
  if (http_method != "GET")
    return false;

  // If this is the Signin process, fork all navigations originating from the
  // renderer.  The destination page will then be bucketed back to this Signin
  // process if it is a Signin url, or to another process if not.
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kSigninProcess)) {
    // We never want to allow non-signin pages to fork-on-POST to a
    // signin-related action URL. We'll need to handle this carefully once
    // http://crbug.com/101395 is fixed. The CHECK ensures we don't forget.
    CHECK_NE(http_method, "POST");
    return true;
  }

  // If |url| matches one of the prerendered URLs, stop this navigation and try
  // to swap in the prerendered page on the browser process. If the prerendered
  // page no longer exists by the time the OpenURL IPC is handled, a normal
  // navigation is attempted.
  if (prerender_dispatcher_.get() && prerender_dispatcher_->IsPrerenderURL(url))
    return true;

  const ExtensionSet* extensions = extension_dispatcher_->extensions();

  // Determine if the new URL is an extension (excluding bookmark apps).
  const Extension* new_url_extension = extensions::GetNonBookmarkAppExtension(
      *extensions, url);
  bool is_extension_url = !!new_url_extension;

  // If the navigation would cross an app extent boundary, we also need
  // to defer to the browser to ensure process isolation.  This is not necessary
  // for server redirects, which will be transferred to a new process by the
  // browser process when they are ready to commit.  It is necessary for client
  // redirects, which won't be transferred in the same way.
  if (!is_server_redirect &&
      CrossesExtensionExtents(frame, url, *extensions, is_extension_url,
          is_initial_navigation)) {
    // Include the referrer in this case since we're going from a hosted web
    // page. (the packaged case is handled previously by the extension
    // navigation test)
    *send_referrer = true;

    const Extension* extension =
        extension_dispatcher_->extensions()->GetExtensionOrAppByURL(url);
    if (extension && extension->is_app()) {
      UMA_HISTOGRAM_ENUMERATION(
          extension->is_platform_app() ?
          extension_misc::kPlatformAppLaunchHistogram :
          extension_misc::kAppLaunchHistogram,
          extension_misc::APP_LAUNCH_CONTENT_NAVIGATION,
          extension_misc::APP_LAUNCH_BUCKET_BOUNDARY);
    }
    return true;
  }

  // If this is a reload, check whether it has the wrong process type.  We
  // should send it to the browser if it's an extension URL (e.g., hosted app)
  // in a normal process, or if it's a process for an extension that has been
  // uninstalled.
  if (frame->top()->document().url() == url) {
    if (is_extension_url != extension_dispatcher_->is_extension_process())
      return true;
  }

  return false;
}

bool ChromeContentRendererClient::WillSendRequest(
    WebKit::WebFrame* frame,
    content::PageTransition transition_type,
    const GURL& url,
    const GURL& first_party_for_cookies,
    GURL* new_url) {
  // Check whether the request should be allowed. If not allowed, we reset the
  // URL to something invalid to prevent the request and cause an error.
  if (url.SchemeIs(extensions::kExtensionScheme) &&
      !extensions::ResourceRequestPolicy::CanRequestResource(
          url,
          frame,
          transition_type,
          extension_dispatcher_->extensions())) {
    *new_url = GURL(chrome::kExtensionInvalidRequestURL);
    return true;
  }

  if (url.SchemeIs(chrome::kExtensionResourceScheme) &&
      !extensions::ResourceRequestPolicy::CanRequestExtensionResourceScheme(
          url,
          frame)) {
    *new_url = GURL(chrome::kExtensionResourceInvalidRequestURL);
    return true;
  }

  const content::RenderView* render_view =
      content::RenderView::FromWebView(frame->view());
  SearchBox* search_box = SearchBox::Get(render_view);
  if (search_box && url.SchemeIs(chrome::kChromeSearchScheme)) {
    if (url.host() == chrome::kChromeUIThumbnailHost)
      return search_box->GenerateThumbnailURLFromTransientURL(url, new_url);
    else if (url.host() == chrome::kChromeUIFaviconHost)
      return search_box->GenerateFaviconURLFromTransientURL(url, new_url);
  }

  return false;
}

bool ChromeContentRendererClient::ShouldPumpEventsDuringCookieMessage() {
  // We no longer pump messages, even under Chrome Frame. We rely on cookie
  // read requests handled by CF not putting up UI or causing other actions
  // that would require us to pump messages. This fixes http://crbug.com/110090.
  return false;
}

void ChromeContentRendererClient::DidCreateScriptContext(
    WebFrame* frame, v8::Handle<v8::Context> context, int extension_group,
    int world_id) {
  extension_dispatcher_->DidCreateScriptContext(
      frame, context, extension_group, world_id);
}

void ChromeContentRendererClient::WillReleaseScriptContext(
    WebFrame* frame, v8::Handle<v8::Context> context, int world_id) {
  extension_dispatcher_->WillReleaseScriptContext(frame, context, world_id);
}

unsigned long long ChromeContentRendererClient::VisitedLinkHash(
    const char* canonical_url, size_t length) {
  return visited_link_slave_->ComputeURLFingerprint(canonical_url, length);
}

bool ChromeContentRendererClient::IsLinkVisited(unsigned long long link_hash) {
  return visited_link_slave_->IsVisited(link_hash);
}

WebKit::WebPrescientNetworking*
ChromeContentRendererClient::GetPrescientNetworking() {
  return prescient_networking_dispatcher_.get();
}

bool ChromeContentRendererClient::ShouldOverridePageVisibilityState(
    const content::RenderView* render_view,
    WebKit::WebPageVisibilityState* override_state) {
  if (!prerender::PrerenderHelper::IsPrerendering(render_view))
    return false;

  *override_state = WebKit::WebPageVisibilityStatePrerender;
  return true;
}

bool ChromeContentRendererClient::HandleGetCookieRequest(
    content::RenderView* sender,
    const GURL& url,
    const GURL& first_party_for_cookies,
    std::string* cookies) {
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kChromeFrame)) {
    IPC::SyncMessage* msg = new ChromeViewHostMsg_GetCookies(
        MSG_ROUTING_NONE, url, first_party_for_cookies, cookies);
    sender->Send(msg);
    return true;
  }
  return false;
}

bool ChromeContentRendererClient::HandleSetCookieRequest(
    content::RenderView* sender,
    const GURL& url,
    const GURL& first_party_for_cookies,
    const std::string& value) {
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kChromeFrame)) {
    sender->Send(new ChromeViewHostMsg_SetCookie(
        MSG_ROUTING_NONE, url, first_party_for_cookies, value));
    return true;
  }
  return false;
}

void ChromeContentRendererClient::SetExtensionDispatcher(
    extensions::Dispatcher* extension_dispatcher) {
  extension_dispatcher_.reset(extension_dispatcher);
  permissions_policy_delegate_.reset(
      new extensions::RendererPermissionsPolicyDelegate(
          extension_dispatcher_.get()));
}

bool ChromeContentRendererClient::CrossesExtensionExtents(
    WebFrame* frame,
    const GURL& new_url,
    const ExtensionSet& extensions,
    bool is_extension_url,
    bool is_initial_navigation) {
  GURL old_url(frame->top()->document().url());

  // If old_url is still empty and this is an initial navigation, then this is
  // a window.open operation.  We should look at the opener URL.
  if (is_initial_navigation && old_url.is_empty() && frame->opener()) {
    // If we're about to open a normal web page from a same-origin opener stuck
    // in an extension process, we want to keep it in process to allow the
    // opener to script it.
    WebDocument opener_document = frame->opener()->document();
    WebSecurityOrigin opener = frame->opener()->document().securityOrigin();
    bool opener_is_extension_url =
        !opener.isUnique() && extensions.GetExtensionOrAppByURL(
            opener_document.url()) != NULL;
    if (!is_extension_url &&
        !opener_is_extension_url &&
        extension_dispatcher_->is_extension_process() &&
        opener.canRequest(WebURL(new_url)))
      return false;

    // In all other cases, we want to compare against the top frame's URL (as
    // opposed to the opener frame's), since that's what determines the type of
    // process.  This allows iframes outside an app to open a popup in the app.
    old_url = frame->top()->opener()->top()->document().url();
  }

  // Only consider keeping non-app URLs in an app process if this window
  // has an opener (in which case it might be an OAuth popup that tries to
  // script an iframe within the app).
  bool should_consider_workaround = !!frame->opener();

  return extensions::CrossesExtensionProcessBoundary(
      extensions, old_url, new_url, should_consider_workaround);
}

#if defined(ENABLE_SPELLCHECK)
void ChromeContentRendererClient::SetSpellcheck(SpellCheck* spellcheck) {
  RenderThread* thread = RenderThread::Get();
  if (spellcheck_.get() && thread)
    thread->RemoveObserver(spellcheck_.get());
  spellcheck_.reset(spellcheck);
  SpellCheckReplacer replacer(spellcheck_.get());
  content::RenderView::ForEach(&replacer);
  if (thread)
    thread->AddObserver(spellcheck_.get());
}
#endif

void ChromeContentRendererClient::OnPurgeMemory() {
#if defined(ENABLE_SPELLCHECK)
  DVLOG(1) << "Resetting spellcheck in renderer client";
  SetSpellcheck(new SpellCheck());
#endif
}

bool ChromeContentRendererClient::IsAdblockInstalled() {
  return g_current_client->extension_dispatcher_->extensions()->Contains(
      "gighmmpiobklfepjocnamgkkbiglidom");
}

bool ChromeContentRendererClient::IsAdblockPlusInstalled() {
  return g_current_client->extension_dispatcher_->extensions()->Contains(
      "cfhdojbkjhnklbpkdaibdccddilifddb");
}

bool ChromeContentRendererClient::IsAdblockWithWebRequestInstalled() {
  return g_current_client->extension_dispatcher_->
      IsAdblockWithWebRequestInstalled();
}

bool ChromeContentRendererClient::IsAdblockPlusWithWebRequestInstalled() {
  return g_current_client->extension_dispatcher_->
      IsAdblockPlusWithWebRequestInstalled();
}

bool ChromeContentRendererClient::IsOtherExtensionWithWebRequestInstalled() {
  return g_current_client->extension_dispatcher_->
      IsOtherExtensionWithWebRequestInstalled();
}

const void* ChromeContentRendererClient::CreatePPAPIInterface(
    const std::string& interface_name) {
#if defined(ENABLE_PLUGINS)
#if !defined(DISABLE_NACL)
  if (interface_name == PPB_NACL_PRIVATE_INTERFACE)
    return PPB_NaCl_Private_Impl::GetInterface();
#endif  // DISABLE_NACL
  if (interface_name == PPB_PDF_INTERFACE)
    return PPB_PDF_Impl::GetInterface();
#endif
  return NULL;
}

bool ChromeContentRendererClient::IsExternalPepperPlugin(
    const std::string& module_name) {
  // TODO(bbudge) remove this when the trusted NaCl plugin has been removed.
  // We must defer certain plugin events for NaCl instances since we switch
  // from the in-process to the out-of-process proxy after instantiating them.
  return module_name == "Native Client";
}

bool ChromeContentRendererClient::IsPluginAllowedToCallRequestOSFileHandle(
    WebKit::WebPluginContainer* container) {
#if defined(ENABLE_PLUGINS)
  if (!container)
    return false;
  GURL url = container->element().document().baseURL();
  const ExtensionSet* extension_set = extension_dispatcher_->extensions();

  return IsExtensionOrSharedModuleWhitelisted(url, extension_set,
                                              allowed_file_handle_origins_) ||
         IsHostAllowedByCommandLine(url, extension_set,
                                    switches::kAllowNaClFileHandleAPI);
#else
  return false;
#endif
}

WebKit::WebSpeechSynthesizer*
ChromeContentRendererClient::OverrideSpeechSynthesizer(
    WebKit::WebSpeechSynthesizerClient* client) {
  return new TtsDispatcher(client);
}

bool ChromeContentRendererClient::AllowBrowserPlugin(
    WebKit::WebPluginContainer* container) {
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableBrowserPluginForAllViewTypes))
    return true;

  // If this |BrowserPlugin| <object> in the |container| is not inside a
  // <webview>/<adview> shadowHost, we disable instantiating this plugin. This
  // is to discourage and prevent developers from accidentally attaching
  // <object> directly in apps.
  //
  // Note that this check below does *not* ensure any security, it is still
  // possible to bypass this check.
  // TODO(lazyboy): http://crbug.com/178663, Ensure we properly disallow
  // instantiating BrowserPlugin outside of the <webview>/<adview> shim.
  if (container->element().isNull())
    return false;

  if (container->element().shadowHost().isNull())
    return false;

  WebString tag_name = container->element().shadowHost().tagName();
  return tag_name.equals(WebString::fromUTF8(kWebViewTagName)) ||
    tag_name.equals(WebString::fromUTF8(kAdViewTagName));
}

bool ChromeContentRendererClient::AllowPepperMediaStreamAPI(
    const GURL& url) {
#if !defined(OS_ANDROID)
  std::string host = url.host();
  // Allow only the Hangouts app to use the MediaStream APIs. It's OK to check
  // the whitelist in the renderer, since we're only preventing access until
  // these APIs are public and stable.
  if (url.SchemeIs(extensions::kExtensionScheme) &&
      !host.compare("hpcogiolnobbkijnnkdahioejpdcdoph")) {
    return true;
  }
  // Allow access for tests.
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnablePepperTesting)) {
    return true;
  }
#endif  // !defined(OS_ANDROID)
  return false;
}


}  // namespace chrome
