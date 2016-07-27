// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('settings_reset_page', function() {
  /** @enum {string} */
  var TestNames = {
    PowerwashDialogAction: 'PowerwashDialogAction',
    PowerwashDialogOpenClose: 'PowerwashDialogOpenClose',
    ResetProfileDialogAction: 'ResetProfileDialogAction',
    ResetProfileDialogOpenClose: 'ResetProfileDialogOpenClose',
  };


  /**
   * @param {string} name chrome.send message name.
   * @return {!Promise} Fires when chrome.send is called with the given message
   *     name.
   */
  function whenChromeSendCalled(name) {
    return new Promise(function(resolve, reject) {
      registerMessageCallback(name, null, resolve);
    });
  }

  function registerTests() {
    suite('DialogTests', function() {
      var resetPage = null;

      suiteSetup(function() {
        return Promise.all([
          PolymerTest.importHtml('chrome://md-settings/i18n_setup.html'),
          PolymerTest.importHtml(
              'chrome://md-settings/reset_page/reset_page.html')
        ]);
      });

      setup(function() {
        PolymerTest.clearBody();
        resetPage = document.createElement('settings-reset-page');
        document.body.appendChild(resetPage);
      });

      // Tests that the reset profile dialog opens and closes correctly, and
      // that chrome.send calls are propagated as expected.
      test(TestNames.ResetProfileDialogOpenClose, function() {
        var onShowResetProfileDialogCalled = whenChromeSendCalled(
            'onShowResetProfileDialog');
        var onHideResetProfileDialogCalled = whenChromeSendCalled(
            'onHideResetProfileDialog');

        // Open reset profile dialog.
        MockInteractions.tap(resetPage.$.resetProfile);
        var dialog = resetPage.$$('settings-reset-profile-dialog');
        var onDialogClosed = new Promise(
            function(resolve, reject) {
              dialog.addEventListener('iron-overlay-closed', resolve);
            });

        MockInteractions.tap(dialog.$.cancel);

        return Promise.all([
          onShowResetProfileDialogCalled,
          onHideResetProfileDialogCalled,
          onDialogClosed
        ]);
      });

      // Tests that when resetting the profile is requested chrome.send calls
      // are propagated as expected.
      test(TestNames.ResetProfileDialogAction, function() {
        // Open reset profile dialog.
        MockInteractions.tap(resetPage.$.resetProfile);
        var dialog = resetPage.$$('settings-reset-profile-dialog');
        var promise = whenChromeSendCalled('performResetProfileSettings');
        MockInteractions.tap(dialog.$.reset);
        return promise;
      });

      if (cr.isChromeOS) {
        // Tests that the powerwash dialog opens and closes correctly, and
        // that chrome.send calls are propagated as expected.
        test(TestNames.PowerwashDialogOpenClose, function() {
          var onPowerwashDialogShowCalled = whenChromeSendCalled(
              'onPowerwashDialogShow');

          // Open powerwash dialog.
          MockInteractions.tap(resetPage.$.powerwash);
          var dialog = resetPage.$$('settings-powerwash-dialog');
          var onDialogClosed = new Promise(
              function(resolve, reject) {
                dialog.addEventListener('iron-overlay-closed', resolve);
              });

          MockInteractions.tap(dialog.$.cancel);
          return Promise.all([onPowerwashDialogShowCalled, onDialogClosed]);
        });

        // Tests that when powerwash is requested chrome.send calls are
        // propagated as expected.
        test(TestNames.PowerwashDialogAction, function() {
          // Open powerwash dialog.
          MockInteractions.tap(resetPage.$.powerwash);
          var dialog = resetPage.$$('settings-powerwash-dialog');
          var promise = whenChromeSendCalled('requestFactoryResetRestart');
          MockInteractions.tap(dialog.$.powerwash);
          return promise;
        });
      }
    });
  }

  return {
    registerTests: registerTests,
  };
});
