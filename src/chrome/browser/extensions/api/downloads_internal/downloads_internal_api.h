// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_DOWNLOADS_INTERNAL_DOWNLOADS_INTERNAL_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_DOWNLOADS_INTERNAL_DOWNLOADS_INTERNAL_API_H_

#include "chrome/browser/extensions/extension_function.h"

class DownloadsInternalDetermineFilenameFunction
    : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloadsInternal.determineFilename",
                             DOWNLOADSINTERNAL_DETERMINEFILENAME);
  DownloadsInternalDetermineFilenameFunction();
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~DownloadsInternalDetermineFilenameFunction();

 private:
  DISALLOW_COPY_AND_ASSIGN(DownloadsInternalDetermineFilenameFunction);
};

#endif  // CHROME_BROWSER_EXTENSIONS_API_DOWNLOADS_INTERNAL_DOWNLOADS_INTERNAL_API_H_
