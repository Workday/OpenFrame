// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TRANSLATE_TRANSLATE_INFOBAR_DELEGATE_H_
#define CHROME_BROWSER_TRANSLATE_TRANSLATE_INFOBAR_DELEGATE_H_

#include <string>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "chrome/browser/infobars/infobar_delegate.h"
#include "chrome/browser/translate/translate_prefs.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/translate/translate_errors.h"

class PrefService;

// The defaults after which extra shortcuts for options
// can be shown.
struct ShortcutConfiguration {
  int always_translate_min_count;
  int never_translate_min_count;
};

class TranslateInfoBarDelegate : public InfoBarDelegate {
 public:
  // The different types of infobars that can be shown for translation.
  enum Type {
    BEFORE_TRANSLATE,
    TRANSLATING,
    AFTER_TRANSLATE,
    TRANSLATION_ERROR
  };

  // The types of background color animations.
  enum BackgroundAnimationType {
    NONE,
    NORMAL_TO_ERROR,
    ERROR_TO_NORMAL
  };

  static const size_t kNoIndex;

  virtual ~TranslateInfoBarDelegate();

  // Factory method to create a translate infobar.  |error_type| must be
  // specified iff |infobar_type| == TRANSLATION_ERROR.  For other infobar
  // types, |original_language| and |target_language| must be ASCII language
  // codes (e.g. "en", "fr", etc.) for languages the TranslateManager supports
  // translating.  The lone exception is when the user initiates translation
  // from the context menu, in which case it's legal to call this with
  // |infobar_type| == TRANSLATING and
  // |original_language| == kUnknownLanguageCode.
  //
  // If |replace_existing_infobar| is true, the infobar is created and added to
  // |infobar_service|, replacing any other translate infobar already present
  // there.  Otherwise, the infobar will only be added if there is no other
  // translate infobar already present.
  static void Create(bool replace_existing_infobar,
                     InfoBarService* infobar_service,
                     Type infobar_type,
                     const std::string& original_language,
                     const std::string& target_language,
                     TranslateErrors::Type error_type,
                     PrefService* prefs,
                     const ShortcutConfiguration& shortcut_config);

  // Returns the number of languages supported.
  size_t num_languages() const { return languages_.size(); }

  // Returns the ISO code for the language at |index|.
  std::string language_code_at(size_t index) const {
    DCHECK_LT(index, num_languages());
    return languages_[index].first;
  }

  // Returns the displayable name for the language at |index|.
  string16 language_name_at(size_t index) const {
    if (index == kNoIndex)
      return string16();
    DCHECK_LT(index, num_languages());
    return languages_[index].second;
  }

  Type infobar_type() const { return infobar_type_; }

  TranslateErrors::Type error_type() const { return error_type_; }

  size_t original_language_index() const { return original_language_index_; }
  void set_original_language_index(size_t language_index) {
    DCHECK_LT(language_index, num_languages());
    original_language_index_ = language_index;
  }
  size_t target_language_index() const { return target_language_index_; }
  void set_target_language_index(size_t language_index) {
    DCHECK_LT(language_index, num_languages());
    target_language_index_ = language_index;
  }

  // Convenience methods.
  std::string original_language_code() const {
    return (original_language_index() == kNoIndex) ?
        chrome::kUnknownLanguageCode :
        language_code_at(original_language_index());
  }
  std::string target_language_code() const {
    return language_code_at(target_language_index());
  }

  // Returns true if the current infobar indicates an error (in which case it
  // should get a yellow background instead of a blue one).
  bool is_error() const { return infobar_type_ == TRANSLATION_ERROR; }

  // Returns what kind of background fading effect the infobar should use when
  // its is shown.
  BackgroundAnimationType background_animation_type() const {
    return background_animation_;
  }

  virtual void Translate();
  virtual void RevertTranslation();
  void ReportLanguageDetectionError();

  // Called when the user declines to translate a page, by either closing the
  // infobar or pressing the "Don't translate" button.
  virtual void TranslationDeclined();

  // Methods called by the Options menu delegate.
  virtual bool IsTranslatableLanguageByPrefs();
  virtual void ToggleTranslatableLanguageByPrefs();
  virtual bool IsSiteBlacklisted();
  virtual void ToggleSiteBlacklist();
  virtual bool ShouldAlwaysTranslate();
  virtual void ToggleAlwaysTranslate();

