// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path.h"

namespace safe_browsing {
namespace download_protection_util {

bool IsArchiveFile(const base::FilePath& file) {
  return file.MatchesExtension(FILE_PATH_LITERAL(".zip"));
}

bool IsBinaryFile(const base::FilePath& file) {
  return (
      // Executable extensions for MS Windows.
      file.MatchesExtension(FILE_PATH_LITERAL(".bas")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".bat")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".cab")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".cmd")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".com")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".exe")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".hta")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".msi")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".pif")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".reg")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".scr")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".vb")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".vbs")) ||
      // Chrome extensions and android APKs are also reported.
      file.MatchesExtension(FILE_PATH_LITERAL(".crx")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".apk")) ||
      // Archives _may_ contain binaries, we'll check in ExtractFileFeatures.
      IsArchiveFile(file));
}

}  // namespace download_protection_util
}  // namespace safe_browsing
