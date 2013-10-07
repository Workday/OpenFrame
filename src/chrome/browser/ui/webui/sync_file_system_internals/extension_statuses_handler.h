// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SYNC_FILE_SYSTEM_INTERNALS_EXTENSION_STATUSES_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SYNC_FILE_SYSTEM_INTERNALS_EXTENSION_STATUSES_HANDLER_H_

#include "base/compiler_specific.h"
#include "content/public/browser/web_ui_message_handler.h"

class Profile;

namespace syncfs_internals {

// This class handles message from WebUI page of chrome://syncfs-internals/
// for the Extension Statuses tab. It corresponds to browser/resources/
// sync_file_system_internals/extension_statuses.html. All methods in this class
// should be called on UI thread.
class ExtensionStatusesHandler : public content::WebUIMessageHandler {
 public:
  explicit ExtensionStatusesHandler(Profile* profile);
  virtual ~ExtensionStatusesHandler();

  // Shared by Extension Statuses Tab and also File Metadata Tab to generate the
  // extension drop down.
  static void GetExtensionStatusesAsDictionary(Profile* profile,
                                               base::ListValue* values);

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

 private:
  void GetExtensionStatuses(const base::ListValue* args);

  Profile* profile_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionStatusesHandler);
};

}  // namespace syncfs_internals

#endif  // CHROME_BROWSER_UI_WEBUI_SYNC_FILE_SYSTEM_INTERNALS_EXTENSION_STATUSES_HANDLER_H_
