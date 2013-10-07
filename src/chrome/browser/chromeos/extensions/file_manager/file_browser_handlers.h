// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file provides utility functions for file browser handlers.
// https://developer.chrome.com/extensions/fileBrowserHandler.html

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_FILE_BROWSER_HANDLERS_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_FILE_BROWSER_HANDLERS_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/callback_forward.h"
#include "chrome/browser/chromeos/extensions/file_manager/file_tasks.h"

class FileBrowserHandler;
class GURL;
class Profile;

namespace base {
class FilePath;
}

namespace extensions {
class Extension;
}

namespace fileapi {
class FileSystemURL;
}

namespace file_manager {
namespace file_browser_handlers {

// Tasks are stored as a vector in order of priorities.
typedef std::vector<const FileBrowserHandler*> FileBrowserHandlerList;

// Returns true if the given file browser handler should be used as a
// fallback. Such handlers are Files.app's internal handlers as well as quick
// office extensions.
bool IsFallbackFileBrowserHandler(const FileBrowserHandler* handler);

// Finds file browser handlers set as default from |common_tasks| for
// |file_list|. If no handlers are set as default, choose the the firstly
// found fallback handler as default.
FileBrowserHandlerList FindDefaultFileBrowserHandlers(
    Profile* profile,
    const std::vector<base::FilePath>& file_list,
    const FileBrowserHandlerList& common_tasks);

// Returns the list of file browser handlers that can open all files in
// |file_list|.
FileBrowserHandlerList FindCommonFileBrowserHandlers(
    Profile* profile,
    const std::vector<GURL>& file_list);

// Finds a file browser handler for a file whose URL is |url| and whose path
// is |path|. Returns the default handler if one is defined (The default
// handler is the one that is assigned to the file manager task button by
// default). If the default handler is not found, tries to match the url with
// one of the file browser handlers.
const FileBrowserHandler* FindFileBrowserHandlerForURLAndPath(
    Profile* profile,
    const GURL& url,
    const base::FilePath& path);

// Executes a file browser handler specified by |extension| of the given
// action ID for |file_urls|. Returns false if undeclared handlers are
// found. |done| is on completion. See also the comment at ExecuteFileTask()
// for other parameters.
bool ExecuteFileBrowserHandler(
    Profile* profile,
    const extensions::Extension* extension,
    int32 tab_id,
    const std::string& action_id,
    const std::vector<fileapi::FileSystemURL>& file_urls,
    const file_tasks::FileTaskFinishedCallback& done);

}  // namespace file_browser_handlers
}  // namespace file_manager

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_FILE_BROWSER_HANDLERS_H_
