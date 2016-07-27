// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_PRINT_PREVIEW_STICKY_SETTINGS_H_
#define CHROME_BROWSER_UI_WEBUI_PRINT_PREVIEW_STICKY_SETTINGS_H_

#include <string>

#include "base/memory/scoped_ptr.h"
#include "printing/print_job_constants.h"

class PrefService;

namespace base {
class DictionaryValue;
class FilePath;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace printing {

// Holds all the settings that should be remembered (sticky) in print preview.
// A sticky setting will be restored next time the user launches print preview.
// Only one instance of this class is instantiated.
class StickySettings {
 public:
  StickySettings();
  ~StickySettings();

  base::FilePath* save_path();
  std::string* printer_app_state();

  // Stores app state for the last used printer.
  void StoreAppState(const std::string& app_state);
  // Stores the last path the user used to save to pdf.
  void StoreSavePath(const base::FilePath& path);

  void SaveInPrefs(PrefService* profile);
  void RestoreFromPrefs(PrefService* profile);
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

 private:
  scoped_ptr<base::FilePath> save_path_;
  scoped_ptr<std::string> printer_app_state_;
};

}  // namespace printing

#endif  // CHROME_BROWSER_UI_WEBUI_PRINT_PREVIEW_STICKY_SETTINGS_H_
