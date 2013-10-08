// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEB_RESOURCE_NOTIFICATION_PROMO_H_
#define CHROME_BROWSER_WEB_RESOURCE_NOTIFICATION_PROMO_H_

#include <string>

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "url/gurl.h"

class PrefRegistrySimple;
class PrefService;

namespace base {
class DictionaryValue;
class ListValue;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

// Helper class for PromoResourceService that parses promo notification info
// from json or prefs.
class NotificationPromo {
 public:
  static GURL PromoServerURL();

  enum PromoType {
    NO_PROMO,
    NTP_NOTIFICATION_PROMO,
    NTP_BUBBLE_PROMO,
    MOBILE_NTP_SYNC_PROMO,
  };

  NotificationPromo();
  ~NotificationPromo();

  // Initialize from json/prefs.
  void InitFromJson(const base::DictionaryValue& json, PromoType promo_type);
  void InitFromPrefs(PromoType promo_type);

  // Can this promo be shown?
  bool CanShow() const;

  // Calculates promo notification start time with group-based time slice
  // offset.
  double StartTimeForGroup() const;
  double EndTime() const;

  // Helpers for NewTabPageHandler.
  // Mark the promo as closed when the user dismisses it.
  static void HandleClosed(PromoType promo_type);
  // Mark the promo has having been viewed. This returns true if views
  // exceeds the maximum allowed.
  static bool HandleViewed(PromoType promo_type);

  bool new_notification() const { return new_notification_; }

  const std::string& promo_text() const { return promo_text_; }
  PromoType promo_type() const { return promo_type_; }
  const base::DictionaryValue* promo_payload() const {
    return promo_payload_.get();
  }

  // Register preferences.
  static void RegisterPrefs(PrefRegistrySimple* registry);
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);
  static void MigrateUserPrefs(PrefService* user_prefs);

 private:
  // For testing.
  friend class NotificationPromoTest;

  // Check if this promo notification is new based on start/end times,
  // and trigger events accordingly.
  void CheckForNewNotification();

  // Actions on receiving a new promo notification.
  void OnNewNotification();

  // Flush data members to prefs for storage.
  void WritePrefs();

  // Tests group_ against max_group_.
  // When max_group_ is 0, all groups pass.
  bool ExceedsMaxGroup() const;

  // Tests views_ against max_views_.
  // When max_views_ is 0, we don't cap the number of views.
  bool ExceedsMaxViews() const;

  // Returns false if this promo should not be displayed because it is a promo
  // for the app launcher, and the user has already enabled the app launcher.
  bool CheckAppLauncher() const;

  PrefService* prefs_;

  PromoType promo_type_;
  std::string promo_text_;

  scoped_ptr<const base::DictionaryValue> promo_payload_;

  double start_;
  double end_;

  int num_groups_;
  int initial_segment_;
  int increment_;
  int time_slice_;
  int max_group_;

  // When max_views_ is 0, we don't cap the number of views.
  int max_views_;

  int group_;
  int views_;
  bool closed_;

  bool new_notification_;

  DISALLOW_COPY_AND_ASSIGN(NotificationPromo);
};

#endif  // CHROME_BROWSER_WEB_RESOURCE_NOTIFICATION_PROMO_H_
