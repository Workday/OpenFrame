// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/website_settings/website_settings.h"

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/i18n/time_formatting.h"
#include "base/metrics/histogram.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browsing_data/browsing_data_cookie_helper.h"
#include "chrome/browser/browsing_data/browsing_data_database_helper.h"
#include "chrome/browser/browsing_data/browsing_data_file_system_helper.h"
#include "chrome/browser/browsing_data/browsing_data_indexed_db_helper.h"
#include "chrome/browser/browsing_data/browsing_data_local_storage_helper.h"
#include "chrome/browser/browsing_data/browsing_data_server_bound_cert_helper.h"
#include "chrome/browser/content_settings/content_settings_utils.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/content_settings/local_shared_objects_container.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/policy/profile_policy_connector_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ssl/ssl_error_info.h"
#include "chrome/browser/ui/website_settings/website_settings_infobar_delegate.h"
#include "chrome/browser/ui/website_settings/website_settings_ui.h"
#include "chrome/common/content_settings_pattern.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/cert_store.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/ssl_status.h"
#include "content/public/common/url_constants.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/cert/cert_status_flags.h"
#include "net/cert/x509_certificate.h"
#include "net/ssl/ssl_cipher_suite_names.h"
#include "net/ssl/ssl_connection_status_flags.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

using content::BrowserThread;

namespace {

// The list of content settings types to display on the Website Settings UI.
ContentSettingsType kPermissionType[] = {
  CONTENT_SETTINGS_TYPE_IMAGES,
  CONTENT_SETTINGS_TYPE_JAVASCRIPT,
  CONTENT_SETTINGS_TYPE_PLUGINS,
  CONTENT_SETTINGS_TYPE_POPUPS,
  CONTENT_SETTINGS_TYPE_GEOLOCATION,
  CONTENT_SETTINGS_TYPE_NOTIFICATIONS,
  CONTENT_SETTINGS_TYPE_FULLSCREEN,
  CONTENT_SETTINGS_TYPE_MOUSELOCK,
  CONTENT_SETTINGS_TYPE_MEDIASTREAM,
  CONTENT_SETTINGS_TYPE_AUTOMATIC_DOWNLOADS,
  CONTENT_SETTINGS_TYPE_MIDI_SYSEX,
};

}  // namespace

WebsiteSettings::WebsiteSettings(
    WebsiteSettingsUI* ui,
    Profile* profile,
    TabSpecificContentSettings* tab_specific_content_settings,
    InfoBarService* infobar_service,
    const GURL& url,
    const content::SSLStatus& ssl,
    content::CertStore* cert_store)
    : TabSpecificContentSettings::SiteDataObserver(
          tab_specific_content_settings),
      ui_(ui),
      infobar_service_(infobar_service),
      show_info_bar_(false),
      site_url_(url),
      site_identity_status_(SITE_IDENTITY_STATUS_UNKNOWN),
      cert_id_(0),
      site_connection_status_(SITE_CONNECTION_STATUS_UNKNOWN),
      cert_store_(cert_store),
      content_settings_(profile->GetHostContentSettingsMap()) {
  Init(profile, url, ssl);

  HistoryService* history_service = HistoryServiceFactory::GetForProfile(
      profile, Profile::EXPLICIT_ACCESS);
  if (history_service) {
    history_service->GetVisibleVisitCountToHost(
        site_url_,
        &visit_count_request_consumer_,
        base::Bind(&WebsiteSettings::OnGotVisitCountToHost,
                   base::Unretained(this)));
  }

  PresentSitePermissions();
  PresentSiteData();
  PresentSiteIdentity();
  PresentHistoryInfo(base::Time());

  // Every time the Website Settings UI is opened a |WebsiteSettings| object is
  // created. So this counts how ofter the Website Settings UI is opened.
  content::RecordAction(content::UserMetricsAction("WebsiteSettings_Opened"));
}

WebsiteSettings::~WebsiteSettings() {
}

