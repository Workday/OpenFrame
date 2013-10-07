// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_USER_DATA_DIR_DIALOG_H_
#define CHROME_BROWSER_UI_USER_DATA_DIR_DIALOG_H_

namespace base {
class FilePath;
}

namespace chrome {

// Shows a user data directory picker dialog. The method blocks while the dialog
// is showing. If the user picks a directory, this method returns the chosen
// directory. |user_data_dir| is the value of the directory we were not able to
// use.
base::FilePath ShowUserDataDirDialog(const base::FilePath& user_data_dir);

}  // namespace chrome

#endif  // CHROME_BROWSER_UI_USER_DATA_DIR_DIALOG_H_
