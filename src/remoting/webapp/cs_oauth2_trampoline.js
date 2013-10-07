// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// For open-source builds, the redirect URL ends in /dev. For official builds,
// it ends in /rel/{extension-id}. This distinction is handled by the manifest;
// all we have to do here is check the extension id in the case that there is
// one, to allow the official beta and dev channels to co-exist.

var extensionId = chrome.i18n.getMessage('@@extension_id');
var officialPath = '/talkgadget/oauth/chrome-remote-desktop/rel/' + extensionId;
var unofficialPath = '/talkgadget/oauth/chrome-remote-desktop/dev';

if (window.location.pathname == officialPath ||
    window.location.pathname == unofficialPath) {
  window.location.replace(
      chrome.extension.getURL('oauth2_callback.html') + window.location.search);
}