void WebsiteSettings::OnSitePermissionChanged(ContentSettingsType type,
                                              ContentSetting setting) {
  // Count how often a permission for a specific content type is changed using
  // the Website Settings UI.
  UMA_HISTOGRAM_COUNTS("WebsiteSettings.PermissionChanged", type);

  ContentSettingsPattern primary_pattern;
  ContentSettingsPattern secondary_pattern;
  switch (type) {
    case CONTENT_SETTINGS_TYPE_GEOLOCATION:
    case CONTENT_SETTINGS_TYPE_MIDI_SYSEX:
      // TODO(markusheintz): The rule we create here should also change the
      // location permission for iframed content.
      primary_pattern = ContentSettingsPattern::FromURLNoWildcard(site_url_);
      secondary_pattern = ContentSettingsPattern::FromURLNoWildcard(site_url_);
      break;
    case CONTENT_SETTINGS_TYPE_NOTIFICATIONS:
      primary_pattern = ContentSettingsPattern::FromURLNoWildcard(site_url_);
      secondary_pattern = ContentSettingsPattern::Wildcard();
      break;
    case CONTENT_SETTINGS_TYPE_IMAGES:
    case CONTENT_SETTINGS_TYPE_JAVASCRIPT:
    case CONTENT_SETTINGS_TYPE_PLUGINS:
    case CONTENT_SETTINGS_TYPE_POPUPS:
    case CONTENT_SETTINGS_TYPE_FULLSCREEN:
    case CONTENT_SETTINGS_TYPE_MOUSELOCK:
    case CONTENT_SETTINGS_TYPE_AUTOMATIC_DOWNLOADS:
      primary_pattern = ContentSettingsPattern::FromURL(site_url_);
      secondary_pattern = ContentSettingsPattern::Wildcard();
      break;
    case CONTENT_SETTINGS_TYPE_MEDIASTREAM: {
      // We need to use the same same patterns as other places like infobar code
      // to override the existing rule instead of creating the new one.
      primary_pattern = ContentSettingsPattern::FromURLNoWildcard(site_url_);
      secondary_pattern = ContentSettingsPattern::Wildcard();
      // Set permission for both microphone and camera.
      content_settings_->SetContentSetting(
          primary_pattern,
          secondary_pattern,
          CONTENT_SETTINGS_TYPE_MEDIASTREAM_MIC,
          std::string(),
          setting);

      content_settings_->SetContentSetting(
          primary_pattern,
          secondary_pattern,
          CONTENT_SETTINGS_TYPE_MEDIASTREAM_CAMERA,
          std::string(),
          setting);
      break;
    }
    default:
      NOTREACHED() << "ContentSettingsType " << type << "is not supported.";
      break;
  }

  if (type != CONTENT_SETTINGS_TYPE_MEDIASTREAM) {
    // Permission settings are specified via rules. There exists always at least
    // one rule for the default setting. Get the rule that currently defines
    // the permission for the given permission |type|. Then test whether the
    // existing rule is more specific than the rule we are about to create. If
    // the existing rule is more specific, than change the existing rule instead
    // of creating a new rule that would be hidden behind the existing rule.
    // This is not a concern for CONTENT_SETTINGS_TYPE_MEDIASTREAM since users
    // can not create media settings exceptions by hand.
    content_settings::SettingInfo info;
    scoped_ptr<Value> v(content_settings_->GetWebsiteSetting(
        site_url_, site_url_, type, std::string(), &info));
    DCHECK(info.source == content_settings::SETTING_SOURCE_USER);
    ContentSettingsPattern::Relation r1 =
        info.primary_pattern.Compare(primary_pattern);
    DCHECK(r1 != ContentSettingsPattern::DISJOINT_ORDER_POST &&
           r1 != ContentSettingsPattern::DISJOINT_ORDER_PRE);
    if (r1 == ContentSettingsPattern::PREDECESSOR) {
      primary_pattern = info.primary_pattern;
    } else if (r1 == ContentSettingsPattern::IDENTITY) {
      ContentSettingsPattern::Relation r2 =
          info.secondary_pattern.Compare(secondary_pattern);
      DCHECK(r2 != ContentSettingsPattern::DISJOINT_ORDER_POST &&
             r2 != ContentSettingsPattern::DISJOINT_ORDER_PRE);
      if (r2 == ContentSettingsPattern::PREDECESSOR)
        secondary_pattern = info.secondary_pattern;
    }

    Value* value = NULL;
    if (setting != CONTENT_SETTING_DEFAULT)
      value = Value::CreateIntegerValue(setting);
    content_settings_->SetWebsiteSetting(
        primary_pattern, secondary_pattern, type, std::string(), value);
  }

  show_info_bar_ = true;

// TODO(markusheintz): This is a temporary hack to fix issue:
// http://crbug.com/144203.
#if defined(OS_MACOSX)
  // Refresh the UI to reflect the new setting.
  PresentSitePermissions();
#endif
}

