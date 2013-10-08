// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_GALLERIES_IMPORTED_MEDIA_GALLERY_REGISTRY_H_
#define CHROME_BROWSER_MEDIA_GALLERIES_IMPORTED_MEDIA_GALLERY_REGISTRY_H_

#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/memory/scoped_ptr.h"

namespace itunes {
class ITunesDataProvider;
class ITunesDataProviderTest;
}

namespace picasa {
class PicasaDataProvider;
}

namespace chrome {

// This class lives on the MediaTaskRunner thread. It has some static
// methods which are called on the UI thread.
//
// MediaTaskRunner is not guaranteed to be one thread, but it is guaranteed
// to be a series of sequential calls. See SequencedTaskRunner for details.
class ImportedMediaGalleryRegistry {
 public:
  static ImportedMediaGalleryRegistry* GetInstance();

  // Should be called on the UI thread only.
  std::string RegisterPicasaFilesystemOnUIThread(
      const base::FilePath& database_path);

  std::string RegisterITunesFilesystemOnUIThread(
      const base::FilePath& xml_library_path);

  bool RevokeImportedFilesystemOnUIThread(const std::string& fsid);

  // Should be called on the MediaTaskRunner thread only.
#if defined(OS_WIN) || defined(OS_MACOSX)
  static picasa::PicasaDataProvider* PicasaDataProvider();
  static itunes::ITunesDataProvider* ITunesDataProvider();
#endif  // defined(OS_WIN) || defined(OS_MACOSX)

 private:
  friend struct base::DefaultLazyInstanceTraits<ImportedMediaGalleryRegistry>;
  friend class itunes::ITunesDataProviderTest;

  ImportedMediaGalleryRegistry();
  virtual ~ImportedMediaGalleryRegistry();

#if defined(OS_WIN) || defined(OS_MACOSX)
  void RegisterPicasaFileSystem(const base::FilePath& database_path);
  void RevokePicasaFileSystem();

  void RegisterITunesFileSystem(const base::FilePath& xml_library_path);
  void RevokeITunesFileSystem();

  // The data providers are only set or accessed on the task runner thread.
  scoped_ptr<picasa::PicasaDataProvider> picasa_data_provider_;
  scoped_ptr<itunes::ITunesDataProvider> itunes_data_provider_;

  // The remaining members are only accessed on the IO thread.
  std::set<std::string> picasa_fsids_;
  std::set<std::string> itunes_fsids_;

#ifndef NDEBUG
  base::FilePath picasa_database_path_;
  base::FilePath itunes_xml_library_path_;
#endif
#endif  // defined(OS_WIN) || defined(OS_MACOSX)

  DISALLOW_COPY_AND_ASSIGN(ImportedMediaGalleryRegistry);
};

}  // namespace chrome

#endif  // CHROME_BROWSER_MEDIA_GALLERIES_IMPORTED_MEDIA_GALLERY_REGISTRY_H_
