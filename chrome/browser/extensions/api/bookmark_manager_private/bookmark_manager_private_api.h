// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_BOOKMARK_MANAGER_PRIVATE_BOOKMARK_MANAGER_PRIVATE_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_BOOKMARK_MANAGER_PRIVATE_BOOKMARK_MANAGER_PRIVATE_API_H_

#include "base/values.h"
#include "chrome/browser/bookmarks/bookmark_node_data.h"
#include "chrome/browser/extensions/api/bookmarks/bookmarks_api.h"
#include "chrome/browser/extensions/extension_function.h"
#include "chrome/browser/ui/bookmarks/bookmark_tab_helper.h"

struct BookmarkNodeData;
class Profile;

namespace content {
class WebContents;
}

namespace extensions {

// Class that handles the chrome.bookmarkManagerPrivate events.
class BookmarkManagerPrivateEventRouter
    : public BookmarkTabHelper::BookmarkDrag {
 public:
  BookmarkManagerPrivateEventRouter(Profile* profile,
                                    content::WebContents* web_contents);
  virtual ~BookmarkManagerPrivateEventRouter();

  // BookmarkTabHelper::BookmarkDrag interface
  virtual void OnDragEnter(const BookmarkNodeData& data) OVERRIDE;
  virtual void OnDragOver(const BookmarkNodeData& data) OVERRIDE;
  virtual void OnDragLeave(const BookmarkNodeData& data) OVERRIDE;
  virtual void OnDrop(const BookmarkNodeData& data) OVERRIDE;

  // The bookmark drag and drop data. This gets set after a drop was done on
  // the page. This returns NULL if no data is available.
  const BookmarkNodeData* GetBookmarkNodeData();

  // Clears the drag and drop data.
  void ClearBookmarkNodeData();

 private:
  // Helper to actually dispatch an event to extension listeners.
  void DispatchEvent(const char* event_name,
                     scoped_ptr<base::ListValue> args);

  void DispatchDragEvent(const BookmarkNodeData& data, const char* event_name);

  Profile* profile_;
  content::WebContents* web_contents_;
  BookmarkNodeData bookmark_drag_data_;

  DISALLOW_COPY_AND_ASSIGN(BookmarkManagerPrivateEventRouter);
};

class ClipboardBookmarkManagerFunction : public extensions::BookmarksFunction {
 protected:
  virtual ~ClipboardBookmarkManagerFunction() {}

  bool CopyOrCut(bool cut, const std::vector<std::string>& id_list);
};

class BookmarkManagerPrivateCopyFunction
    : public ClipboardBookmarkManagerFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bookmarkManagerPrivate.copy",
                             BOOKMARKMANAGERPRIVATE_COPY)

 protected:
  virtual ~BookmarkManagerPrivateCopyFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class BookmarkManagerPrivateCutFunction
    : public ClipboardBookmarkManagerFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bookmarkManagerPrivate.cut",
                             BOOKMARKMANAGERPRIVATE_CUT)

 protected:
  virtual ~BookmarkManagerPrivateCutFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class BookmarkManagerPrivatePasteFunction
    : public extensions::BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bookmarkManagerPrivate.paste",
                             BOOKMARKMANAGERPRIVATE_PASTE)

 protected:
  virtual ~BookmarkManagerPrivatePasteFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class BookmarkManagerPrivateCanPasteFunction
    : public extensions::BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bookmarkManagerPrivate.canPaste",
                             BOOKMARKMANAGERPRIVATE_CANPASTE)

 protected:
  virtual ~BookmarkManagerPrivateCanPasteFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class BookmarkManagerPrivateSortChildrenFunction
    : public extensions::BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bookmarkManagerPrivate.sortChildren",
                             BOOKMARKMANAGERPRIVATE_SORTCHILDREN)

 protected:
  virtual ~BookmarkManagerPrivateSortChildrenFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class BookmarkManagerPrivateGetStringsFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bookmarkManagerPrivate.getStrings",
                             BOOKMARKMANAGERPRIVATE_GETSTRINGS)

 protected:
  virtual ~BookmarkManagerPrivateGetStringsFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class BookmarkManagerPrivateStartDragFunction
    : public extensions::BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bookmarkManagerPrivate.startDrag",
                             BOOKMARKMANAGERPRIVATE_STARTDRAG)

 protected:
  virtual ~BookmarkManagerPrivateStartDragFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class BookmarkManagerPrivateDropFunction
    : public extensions::BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bookmarkManagerPrivate.drop",
                             BOOKMARKMANAGERPRIVATE_DROP)

 protected:
  virtual ~BookmarkManagerPrivateDropFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class BookmarkManagerPrivateGetSubtreeFunction
    : public extensions::BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bookmarkManagerPrivate.getSubtree",
                             BOOKMARKMANAGERPRIVATE_GETSUBTREE)

 protected:
  virtual ~BookmarkManagerPrivateGetSubtreeFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class BookmarkManagerPrivateCanEditFunction
    : public extensions::BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bookmarkManagerPrivate.canEdit",
                             BOOKMARKMANAGERPRIVATE_CANEDIT)

 protected:
  virtual ~BookmarkManagerPrivateCanEditFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class BookmarkManagerPrivateRecordLaunchFunction
    : public extensions::BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bookmarkManagerPrivate.recordLaunch",
                             BOOKMARKMANAGERPRIVATE_RECORDLAUNCH)

 protected:
  virtual ~BookmarkManagerPrivateRecordLaunchFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class BookmarkManagerPrivateCanOpenNewWindowsFunction
    : public extensions::BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bookmarkManagerPrivate.canOpenNewWindows",
                             BOOKMARKMANAGERPRIVATE_CANOPENNEWWINDOWS)

 protected:
  virtual ~BookmarkManagerPrivateCanOpenNewWindowsFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class BookmarkManagerPrivateRemoveTreesFunction
    : public extensions::BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("bookmarkManagerPrivate.removeTrees",
                             BOOKMARKMANAGERPRIVATE_REMOVETREES)

 protected:
  virtual ~BookmarkManagerPrivateRemoveTreesFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_BOOKMARK_MANAGER_PRIVATE_BOOKMARK_MANAGER_PRIVATE_API_H_