void WebsiteSettings::OnGotVisitCountToHost(HistoryService::Handle handle,
                                            bool found_visits,
                                            int visit_count,
                                            base::Time first_visit) {
  if (!found_visits) {
    // This indicates an error, such as the page's URL scheme wasn't
    // http/https.
    first_visit = base::Time();
  } else if (visit_count == 0) {
    first_visit = base::Time::Now();
  }
  PresentHistoryInfo(first_visit);
}

void WebsiteSettings::OnSiteDataAccessed() {
  PresentSiteData();
}

void WebsiteSettings::OnUIClosing() {
  if (show_info_bar_)
    WebsiteSettingsInfoBarDelegate::Create(infobar_service_);
}

void WebsiteSettings::Init(Profile* profile,
                           const GURL& url,
                           const content::SSLStatus& ssl) {
  if (url.SchemeIs(chrome::kChromeUIScheme)) {
    site_identity_status_ = SITE_IDENTITY_STATUS_INTERNAL_PAGE;
    site_identity_details_ =
        l10n_util::GetStringUTF16(IDS_PAGE_INFO_INTERNAL_PAGE);
    site_connection_status_ = SITE_CONNECTION_STATUS_INTERNAL_PAGE;
    return;
  }

  scoped_refptr<net::X509Certificate> cert;

  // Identity section.
  string16 subject_name(UTF8ToUTF16(url.host()));
  if (subject_name.empty()) {
    subject_name.assign(
        l10n_util::GetStringUTF16(IDS_PAGE_INFO_SECURITY_TAB_UNKNOWN_PARTY));
  }

  cert_id_ = ssl.cert_id;

  if (ssl.cert_id &&
      cert_store_->RetrieveCert(ssl.cert_id, &cert) &&
      (!net::IsCertStatusError(ssl.cert_status) ||
       net::IsCertStatusMinorError(ssl.cert_status))) {
    // There are no major errors. Check for minor errors.
    if (policy::ProfilePolicyConnectorFactory::GetForProfile(profile)->
        UsedPolicyCertificates()) {
      site_identity_status_ = SITE_IDENTITY_STATUS_ADMIN_PROVIDED_CERT;
      site_identity_details_ =
          l10n_util::GetStringFUTF16(IDS_CERT_POLICY_PROVIDED_CERT_MESSAGE,
                                     UTF8ToUTF16(url.host()));
    } else if (net::IsCertStatusMinorError(ssl.cert_status)) {
      site_identity_status_ = SITE_IDENTITY_STATUS_CERT_REVOCATION_UNKNOWN;
      string16 issuer_name(UTF8ToUTF16(cert->issuer().GetDisplayName()));
      if (issuer_name.empty()) {
        issuer_name.assign(l10n_util::GetStringUTF16(
            IDS_PAGE_INFO_SECURITY_TAB_UNKNOWN_PARTY));
      }
      site_identity_details_.assign(l10n_util::GetStringFUTF16(
          IDS_PAGE_INFO_SECURITY_TAB_SECURE_IDENTITY, issuer_name));

      site_identity_details_ += ASCIIToUTF16("\n\n");
      if (ssl.cert_status & net::CERT_STATUS_UNABLE_TO_CHECK_REVOCATION) {
        site_identity_details_ += l10n_util::GetStringUTF16(
            IDS_PAGE_INFO_SECURITY_TAB_UNABLE_TO_CHECK_REVOCATION);
      } else if (ssl.cert_status & net::CERT_STATUS_NO_REVOCATION_MECHANISM) {
        site_identity_details_ += l10n_util::GetStringUTF16(
            IDS_PAGE_INFO_SECURITY_TAB_NO_REVOCATION_MECHANISM);
      } else {
        NOTREACHED() << "Need to specify string for this warning";
      }
    } else if (ssl.cert_status & net::CERT_STATUS_IS_EV) {
      // EV HTTPS page.
      site_identity_status_ = SITE_IDENTITY_STATUS_EV_CERT;
      DCHECK(!cert->subject().organization_names.empty());
      organization_name_ = UTF8ToUTF16(cert->subject().organization_names[0]);
      // An EV Cert is required to have a city (localityName) and country but
      // state is "if any".
      DCHECK(!cert->subject().locality_name.empty());
      DCHECK(!cert->subject().country_name.empty());
      string16 locality;
      if (!cert->subject().state_or_province_name.empty()) {
        locality = l10n_util::GetStringFUTF16(
            IDS_PAGEINFO_ADDRESS,
            UTF8ToUTF16(cert->subject().locality_name),
            UTF8ToUTF16(cert->subject().state_or_province_name),
            UTF8ToUTF16(cert->subject().country_name));
      } else {
        locality = l10n_util::GetStringFUTF16(
            IDS_PAGEINFO_PARTIAL_ADDRESS,
            UTF8ToUTF16(cert->subject().locality_name),
            UTF8ToUTF16(cert->subject().country_name));
      }
      DCHECK(!cert->subject().organization_names.empty());
      site_identity_details_.assign(l10n_util::GetStringFUTF16(
          IDS_PAGE_INFO_SECURITY_TAB_SECURE_IDENTITY_EV,
          UTF8ToUTF16(cert->subject().organization_names[0]),
          locality,
          UTF8ToUTF16(cert->issuer().GetDisplayName())));
    } else {
      // Non-EV OK HTTPS page.
      site_identity_status_ = SITE_IDENTITY_STATUS_CERT;
      string16 issuer_name(UTF8ToUTF16(cert->issuer().GetDisplayName()));
      if (issuer_name.empty()) {
        issuer_name.assign(l10n_util::GetStringUTF16(
            IDS_PAGE_INFO_SECURITY_TAB_UNKNOWN_PARTY));
      }
      site_identity_details_.assign(l10n_util::GetStringFUTF16(
          IDS_PAGE_INFO_SECURITY_TAB_SECURE_IDENTITY, issuer_name));
    }
  } else {
    // HTTP or HTTPS with errors (not warnings).
    site_identity_details_.assign(l10n_util::GetStringUTF16(
        IDS_PAGE_INFO_SECURITY_TAB_INSECURE_IDENTITY));
    if (ssl.security_style == content::SECURITY_STYLE_UNAUTHENTICATED)
      site_identity_status_ = SITE_IDENTITY_STATUS_NO_CERT;
    else
      site_identity_status_ = SITE_IDENTITY_STATUS_ERROR;

    const string16 bullet = UTF8ToUTF16("\n • ");
    std::vector<SSLErrorInfo> errors;
    SSLErrorInfo::GetErrorsForCertStatus(ssl.cert_id, ssl.cert_status,
                                         url, &errors);
    for (size_t i = 0; i < errors.size(); ++i) {
      site_identity_details_ += bullet;
      site_identity_details_ += errors[i].short_description();
    }

    if (ssl.cert_status & net::CERT_STATUS_NON_UNIQUE_NAME) {
      site_identity_details_ += ASCIIToUTF16("\n\n");
      site_identity_details_ += l10n_util::GetStringUTF16(
          IDS_PAGE_INFO_SECURITY_TAB_NON_UNIQUE_NAME);
    }
  }

  // Site Connection
  // We consider anything less than 80 bits encryption to be weak encryption.
  // TODO(wtc): Bug 1198735: report mixed/unsafe content for unencrypted and
  // weakly encrypted connections.
  site_connection_status_ = SITE_CONNECTION_STATUS_UNKNOWN;

  if (!ssl.cert_id) {
    // Not HTTPS.
    DCHECK_EQ(ssl.security_style, content::SECURITY_STYLE_UNAUTHENTICATED);
    if (ssl.security_style == content::SECURITY_STYLE_UNAUTHENTICATED)
      site_connection_status_ = SITE_CONNECTION_STATUS_UNENCRYPTED;
    else
      site_connection_status_ = SITE_CONNECTION_STATUS_ENCRYPTED_ERROR;

    site_connection_details_.assign(l10n_util::GetStringFUTF16(
        IDS_PAGE_INFO_SECURITY_TAB_NOT_ENCRYPTED_CONNECTION_TEXT,
        subject_name));
  } else if (ssl.security_bits < 0) {
    // Security strength is unknown.  Say nothing.
    site_connection_status_ = SITE_CONNECTION_STATUS_ENCRYPTED_ERROR;
  } else if (ssl.security_bits == 0) {
    DCHECK_NE(ssl.security_style, content::SECURITY_STYLE_UNAUTHENTICATED);
    site_connection_status_ = SITE_CONNECTION_STATUS_ENCRYPTED_ERROR;
    site_connection_details_.assign(l10n_util::GetStringFUTF16(
        IDS_PAGE_INFO_SECURITY_TAB_NOT_ENCRYPTED_CONNECTION_TEXT,
        subject_name));
  } else if (ssl.security_bits < 80) {
    site_connection_status_ = SITE_CONNECTION_STATUS_ENCRYPTED_ERROR;
    site_connection_details_.assign(l10n_util::GetStringFUTF16(
        IDS_PAGE_INFO_SECURITY_TAB_WEAK_ENCRYPTION_CONNECTION_TEXT,
        subject_name));
  } else {
    site_connection_status_ = SITE_CONNECTION_STATUS_ENCRYPTED;
    site_connection_details_.assign(l10n_util::GetStringFUTF16(
        IDS_PAGE_INFO_SECURITY_TAB_ENCRYPTED_CONNECTION_TEXT,
        subject_name,
        base::IntToString16(ssl.security_bits)));
    if (ssl.content_status) {
      bool ran_insecure_content =
          !!(ssl.content_status & content::SSLStatus::RAN_INSECURE_CONTENT);
      site_connection_status_ = ran_insecure_content ?
          SITE_CONNECTION_STATUS_ENCRYPTED_ERROR
          : SITE_CONNECTION_STATUS_MIXED_CONTENT;
      site_connection_details_.assign(l10n_util::GetStringFUTF16(
          IDS_PAGE_INFO_SECURITY_TAB_ENCRYPTED_SENTENCE_LINK,
          site_connection_details_,
          l10n_util::GetStringUTF16(ran_insecure_content ?
              IDS_PAGE_INFO_SECURITY_TAB_ENCRYPTED_INSECURE_CONTENT_ERROR :
              IDS_PAGE_INFO_SECURITY_TAB_ENCRYPTED_INSECURE_CONTENT_WARNING)));
    }
  }

  uint16 cipher_suite =
      net::SSLConnectionStatusToCipherSuite(ssl.connection_status);
  if (ssl.security_bits > 0 && cipher_suite) {
    int ssl_version =
        net::SSLConnectionStatusToVersion(ssl.connection_status);
    const char* ssl_version_str;
    net::SSLVersionToString(&ssl_version_str, ssl_version);
    site_connection_details_ += ASCIIToUTF16("\n\n");
    site_connection_details_ += l10n_util::GetStringFUTF16(
        IDS_PAGE_INFO_SECURITY_TAB_SSL_VERSION,
        ASCIIToUTF16(ssl_version_str));

    bool did_fallback = (ssl.connection_status &
                         net::SSL_CONNECTION_VERSION_FALLBACK) != 0;
    bool no_renegotiation =
        (ssl.connection_status &
        net::SSL_CONNECTION_NO_RENEGOTIATION_EXTENSION) != 0;
    const char *key_exchange, *cipher, *mac;
    bool is_aead;
    net::SSLCipherSuiteToStrings(
        &key_exchange, &cipher, &mac, &is_aead, cipher_suite);

    site_connection_details_ += ASCIIToUTF16("\n\n");
    if (is_aead) {
      site_connection_details_ += l10n_util::GetStringFUTF16(
          IDS_PAGE_INFO_SECURITY_TAB_ENCRYPTION_DETAILS_AEAD,
          ASCIIToUTF16(cipher), ASCIIToUTF16(key_exchange));
    } else {
      site_connection_details_ += l10n_util::GetStringFUTF16(
          IDS_PAGE_INFO_SECURITY_TAB_ENCRYPTION_DETAILS,
          ASCIIToUTF16(cipher), ASCIIToUTF16(mac), ASCIIToUTF16(key_exchange));
    }

    if (did_fallback) {
      // For now, only SSLv3 fallback will trigger a warning icon.
      if (site_connection_status_ < SITE_CONNECTION_STATUS_MIXED_CONTENT)
        site_connection_status_ = SITE_CONNECTION_STATUS_MIXED_CONTENT;
      site_connection_details_ += ASCIIToUTF16("\n\n");
      site_connection_details_ += l10n_util::GetStringUTF16(
          IDS_PAGE_INFO_SECURITY_TAB_FALLBACK_MESSAGE);
    }
    if (no_renegotiation) {
      site_connection_details_ += ASCIIToUTF16("\n\n");
      site_connection_details_ += l10n_util::GetStringUTF16(
          IDS_PAGE_INFO_SECURITY_TAB_RENEGOTIATION_MESSAGE);
    }
  }

  // By default select the permissions tab that displays all the site
  // permissions. In case of a connection error or an issue with the
  // certificate presented by the website, select the connection tab to draw
  // the user's attention to the issue. If the site does not provide a
  // certificate because it was loaded over an unencrypted connection, don't
  // select the connection tab.
  WebsiteSettingsUI::TabId tab_id = WebsiteSettingsUI::TAB_ID_PERMISSIONS;
  if (site_connection_status_ == SITE_CONNECTION_STATUS_ENCRYPTED_ERROR ||
      site_connection_status_ == SITE_CONNECTION_STATUS_MIXED_CONTENT ||
      site_identity_status_ == SITE_IDENTITY_STATUS_ERROR ||
      site_identity_status_ == SITE_IDENTITY_STATUS_CERT_REVOCATION_UNKNOWN ||
      site_identity_status_ == SITE_IDENTITY_STATUS_ADMIN_PROVIDED_CERT)
    tab_id = WebsiteSettingsUI::TAB_ID_CONNECTION;
  ui_->SetSelectedTab(tab_id);
}

