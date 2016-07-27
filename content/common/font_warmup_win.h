// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_FONT_WARMUP_WIN_H_
#define CONTENT_COMMON_FONT_WARMUP_WIN_H_

#include "base/files/file_path.h"
#include "content/common/content_export.h"

class SkFontMgr;
class SkTypeface;

namespace content {

// Make necessary calls to cache the data for a given font, used before
// sandbox lockdown.
CONTENT_EXPORT void DoPreSandboxWarmupForTypeface(SkTypeface* typeface);

// Get the shared font manager used during pre-sandbox warmup for DirectWrite
// fonts.
CONTENT_EXPORT SkFontMgr* GetPreSandboxWarmupFontMgr();

class GdiFontPatchData {
 public:
  virtual ~GdiFontPatchData() {}

 protected:
  GdiFontPatchData() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(GdiFontPatchData);
};

// Hook a module's imported GDI font functions to reimplement font enumeration
// and font data retrieval for DLLs which can't be easily modified.
CONTENT_EXPORT GdiFontPatchData* PatchGdiFontEnumeration(
    const base::FilePath& path);

// Testing method to get the number of in-flight emulated GDI handles.
CONTENT_EXPORT size_t GetEmulatedGdiHandleCountForTesting();

// Testing method to reset the table of emulated GDI handles.
CONTENT_EXPORT void ResetEmulatedGdiHandlesForTesting();

// Sets the pre-sandbox warmup font manager directly. This should only be used
// for testing the implementation.
CONTENT_EXPORT void SetPreSandboxWarmupFontMgrForTesting(SkFontMgr* fontmgr);

// Warmup the direct write font manager for content processes.
CONTENT_EXPORT void WarmupDirectWrite();

}  // namespace content

#endif  // CONTENT_COMMON_FONT_WARMUP_WIN_H_
