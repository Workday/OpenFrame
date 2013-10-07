// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {
  var OptionsPage = options.OptionsPage;

  /**
   * ResetProfileSettingsOverlay class
   * Encapsulated handling of the 'Reset Profile Settings' overlay page.
   * @class
   */
  function ResetProfileSettingsOverlay() {
    OptionsPage.call(
        this, 'resetProfileSettings',
        loadTimeData.getString('resetProfileSettingsOverlayTabTitle'),
        'reset-profile-settings-overlay');
  }

  cr.addSingletonGetter(ResetProfileSettingsOverlay);

  ResetProfileSettingsOverlay.prototype = {
    // Inherit ResetProfileSettingsOverlay from OptionsPage.
    __proto__: OptionsPage.prototype,

    /**
     * Initialize the page.
     */
    initializePage: function() {
      OptionsPage.prototype.initializePage.call(this);

      $('reset-profile-settings-dismiss').onclick = function(event) {
        ResetProfileSettingsOverlay.dismiss();
      };
      $('reset-profile-settings-commit').onclick = function(event) {
        ResetProfileSettingsOverlay.setResettingState(true);
        chrome.send('performResetProfileSettings',
                    [$('send-settings').checked]);
      };
    },

    /** @override */
    didShowPage: function() {
      chrome.send('onShowResetProfileDialog');
    },
  };

  /**
   * Enables/disables UI elements after/while Chrome is performing a reset.
   * @param {boolean} state If true, UI elements are disabled.
   */
  ResetProfileSettingsOverlay.setResettingState = function(state) {
    $('reset-profile-settings-throbber').style.visibility =
        state ? 'visible' : 'hidden';
    $('reset-profile-settings-dismiss').disabled = state;
    $('reset-profile-settings-commit').disabled = state;
  };

  /**
   * Chrome callback to notify ResetProfileSettingsOverlay that the reset
   * operation has terminated.
   */
  ResetProfileSettingsOverlay.doneResetting = function() {
    // The delay gives the user some feedback that the resetting
    // actually worked. Otherwise the dialog just vanishes instantly in most
    // cases.
    window.setTimeout(function() {
      ResetProfileSettingsOverlay.dismiss();
    }, 200);
  };

  /**
   * Dismisses the overlay.
   */
  ResetProfileSettingsOverlay.dismiss = function() {
    OptionsPage.closeOverlay();
    ResetProfileSettingsOverlay.setResettingState(false);
  };

  // Export
  return {
    ResetProfileSettingsOverlay: ResetProfileSettingsOverlay
  };
});