void WebsiteSettings::PresentSitePermissions() {
  PermissionInfoList permission_info_list;

  WebsiteSettingsUI::PermissionInfo permission_info;
  for (size_t i = 0; i < arraysize(kPermissionType); ++i) {
    permission_info.type = kPermissionType[i];
    if (permission_info.type == CONTENT_SETTINGS_TYPE_MIDI_SYSEX) {
      const CommandLine* command_line = CommandLine::ForCurrentProcess();
      if (!command_line->HasSwitch(switches::kEnableWebMIDI))
        continue;
    }

    content_settings::SettingInfo info;
    if (permission_info.type == CONTENT_SETTINGS_TYPE_MEDIASTREAM) {
      scoped_ptr<base::Value> mic_value(content_settings_->GetWebsiteSetting(
          site_url_,
          site_url_,
          CONTENT_SETTINGS_TYPE_MEDIASTREAM_MIC,
          std::string(),
          &info));
      ContentSetting mic_setting =
          content_settings::ValueToContentSetting(mic_value.get());

      scoped_ptr<base::Value> camera_value(content_settings_->GetWebsiteSetting(
          site_url_,
          site_url_,
          CONTENT_SETTINGS_TYPE_MEDIASTREAM_CAMERA,
          std::string(),
          &info));
      ContentSetting camera_setting =
          content_settings::ValueToContentSetting(camera_value.get());

      if (mic_setting != camera_setting || mic_setting == CONTENT_SETTING_ASK)
        permission_info.setting = CONTENT_SETTING_DEFAULT;
      else
        permission_info.setting = mic_setting;
    } else {
      scoped_ptr<Value> value(content_settings_->GetWebsiteSetting(
          site_url_, site_url_, permission_info.type, std::string(), &info));
      DCHECK(value.get());
      if (value->GetType() == Value::TYPE_INTEGER) {
        permission_info.setting =
            content_settings::ValueToContentSetting(value.get());
      } else {
        NOTREACHED();
      }
    }

    permission_info.source = info.source;

    if (info.primary_pattern == ContentSettingsPattern::Wildcard() &&
        info.secondary_pattern == ContentSettingsPattern::Wildcard() &&
        permission_info.type != CONTENT_SETTINGS_TYPE_MEDIASTREAM) {
      permission_info.default_setting = permission_info.setting;
      permission_info.setting = CONTENT_SETTING_DEFAULT;
    } else {
      permission_info.default_setting =
          content_settings_->GetDefaultContentSetting(permission_info.type,
                                                      NULL);
    }
    permission_info_list.push_back(permission_info);
  }

  ui_->SetPermissionInfo(permission_info_list);
}

