// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_ITUNES_DATA_PROVIDER_H_
#define CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_ITUNES_DATA_PROVIDER_H_

#include <map>
#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/callback_forward.h"
#include "base/files/file_path.h"
#include "base/files/file_path_watcher.h"
#include "chrome/browser/media_galleries/fileapi/safe_itunes_library_parser.h"

namespace itunes {

class TestITunesDataProvider;

// This class is the holder for iTunes parsed data. Given a path to the iTunes
// library XML file it will read it in, parse the data, and provide convenient
// methods to access it.  When the file changes, it will update the data.
// It is not thread safe, but can be run on any thread with IO access.
class ITunesDataProvider {
 public:
  typedef std::string ArtistName;
  typedef std::string AlbumName;
  typedef std::string TrackName;
  typedef std::map<TrackName, base::FilePath> Album;
  typedef base::Callback<void(bool)> ReadyCallback;

  explicit ITunesDataProvider(const base::FilePath& library_path);
  virtual ~ITunesDataProvider();

  // Ask the data provider to refresh the data if necessary. |ready_callback|
  // will be called with the result; false if unable to parse the XML file.
  void RefreshData(const ReadyCallback& ready_callback);

  // Get the platform path for the library XML file.
  const base::FilePath& library_path() const;

  // Returns true if |artist| exists in the library.
  bool KnownArtist(const ArtistName& artist) const;

  // Returns true if |artist| has an album by the name |album| in the library.
  bool KnownAlbum(const ArtistName& artist, const AlbumName& album) const;

  // Get the track named (filename basename) |track| in |album| by |artist|.
  // If no such track exists, an empty FilePath is returned.
  base::FilePath GetTrackLocation(const ArtistName& artist,
                                  const AlbumName& album,
                                  const TrackName& track) const;

  // Get the set of artists.
  std::set<ArtistName> GetArtistNames() const;

  // Get the set of albums for |artist|.
  std::set<AlbumName> GetAlbumNames(const ArtistName& artist) const;

  // Get the tracks for the |album| by |artist|.
  Album GetAlbum(const ArtistName& artist, const AlbumName& album) const;

 private:
  friend class TestITunesDataProvider;

  typedef std::map<AlbumName, Album> Artist;
  typedef std::map<ArtistName, Artist> Library;

  // These are hacks to work around http://crbug.com/165590. Otherwise a
  // WeakPtrFactory would be the obvious answer here.
  // static so they can call their real counterparts.
  // TODO(vandebo) Remove these when the bug is fixed.
  static void OnLibraryWatchStartedCallback(
      scoped_ptr<base::FilePathWatcher> library_watcher);
  static void OnLibraryChangedCallback(const base::FilePath& path, bool error);
  static void OnLibraryParsedCallback(const ReadyCallback& ready_callback,
                                      bool result,
                                      const parser::Library& library);

  // Called when the FilePathWatcher for |library_path_| has tried to add an
  // watch.
  void OnLibraryWatchStarted(scoped_ptr<base::FilePathWatcher> library_watcher);

  // Called when |library_path_| has changed. Virtual for testing.
  virtual void OnLibraryChanged(const base::FilePath& path, bool error);

  // Called when the utility process finishes parsing the library XML file.
  void OnLibraryParsed(const ReadyCallback& ready_callback,
                       bool result,
                       const parser::Library& library);

  // Path to the library XML file.
  const base::FilePath library_path_;

  // The parsed and uniquified data.
  Library library_;

  // True if the data needs to be refreshed from disk.
  bool needs_refresh_;

  // True if |library_| contain valid data.  False at construction and if
  // reading or parsing the XML file fails.
  bool is_valid_;

  // A watcher on the library xml file.
  scoped_ptr<base::FilePathWatcher> library_watcher_;

  scoped_refptr<SafeITunesLibraryParser> xml_parser_;

  DISALLOW_COPY_AND_ASSIGN(ITunesDataProvider);
};

}  // namespace itunes

#endif  // CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_ITUNES_DATA_PROVIDER_H_
