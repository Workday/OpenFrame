// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TRANSLATE_TRANSLATE_PREFS_H_
#define CHROME_BROWSER_TRANSLATE_TRANSLATE_PREFS_H_

#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "url/gurl.h"

class PrefService;
class Profile;

namespace base {
class DictionaryValue;
class ListValue;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

class TranslatePrefs {
 public:
  static const char kPrefTranslateLanguageBlacklist[];
  static const char kPrefTranslateSiteBlacklist[];
  static const char kPrefTranslateWhitelists[];
  static const char kPrefTranslateDeniedCount[];
  static const char kPrefTranslateAcceptedCount[];
  static const char kPrefTranslateBlockedLanguages[];

  explicit TranslatePrefs(PrefService* user_prefs);

  bool IsBlockedLanguage(const std::string& original_language) const;
  void BlockLanguage(const std::string& original_language);
  void UnblockLanguage(const std::string& original_language);

  // Removes a language from the old blacklist. This method is for
  // chrome://translate-internals/.  Don't use this if there is no special
  // reason.
  void RemoveLanguageFromLegacyBlacklist(const std::string& original_language);

  bool IsSiteBlacklisted(const std::string& site) const;
  void BlacklistSite(const std::string& site);
  void RemoveSiteFromBlacklist(const std::string& site);

  bool HasWhitelistedLanguagePairs() const;
  void ClearWhitelistedLanguagePairs();

  bool IsLanguagePairWhitelisted(const std::string& original_language,
      const std::string& target_language);
  void WhitelistLanguagePair(const std::string& original_language,
      const std::string& target_language);
  void RemoveLanguagePairFromWhitelist(const std::string& original_language,
      const std::string& target_language);

  // Will return true if at least one language has been blacklisted.
  bool HasBlacklistedLanguages() const;
  void ClearBlacklistedLanguages();

  // Will return true if at least one site has been blacklisted.
  bool HasBlacklistedSites() const;
  void ClearBlacklistedSites();

  // These methods are used to track how many times the user has denied the
  // translation for a specific language. (So we can present a UI to black-list
  // that language if the user keeps denying translations).
  int GetTranslationDeniedCount(const std::string& language) const;
  void IncrementTranslationDeniedCount(const std::string& language);
  void ResetTranslationDeniedCount(const std::string& language);

  // These methods are used to track how many times the user has accepted the
  // translation for a specific language. (So we can present a UI to white-list
  // that language if the user keeps accepting translations).
  int GetTranslationAcceptedCount(const std::string& language);
  void IncrementTranslationAcceptedCount(const std::string& language);
  void ResetTranslationAcceptedCount(const std::string& language);

  static bool CanTranslateLanguage(
      Profile* profile, const std::string& language);
  static bool ShouldAutoTranslate(PrefService* user_prefs,
      const std::string& original_language, std::string* target_language);
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);
  static void MigrateUserPrefs(PrefService* user_prefs);

 private:
  friend class TranslatePrefsTest;
  FRIEND_TEST_ALL_PREFIXES(TranslatePrefsTest, CreateBlockedLanguages);

  // Merges two language sets to migrate to the language setting UI.
  static void CreateBlockedLanguages(
      std::vector<std::string>* blocked_languages,
      const std::vector<std::string>& blacklisted_languages,
      const std::vector<std::string>& accept_languages);

  bool IsValueBlacklisted(const char* pref_id, const std::string& value) const;
  void BlacklistValue(const char* pref_id, const std::string& value);
  void RemoveValueFromBlacklist(const char* pref_id, const std::string& value);
  bool IsValueInList(
      const base::ListValue* list, const std::string& value) const;
  bool IsLanguageWhitelisted(const std::string& original_language,
      std::string* target_language) const;
  bool IsListEmpty(const char* pref_id) const;
  bool IsDictionaryEmpty(const char* pref_id) const;

  // Retrieves the dictionary mapping the number of times translation has been
  // denied for a language, creating it if necessary.
  base::DictionaryValue* GetTranslationDeniedCountDictionary();

  // Retrieves the dictionary mapping the number of times translation has been
  // accepted for a language, creating it if necessary.
  base::DictionaryValue* GetTranslationAcceptedCountDictionary() const;

  PrefService* prefs_;  // Weak.
};

#endif  // CHROME_BROWSER_TRANSLATE_TRANSLATE_PREFS_H_