  // Methods called by the extra-buttons that can appear on the "before
  // translate" infobar (when the user has accepted/declined the translation
  // several times).
  void AlwaysTranslatePageLanguage();
  void NeverTranslatePageLanguage();

  // The following methods are called by the infobar that displays the status
  // while translating and also the one displaying the error message.
  string16 GetMessageInfoBarText();
  string16 GetMessageInfoBarButtonText();
  void MessageInfoBarButtonPressed();
  bool ShouldShowMessageInfoBarButton();

  // Called by the before translate infobar to figure-out if it should show
  // an extra shortcut to let the user black-list/white-list that language
  // (based on how many times the user accepted/declined translation).
  // The shortcut itself is platform specific, it can be a button or a new bar
  // for example.
  bool ShouldShowNeverTranslateShortcut();
  bool ShouldShowAlwaysTranslateShortcut();

  // Convenience method that returns the displayable language name for
  // |language_code| in the current application locale.
  static string16 GetLanguageDisplayableName(const std::string& language_code);

  // Adds the strings that should be displayed in the after translate infobar to
  // |strings|. If |autodetermined_source_language| is false, the text in that
  // infobar is:
  // "The page has been translated from <lang1> to <lang2>."
  // Otherwise:
  // "The page has been translated to <lang1>."
  // Because <lang1>, or <lang1> and <lang2> are displayed in menu buttons, the
  // text is split in 2 or 3 chunks. |swap_languages| is set to true if
  // |autodetermined_source_language| is false, and <lang1> and <lang2>
  // should be inverted (some languages express the sentense as "The page has
  // been translate to <lang2> from <lang1>."). It is ignored if
  // |autodetermined_source_language| is true.
  static void GetAfterTranslateStrings(std::vector<string16>* strings,
                                       bool* swap_languages,
                                       bool autodetermined_source_language);

 protected:
  TranslateInfoBarDelegate(InfoBarService* infobar_service,
                           Type infobar_type,
                           TranslateInfoBarDelegate* old_delegate,
                           const std::string& original_language,
                           const std::string& target_language,
                           TranslateErrors::Type error_type,
                           PrefService* prefs,
                           ShortcutConfiguration shortcut_config);

 private:
  typedef std::pair<std::string, string16> LanguageNamePair;

  // InfoBarDelegate:
  virtual InfoBar* CreateInfoBar(InfoBarService* infobar_service) OVERRIDE;
  virtual void InfoBarDismissed() OVERRIDE;
  virtual int GetIconID() const OVERRIDE;
  virtual InfoBarDelegate::Type GetInfoBarType() const OVERRIDE;
  virtual bool ShouldExpire(
       const content::LoadCommittedDetails& details) const OVERRIDE;
  virtual TranslateInfoBarDelegate* AsTranslateInfoBarDelegate() OVERRIDE;

  // Gets the host of the page being translated, or an empty string if no URL is
  // associated with the current page.
  std::string GetPageHost();

  Type infobar_type_;

  // The type of fading animation if any that should be used when showing this
  // infobar.
  BackgroundAnimationType background_animation_;

  // The list supported languages for translation.
  // The pair first string is the language ISO code (ex: en, fr...), the second
  // string is the displayable name on the current locale.
  // The languages are sorted alphabetically based on the displayable name.
  std::vector<LanguageNamePair> languages_;

  // The index for language the page is originally in.
  size_t original_language_index_;

  // The index for language the page is originally in that was originally
  // reported (original_language_index_ changes if the user selects a new
  // original language, but this one does not).  This is necessary to report
  // language detection errors with the right original language even if the user
  // changed the original language.
  size_t initial_original_language_index_;

  // The index for language the page should be translated to.
  size_t target_language_index_;

  // The error that occurred when trying to translate (NONE if no error).
  TranslateErrors::Type error_type_;

  // The translation related preferences.
  TranslatePrefs prefs_;

  // Translation shortcut configuration
  ShortcutConfiguration shortcut_config_;
  DISALLOW_COPY_AND_ASSIGN(TranslateInfoBarDelegate);
};

#endif  // CHROME_BROWSER_TRANSLATE_TRANSLATE_INFOBAR_DELEGATE_H_
