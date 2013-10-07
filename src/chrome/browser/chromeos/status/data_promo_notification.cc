// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/status/data_promo_notification.h"

#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/system/chromeos/network/network_observer.h"
#include "ash/system/tray/system_tray.h"
#include "ash/system/tray/system_tray_notifier.h"
#include "base/prefs/pref_registry_simple.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "chrome/browser/chromeos/mobile_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/pref_names.h"
#include "chromeos/login/login_state.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace {

// Time in milliseconds to delay showing of promo
// notification when Chrome window is not on screen.
const int kPromoShowDelayMs = 10000;

const int kNotificationCountPrefDefault = -1;

bool GetBooleanPref(const char* pref_name) {
  Profile* profile = ProfileManager::GetDefaultProfileOrOffTheRecord();
  PrefService* prefs = profile->GetPrefs();
  return prefs->GetBoolean(pref_name);
}

int GetIntegerLocalPref(const char* pref_name) {
  PrefService* prefs = g_browser_process->local_state();
  return prefs->GetInteger(pref_name);
}

void SetBooleanPref(const char* pref_name, bool value) {
  Profile* profile = ProfileManager::GetDefaultProfileOrOffTheRecord();
  PrefService* prefs = profile->GetPrefs();
  prefs->SetBoolean(pref_name, value);
}

void SetIntegerLocalPref(const char* pref_name, int value) {
  PrefService* prefs = g_browser_process->local_state();
  prefs->SetInteger(pref_name, value);
}

// Returns prefs::kShow3gPromoNotification or false
// if there's no active browser.
bool ShouldShow3gPromoNotification() {
  return GetBooleanPref(prefs::kShow3gPromoNotification);
}

void SetShow3gPromoNotification(bool value) {
  SetBooleanPref(prefs::kShow3gPromoNotification, value);
}

// Returns prefs::kCarrierDealPromoShown which is number of times
// carrier deal notification has been shown to users on this machine.
int GetCarrierDealPromoShown() {
  return GetIntegerLocalPref(prefs::kCarrierDealPromoShown);
}

void SetCarrierDealPromoShown(int value) {
  SetIntegerLocalPref(prefs::kCarrierDealPromoShown, value);
}

const chromeos::MobileConfig::Carrier* GetCarrier(
    chromeos::NetworkLibrary* cros) {
  std::string carrier_id = cros->GetCellularHomeCarrierId();
  if (carrier_id.empty()) {
    LOG(ERROR) << "Empty carrier ID with a cellular connected.";
    return NULL;
  }

  chromeos::MobileConfig* config = chromeos::MobileConfig::GetInstance();
  if (!config->IsReady())
    return NULL;

  return config->GetCarrier(carrier_id);
}

const chromeos::MobileConfig::CarrierDeal* GetCarrierDeal(
    const chromeos::MobileConfig::Carrier* carrier) {
  const chromeos::MobileConfig::CarrierDeal* deal = carrier->GetDefaultDeal();
  if (deal) {
    // Check deal for validity.
    int carrier_deal_promo_pref = GetCarrierDealPromoShown();
    if (carrier_deal_promo_pref >= deal->notification_count())
      return NULL;
    const std::string locale = g_browser_process->GetApplicationLocale();
    std::string deal_text = deal->GetLocalizedString(locale,
                                                     "notification_text");
    if (deal_text.empty())
      return NULL;
  }
  return deal;
}

}  // namespace

namespace chromeos {

////////////////////////////////////////////////////////////////////////////////
// DataPromoNotification

DataPromoNotification::DataPromoNotification()
    : check_for_promo_(true),
      weak_ptr_factory_(this) {
}

DataPromoNotification::~DataPromoNotification() {
  CloseNotification();
}

void DataPromoNotification::RegisterPrefs(PrefRegistrySimple* registry) {
  // Carrier deal notification shown count defaults to 0.
  registry->RegisterIntegerPref(prefs::kCarrierDealPromoShown, 0);
}

void DataPromoNotification::ShowOptionalMobileDataPromoNotification(
    NetworkLibrary* cros,
    views::View* host,
    ash::NetworkTrayDelegate* listener) {
  // Display one-time notification for regular users on first use
  // of Mobile Data connection or if there's a carrier deal defined
  // show that even if user has already seen generic promo.
  if (LoginState::Get()->IsUserAuthenticated() &&
      check_for_promo_ &&
      cros->cellular_connected() && !cros->ethernet_connected() &&
      !cros->wifi_connected() && !cros->wimax_connected()) {
    std::string deal_text;
    int carrier_deal_promo_pref = kNotificationCountPrefDefault;
    const MobileConfig::CarrierDeal* deal = NULL;
    const MobileConfig::Carrier* carrier = GetCarrier(cros);
    if (carrier)
      deal = GetCarrierDeal(carrier);
    deal_info_url_.clear();
    deal_topup_url_.clear();
    if (deal) {
      carrier_deal_promo_pref = GetCarrierDealPromoShown();
      const std::string locale = g_browser_process->GetApplicationLocale();
      deal_text = deal->GetLocalizedString(locale, "notification_text");
      deal_info_url_ = deal->info_url();
      deal_topup_url_ = carrier->top_up_url();
    } else if (!ShouldShow3gPromoNotification()) {
      check_for_promo_ = false;
      return;
    }

    const chromeos::CellularNetwork* cellular = cros->cellular_network();
    DCHECK(cellular);
    // If we do not know the technology type, do not show the notification yet.
    // The next NetworkLibrary Manager update should trigger it.
    if (cellular->network_technology() == NETWORK_TECHNOLOGY_UNKNOWN)
      return;

    string16 message = l10n_util::GetStringUTF16(IDS_3G_NOTIFICATION_MESSAGE);
    if (!deal_text.empty())
      message = UTF8ToUTF16(deal_text + "\n\n") + message;

    // Use deal URL if it's defined or general "Network Settings" URL.
    int link_message_id;
    if (deal_topup_url_.empty())
      link_message_id = IDS_OFFLINE_NETWORK_SETTINGS;
    else
      link_message_id = IDS_STATUSBAR_NETWORK_VIEW_ACCOUNT;

    ash::NetworkObserver::NetworkType type =
        (cellular->network_technology() == NETWORK_TECHNOLOGY_LTE ||
         cellular->network_technology() == NETWORK_TECHNOLOGY_LTE_ADVANCED)
        ? ash::NetworkObserver::NETWORK_CELLULAR_LTE
        : ash::NetworkObserver::NETWORK_CELLULAR;

    std::vector<string16> links;
    links.push_back(l10n_util::GetStringUTF16(link_message_id));
    if (!deal_info_url_.empty())
      links.push_back(l10n_util::GetStringUTF16(IDS_LEARN_MORE));
    ash::Shell::GetInstance()->system_tray_notifier()->NotifySetNetworkMessage(
        listener, ash::NetworkObserver::MESSAGE_DATA_PROMO,
        type, string16(), message, links);
    check_for_promo_ = false;
    SetShow3gPromoNotification(false);
    if (carrier_deal_promo_pref != kNotificationCountPrefDefault)
      SetCarrierDealPromoShown(carrier_deal_promo_pref + 1);
  }
}

void DataPromoNotification::CloseNotification() {
  ash::Shell::GetInstance()->system_tray_notifier()->NotifyClearNetworkMessage(
      ash::NetworkObserver::MESSAGE_DATA_PROMO);
}

}  // namespace chromeos
