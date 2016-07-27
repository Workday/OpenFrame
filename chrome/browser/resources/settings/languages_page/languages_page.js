// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview 'settings-languages-page' is the settings page
 * for language and input method settings.
 *
 * @group Chrome Settings Elements
 * @element settings-languages-page
 */
(function() {
'use strict';

Polymer({
  is: 'settings-languages-page',

  properties: {
    /**
     * The current active route.
     */
    currentRoute: {
      type: Object,
      notify: true,
    },

    /**
     * Preferences state.
     */
    prefs: {
      type: Object,
      notify: true,
    },

    /**
     * Read-only reference to the languages model provided by the
     * 'settings-languages' instance.
     * @type {LanguagesModel|undefined}
     */
    languages: {
      type: Object,
      notify: true,
    },
  },

  /**
   * Handler for clicking a language on the main page, which selects the
   * language as the prospective UI language on Chrome OS and Windows.
   * @param {!{model: !{item: !LanguageInfo}}} e
   */
  onLanguageTap_: function(e) {
    // Taps on the paper-icon-button are handled in onShowLanguageDetailTap_.
    if (e.target.tagName == 'PAPER-ICON-BUTTON')
      return;

    // Set the prospective UI language. This won't take effect until a restart.
    if (e.model.item.language.supportsUI)
      this.$.languages.setUILanguage(e.model.item.language.code);
  },

  /**
   * Handler for enabling or disabling spell check.
   * @param {!{target: Element, model: !{item: !LanguageInfo}}} e
   */
  onSpellCheckChange_: function(e) {
    this.$.languages.toggleSpellCheck(e.model.item.language.code,
                                      e.target.checked);
  },

  /** @private */
  onBackTap_: function() {
    this.$.pages.back();
  },

  /**
   * Opens the Manage Languages page.
   * @private
   */
  onManageLanguagesTap_: function() {
    this.$.pages.setSubpageChain(['manage-languages']);
    this.forceRenderList_('settings-manage-languages-page');
  },

  /**
   * Opens the Language Detail page for the language.
   * @param {!{model: !{item}}} e
   * @private
   */
  onShowLanguageDetailTap_: function(e) {
    this.$.languageSelector.select(e.model.item);
    this.$.pages.setSubpageChain(['language-detail']);
  },

<if expr="not is_macosx">
  /**
   * Opens the Custom Dictionary page.
   * @private
   */
  onEditDictionaryTap_: function() {
    this.$.pages.setSubpageChain(['edit-dictionary']);
    this.forceRenderList_('settings-edit-dictionary-page');
  },
</if>

  /**
   * @param {string} languageCode The language code identifying a language.
   * @param {string} prospectiveUILanguage The prospective UI language.
   * @return {boolean} True if the given language matches the prospective UI
   *     pref (which may be different from the actual UI language).
   * @private
   */
  isUILanguage_: function(languageCode, prospectiveUILanguage) {
    return languageCode == this.$.languages.getProspectiveUILanguage();
  },

  /**
   * @param {string} id The input method ID.
   * @param {string} currentId The ID of the currently enabled input method.
   * @return {boolean} True if the IDs match.
   * @private
   */
  isCurrentInputMethod_: function(id, currentId) {
    assert(cr.isChromeOS);
    return id == currentId;
  },

  /**
   * HACK(michaelpg): This is necessary to show the list when navigating to
   * the sub-page. Remove this function when PolymerElements/neon-animation#60
   * is fixed.
   * @param {string} tagName Name of the element containing the <iron-list>.
   */
  forceRenderList_: function(tagName) {
    this.$$(tagName).$$('iron-list').fire('iron-resize');
  },
});
})();