void WebsiteSettings::PresentSiteData() {
  CookieInfoList cookie_info_list;
  const LocalSharedObjectsContainer& allowed_objects =
      tab_specific_content_settings()->allowed_local_shared_objects();
  const LocalSharedObjectsContainer& blocked_objects =
      tab_specific_content_settings()->blocked_local_shared_objects();

  // Add first party cookie and site data counts.
  WebsiteSettingsUI::CookieInfo cookie_info;
  std::string cookie_source =
      net::registry_controlled_domains::GetDomainAndRegistry(
          site_url_,
          net::registry_controlled_domains::EXCLUDE_PRIVATE_REGISTRIES);
  if (cookie_source.empty())
    cookie_source = site_url_.host();
  cookie_info.cookie_source = cookie_source;
  cookie_info.allowed = allowed_objects.GetObjectCountForDomain(site_url_);
  cookie_info.blocked = blocked_objects.GetObjectCountForDomain(site_url_);
  cookie_info_list.push_back(cookie_info);

  // Add third party cookie counts.
  cookie_info.cookie_source = l10n_util::GetStringUTF8(
     IDS_WEBSITE_SETTINGS_THIRD_PARTY_SITE_DATA);
  cookie_info.allowed = allowed_objects.GetObjectCount() - cookie_info.allowed;
  cookie_info.blocked = blocked_objects.GetObjectCount() - cookie_info.blocked;
  cookie_info_list.push_back(cookie_info);

  ui_->SetCookieInfo(cookie_info_list);
}

