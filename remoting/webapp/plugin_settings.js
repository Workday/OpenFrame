// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains all the settings that may need massaging by the build script.
// Keeping all that centralized here allows us to use symlinks for the other
// files making for a faster compile/run cycle when only modifying HTML/JS.

'use strict';

/** @suppress {duplicate} */
var remoting = remoting || {};

/** @type {remoting.Settings} */
remoting.settings = null;
/** @constructor */
remoting.Settings = function() {};

// The settings on this file are automatically substituted by build-webapp.py.
// Do not override them manually, except for running local tests.

/** @type {string} MIME type for the host plugin.*/
remoting.Settings.prototype.PLUGIN_MIMETYPE = 'HOST_PLUGIN_MIMETYPE';
/** @type {string} API client ID.*/
remoting.Settings.prototype.OAUTH2_CLIENT_ID = 'API_CLIENT_ID';
/** @type {string} API client secret.*/
remoting.Settings.prototype.OAUTH2_CLIENT_SECRET = 'API_CLIENT_SECRET';

/** @type {string} Base URL for OAuth2 authentication. */
remoting.Settings.prototype.OAUTH2_BASE_URL = 'OAUTH2_BASE_URL';
/** @type {string} Base URL for the OAuth2 API. */
remoting.Settings.prototype.OAUTH2_API_BASE_URL = 'OAUTH2_API_BASE_URL';
/** @type {string} Base URL for the Remoting Directory REST API. */
remoting.Settings.prototype.DIRECTORY_API_BASE_URL = 'DIRECTORY_API_BASE_URL';
/** @type {string} URL for the talk gadget web service. */
remoting.Settings.prototype.TALK_GADGET_URL = 'TALK_GADGET_URL';
/** @type {string} OAuth2 redirect URI. */
remoting.Settings.prototype.OAUTH2_REDIRECT_URL = 'OAUTH2_REDIRECT_URL';

/** @type {string} XMPP JID for the remoting directory server bot. */
remoting.Settings.prototype.DIRECTORY_BOT_JID = 'DIRECTORY_BOT_JID';

// XMPP server connection settings.
/** @type {string} XMPP server host name (or IP address) and port. */
remoting.Settings.prototype.XMPP_SERVER_ADDRESS = 'XMPP_SERVER_ADDRESS';
/** @type {boolean} Whether to use TLS on connections to the XMPP server. */
remoting.Settings.prototype.XMPP_SERVER_USE_TLS =
    Boolean('XMPP_SERVER_USE_TLS');

// Third party authentication settings.
/** @type {string} The third party auth redirect URI. */
remoting.Settings.prototype.THIRD_PARTY_AUTH_REDIRECT_URI =
    'THIRD_PARTY_AUTH_REDIRECT_URL';
