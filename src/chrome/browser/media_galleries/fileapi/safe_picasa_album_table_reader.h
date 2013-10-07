// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_SAFE_PICASA_ALBUM_TABLE_READER_H_
#define CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_SAFE_PICASA_ALBUM_TABLE_READER_H_

#include <string>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/weak_ptr.h"
#include "chrome/common/media_galleries/picasa_types.h"
#include "content/public/browser/utility_process_host.h"
#include "content/public/browser/utility_process_host_client.h"

namespace base {
class FilePath;
}

namespace IPC {
class Message;
}

namespace picasa {

// SafePicasaAlbumTableReader parses the given Picasa PMP Album Table safely
// via a utility process. The SafePicasaAlbumTableReader object is ref-counted
// and kept alive after Start() is called until the ParserCallback is called.
// The ParserCallback is guaranteed to be called eventually either when the
// utility process replies or when it dies.
class SafePicasaAlbumTableReader : public content::UtilityProcessHostClient {
 public:
  typedef base::Callback<void(bool,
                              const std::vector<AlbumInfo>&,
                              const std::vector<AlbumInfo>&)> ParserCallback;

  SafePicasaAlbumTableReader(const AlbumTableFiles& album_table_files,
                             const ParserCallback& callback);

  void Start();

 private:
  enum ParserState {
    INITIAL_STATE,
    PINGED_UTILITY_PROCESS_STATE,
    STARTED_PARSING_STATE,
    FINISHED_PARSING_STATE,
  };

  // Private because content::UtilityProcessHostClient is ref-counted.
  virtual ~SafePicasaAlbumTableReader();

  // Launches the utility process.  Must run on the IO thread.
  void StartWorkOnIOThread();

  // Notification that the utility process is running, and we can now get its
  // process handle.
  // Runs on the IO thread.
  void OnProcessStarted();

  // Notification from the utility process when it finshes parsing the PMP
  // database. This is received even if PMP parsing fails.
  // Runs on the IO thread.
  void OnParsePicasaPMPDatabaseFinished(bool parse_success,
                                        const std::vector<AlbumInfo>& albums,
                                        const std::vector<AlbumInfo>& folders);

  // UtilityProcessHostClient implementation.
  // Runs on the IO thread.
  virtual void OnProcessCrashed(int exit_code) OVERRIDE;
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

  const AlbumTableFiles album_table_files_;

  // Only accessed on the IO thread.
  base::WeakPtr<content::UtilityProcessHost> utility_process_host_;

  // Only accessed on the Media Task Runner.
  const ParserCallback callback_;

  // Verifies the messages from the utility process came at the right time.
  // Initialized on the Media Task Runner, but only accessed on the IO thread.
  ParserState parser_state_;

  DISALLOW_COPY_AND_ASSIGN(SafePicasaAlbumTableReader);
};

}  // namespace picasa

#endif  // CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_SAFE_PICASA_ALBUM_TABLE_READER_H_