void WebsiteSettings::PresentSiteIdentity() {
  // After initialization the status about the site's connection
  // and it's identity must be available.
  DCHECK_NE(site_identity_status_, SITE_IDENTITY_STATUS_UNKNOWN);
  DCHECK_NE(site_connection_status_, SITE_CONNECTION_STATUS_UNKNOWN);
  WebsiteSettingsUI::IdentityInfo info;
  if (site_identity_status_ == SITE_IDENTITY_STATUS_EV_CERT)
    info.site_identity = UTF16ToUTF8(organization_name());
  else
    info.site_identity = site_url_.host();

  info.connection_status = site_connection_status_;
  info.connection_status_description =
      UTF16ToUTF8(site_connection_details_);
  info.identity_status = site_identity_status_;
  info.identity_status_description =
      UTF16ToUTF8(site_identity_details_);
  info.cert_id = cert_id_;
  ui_->SetIdentityInfo(info);
}

void WebsiteSettings::PresentHistoryInfo(base::Time first_visit) {
  if (first_visit == base::Time()) {
    ui_->SetFirstVisit(string16());
    return;
  }

  bool visited_before_today = false;
  base::Time today = base::Time::Now().LocalMidnight();
  base::Time first_visit_midnight = first_visit.LocalMidnight();
  visited_before_today = (first_visit_midnight < today);

  string16 first_visit_text;
  if (visited_before_today) {
    first_visit_text = l10n_util::GetStringFUTF16(
        IDS_PAGE_INFO_SECURITY_TAB_VISITED_BEFORE_TODAY,
        base::TimeFormatShortDate(first_visit));
  } else {
    first_visit_text = l10n_util::GetStringUTF16(
        IDS_PAGE_INFO_SECURITY_TAB_FIRST_VISITED_TODAY);

  }
  ui_->SetFirstVisit(first_visit_text);
}
