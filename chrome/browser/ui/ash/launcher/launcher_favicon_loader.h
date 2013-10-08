// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_LAUNCHER_LAUNCHER_FAVICON_LOADER_H_
#define CHROME_BROWSER_UI_ASH_LAUNCHER_LAUNCHER_FAVICON_LOADER_H_

#include <vector>

#include "base/basictypes.h"
#include "base/callback.h"
#include "content/public/common/favicon_url.h"

class GURL;
class SkBitmap;

namespace internal {
class FaviconBitmapHandler;
}

namespace content {
class WebContents;
}

// LauncherFaviconLoader handles updates to the list of favicon urls and
// retrieves the appropriately sized favicon for panels in the Launcher.

class LauncherFaviconLoader {
 public:
  class Delegate {
   public:
    virtual void FaviconUpdated() = 0;

   protected:
    virtual ~Delegate() {}
  };

  LauncherFaviconLoader(Delegate* delegate,
                        content::WebContents* web_contents);
  virtual ~LauncherFaviconLoader();

  content::WebContents* web_contents() {
    return web_contents_;
  }

  // Returns an appropriately sized favicon for the Launcher. If none are
  // available will return an isNull bitmap.
  SkBitmap GetFavicon() const;

  // Returns true if the loader is waiting for downloads to finish.
  bool HasPendingDownloads() const;

 private:
  content::WebContents* web_contents_;
  scoped_ptr<internal::FaviconBitmapHandler> favicon_handler_;

  DISALLOW_COPY_AND_ASSIGN(LauncherFaviconLoader);
};

#endif  // CHROME_BROWSER_UI_ASH_LAUNCHER_LAUNCHER_FAVICON_LOADER_H_
