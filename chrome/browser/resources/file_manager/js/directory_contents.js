// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

/**
 * This class manages filters and determines a file should be shown or not.
 * When filters are changed, a 'changed' event is fired.
 *
 * @param {MetadataCache} metadataCache Metadata cache service.
 * @param {boolean} showHidden If files starting with '.' are shown.
 * @constructor
 * @extends {cr.EventTarget}
 */
function FileFilter(metadataCache, showHidden) {
  /**
   * @type {MetadataCache}
   * @private
   */
  this.metadataCache_ = metadataCache;
  /**
   * @type Object.<string, Function>
   * @private
   */
  this.filters_ = {};
  this.setFilterHidden(!showHidden);

  // Do not show entries marked as 'deleted'.
  this.addFilter('deleted', function(entry) {
    var internal = this.metadataCache_.getCached(entry, 'internal');
    return !(internal && internal.deleted);
  }.bind(this));
}

/*
 * FileFilter extends cr.EventTarget.
 */
FileFilter.prototype = {__proto__: cr.EventTarget.prototype};

/**
 * @param {string} name Filter identifier.
 * @param {function(Entry)} callback A filter — a function receiving an Entry,
 *     and returning bool.
 */
FileFilter.prototype.addFilter = function(name, callback) {
  this.filters_[name] = callback;
  cr.dispatchSimpleEvent(this, 'changed');
};

/**
 * @param {string} name Filter identifier.
 */
FileFilter.prototype.removeFilter = function(name) {
  delete this.filters_[name];
  cr.dispatchSimpleEvent(this, 'changed');
};

/**
 * @param {boolean} value If do not show hidden files.
 */
FileFilter.prototype.setFilterHidden = function(value) {
  if (value) {
    this.addFilter(
        'hidden',
        function(entry) { return entry.name.substr(0, 1) !== '.'; }
    );
  } else {
    this.removeFilter('hidden');
  }
};

/**
 * @return {boolean} If the files with names starting with "." are not shown.
 */
FileFilter.prototype.isFilterHiddenOn = function() {
  return 'hidden' in this.filters_;
};

/**
 * @param {Entry} entry File entry.
 * @return {boolean} True if the file should be shown, false otherwise.
 */
FileFilter.prototype.filter = function(entry) {
  for (var name in this.filters_) {
    if (!this.filters_[name](entry))
      return false;
  }
  return true;
};

/**
 * A context of DirectoryContents.
 * TODO(yoshiki): remove this. crbug.com/224869.
 *
 * @param {FileFilter} fileFilter The file-filter context.
 * @param {MetadataCache} metadataCache Metadata cache service.
 * @constructor
 */
function FileListContext(fileFilter, metadataCache) {
  /**
   * @type {cr.ui.ArrayDataModel}
   */
  this.fileList = new cr.ui.ArrayDataModel([]);
  /**
   * @type {MetadataCache}
   */
  this.metadataCache = metadataCache;

  /**
   * @type {FileFilter}
   */
  this.fileFilter = fileFilter;
}

/**
 * This class is responsible for scanning directory (or search results),
 * and filling the fileList. Different descendants handle various types of
 * directory contents shown: basic directory, drive search results, local search
 * results.
 * @param {FileListContext} context The file list context.
 * @constructor
 * @extends {cr.EventTarget}
 */
function DirectoryContents(context) {
  this.context_ = context;
  this.fileList_ = context.fileList;
  this.scanCompletedCallback_ = null;
  this.scanFailedCallback_ = null;
  this.scanCancelled_ = false;
  this.allChunksFetched_ = false;
  this.pendingMetadataRequests_ = 0;
  this.fileList_.prepareSort = this.prepareSort_.bind(this);
}

/**
 * DirectoryContents extends cr.EventTarget.
 */
DirectoryContents.prototype.__proto__ = cr.EventTarget.prototype;

/**
 * Create the copy of the object, but without scan started.
 * @return {DirectoryContents} Object copy.
 */
DirectoryContents.prototype.clone = function() {
  return new DirectoryContents(this.context_);
};

/**
 * Use a given fileList instead of the fileList from the context.
 * @param {Array|cr.ui.ArrayDataModel} fileList The new file list.
 */
DirectoryContents.prototype.setFileList = function(fileList) {
  this.fileList_ = fileList;
  this.fileList_.prepareSort = this.prepareSort_.bind(this);
};

/**
 * Use the filelist from the context and replace its contents with the entries
 * from the current fileList.
 */
DirectoryContents.prototype.replaceContextFileList = function() {
  if (this.context_.fileList !== this.fileList_) {
    var spliceArgs = [].slice.call(this.fileList_);
    var fileList = this.context_.fileList;
    spliceArgs.unshift(0, fileList.length);
    fileList.splice.apply(fileList, spliceArgs);
    this.fileList_ = fileList;
  }
};

/**
 * @return {boolean} If the scan is active.
 */
DirectoryContents.prototype.isScanning = function() {
  return !this.scanCancelled_ &&
         (!this.allChunksFetched_ || this.pendingMetadataRequests_ > 0);
};

/**
 * @return {boolean} True if search results (drive or local).
 */
DirectoryContents.prototype.isSearch = function() {
  return false;
};

/**
 * @return {DirectoryEntry} A DirectoryEntry for current directory. In case of
 *     search -- the top directory from which search is run.
 */
DirectoryContents.prototype.getDirectoryEntry = function() {
  throw 'Not implemented.';
};

/**
 * @return {DirectoryEntry} A DirectoryEntry for the last non search contents.
 */
DirectoryContents.prototype.getLastNonSearchDirectoryEntry = function() {
  throw 'Not implemented.';
};

/**
 * Start directory scan/search operation. Either 'scan-completed' or
 * 'scan-failed' event will be fired upon completion.
 */
DirectoryContents.prototype.scan = function() {
  throw 'Not implemented.';
};

/**
 * Read next chunk of results from DirectoryReader.
 * @protected
 */
DirectoryContents.prototype.readNextChunk = function() {
  throw 'Not implemented.';
};

/**
 * Cancel the running scan.
 */
DirectoryContents.prototype.cancelScan = function() {
  if (this.scanCancelled_)
    return;
  this.scanCancelled_ = true;
  cr.dispatchSimpleEvent(this, 'scan-cancelled');
};


/**
 * Called in case scan has failed. Should send the event.
 * @protected
 */
DirectoryContents.prototype.onError = function() {
  cr.dispatchSimpleEvent(this, 'scan-failed');
};

/**
 * Called in case scan has completed succesfully. Should send the event.
 * @protected
 */
DirectoryContents.prototype.lastChunkReceived = function() {
  this.allChunksFetched_ = true;
  if (!this.scanCancelled_ && this.pendingMetadataRequests_ === 0)
    cr.dispatchSimpleEvent(this, 'scan-completed');
};

/**
 * Cache necessary data before a sort happens.
 *
 * This is called by the table code before a sort happens, so that we can
 * go fetch data for the sort field that we may not have yet.
 * @param {string} field Sort field.
 * @param {function(Object)} callback Called when done.
 * @private
 */
DirectoryContents.prototype.prepareSort_ = function(field, callback) {
  this.prefetchMetadata(this.fileList_.slice(), callback);
};

/**
 * @param {Array.<Entry>} entries Files.
 * @param {function(Object)} callback Callback on done.
 */
DirectoryContents.prototype.prefetchMetadata = function(entries, callback) {
  this.context_.metadataCache.get(entries, 'filesystem', callback);
};

/**
 * @param {Array.<Entry>} entries Files.
 * @param {function(Object)} callback Callback on done.
 */
DirectoryContents.prototype.reloadMetadata = function(entries, callback) {
  this.context_.metadataCache.clear(entries, '*');
  this.context_.metadataCache.get(entries, 'filesystem', callback);
};

/**
 * @protected
 * @param {Array.<Entry>} entries File list.
 */
DirectoryContents.prototype.onNewEntries = function(entries) {
  if (this.scanCancelled_)
    return;

  var entriesFiltered = [].filter.call(
      entries, this.context_.fileFilter.filter.bind(this.context_.fileFilter));

  var onPrefetched = function() {
    this.pendingMetadataRequests_--;
    if (this.scanCancelled_)
      return;
    this.fileList_.push.apply(this.fileList_, entriesFiltered);

    if (this.pendingMetadataRequests_ === 0 && this.allChunksFetched_)
      cr.dispatchSimpleEvent(this, 'scan-completed');
    else
      cr.dispatchSimpleEvent(this, 'scan-updated');

    if (!this.allChunksFetched_)
      this.readNextChunk();
  };

  this.pendingMetadataRequests_++;
  this.prefetchMetadata(entriesFiltered, onPrefetched.bind(this));
};

/**
 * @param {string} name Directory name.
 * @param {function(DirectoryEntry)} successCallback Called on success.
 * @param {function(FileError)} errorCallback On error.
 */
DirectoryContents.prototype.createDirectory = function(
    name, successCallback, errorCallback) {
  throw 'Not implemented.';
};


/**
 * @param {FileListContext} context File list context.
 * @param {DirectoryEntry} entry DirectoryEntry for current directory.
 * @constructor
 * @extends {DirectoryContents}
 */
function DirectoryContentsBasic(context, entry) {
  DirectoryContents.call(this, context);
  this.entry_ = entry;
}

/**
 * Extends DirectoryContents
 */
DirectoryContentsBasic.prototype.__proto__ = DirectoryContents.prototype;

/**
 * Create the copy of the object, but without scan started.
 * @return {DirectoryContentsBasic} Object copy.
 */
DirectoryContentsBasic.prototype.clone = function() {
  return new DirectoryContentsBasic(this.context_, this.entry_);
};

/**
 * @return {DirectoryEntry} DirectoryEntry of the current directory.
 */
DirectoryContentsBasic.prototype.getDirectoryEntry = function() {
  return this.entry_;
};

/**
 * @return {DirectoryEntry} DirectoryEntry for the currnet entry.
 */
DirectoryContentsBasic.prototype.getLastNonSearchDirectoryEntry = function() {
  return this.entry_;
};

/**
 * Start directory scan.
 */
DirectoryContentsBasic.prototype.scan = function() {
  if (this.entry_ === DirectoryModel.fakeDriveEntry_) {
    this.lastChunkReceived();
    return;
  }

  metrics.startInterval('DirectoryScan');
  this.reader_ = this.entry_.createReader();
  this.readNextChunk();
};

/**
 * Read next chunk of results from DirectoryReader.
 * @protected
 */
DirectoryContentsBasic.prototype.readNextChunk = function() {
  this.reader_.readEntries(this.onChunkComplete_.bind(this),
                           this.onError.bind(this));
};

/**
 * @param {Array.<Entry>} entries File list.
 * @private
 */
DirectoryContentsBasic.prototype.onChunkComplete_ = function(entries) {
  if (this.scanCancelled_)
    return;

  if (entries.length == 0) {
    this.lastChunkReceived();
    this.recordMetrics_();
    return;
  }

  this.onNewEntries(entries);
};

/**
 * @private
 */
DirectoryContentsBasic.prototype.recordMetrics_ = function() {
  metrics.recordInterval('DirectoryScan');
  if (this.entry_.fullPath === RootDirectory.DOWNLOADS) {
    metrics.recordMediumCount('DownloadsCount', this.fileList_.length);
  }
};

/**
 * @param {string} name Directory name.
 * @param {function(Entry)} successCallback Called on success.
 * @param {function(FileError)} errorCallback On error.
 */
DirectoryContentsBasic.prototype.createDirectory = function(
    name, successCallback, errorCallback) {
  var onSuccess = function(newEntry) {
    this.reloadMetadata([newEntry], function() {
      successCallback(newEntry);
    });
  };

  this.entry_.getDirectory(name, {create: true, exclusive: true},
                           onSuccess.bind(this), errorCallback);
};

/**
 * Delay to be used for drive search scan.
 * The goal is to reduce the number of server requests when user is typing the
 * query.
 *
 * @type {number}
 * @const
 */
DirectoryContentsDriveSearch.SCAN_DELAY = 200;

/**
 * Maximum number of results which is shown on the search.
 *
 * @type {number}
 * @const
 */
DirectoryContentsDriveSearch.MAX_RESULTS = 100;

/**
 * @param {FileListContext} context File list context.
 * @param {DirectoryEntry} dirEntry Current directory.
 * @param {DirectoryEntry} previousDirEntry DirectoryEntry that was current
 *     before the search.
 * @param {string} query Search query.
 * @constructor
 * @extends {DirectoryContents}
 */
function DirectoryContentsDriveSearch(context,
                                      dirEntry,
                                      previousDirEntry,
                                      query) {
  DirectoryContents.call(this, context);
  this.directoryEntry_ = dirEntry;
  this.previousDirectoryEntry_ = previousDirEntry;
  this.query_ = query;
  this.nextFeed_ = '';
  this.done_ = false;
  this.fetchedResultsNum_ = 0;
}

/**
 * Extends DirectoryContents.
 */
DirectoryContentsDriveSearch.prototype.__proto__ = DirectoryContents.prototype;

/**
 * Create the copy of the object, but without scan started.
 * @return {DirectoryContentsBasic} Object copy.
 */
DirectoryContentsDriveSearch.prototype.clone = function() {
  return new DirectoryContentsDriveSearch(
      this.context_, this.directoryEntry_,
      this.previousDirectoryEntry_, this.query_);
};

/**
 * @return {boolean} True if this is search results (yes).
 */
DirectoryContentsDriveSearch.prototype.isSearch = function() {
  return true;
};

/**
 * @return {DirectoryEntry} A DirectoryEntry for the top directory from which
 *     search is run (i.e. drive root).
 */
DirectoryContentsDriveSearch.prototype.getDirectoryEntry = function() {
  return this.directoryEntry_;
};

/**
 * @return {DirectoryEntry} DirectoryEntry for the directory that was current
 *     before the search.
 */
DirectoryContentsDriveSearch.prototype.getLastNonSearchDirectoryEntry =
    function() {
  return this.previousDirectoryEntry_;
};

/**
 * Start directory scan.
 */
DirectoryContentsDriveSearch.prototype.scan = function() {
  // Let's give another search a chance to cancel us before we begin.
  setTimeout(this.readNextChunk.bind(this),
             DirectoryContentsDriveSearch.SCAN_DELAY);
};

/**
 * All the results are read in one chunk, so when we try to read second chunk,
 * it means we're done.
 */
DirectoryContentsDriveSearch.prototype.readNextChunk = function() {
  if (this.scanCancelled_)
    return;

  if (this.done_) {
    this.lastChunkReceived();
    return;
  }

  var searchCallback = (function(entries, nextFeed) {
    // TODO(tbarzic): Improve error handling.
    if (!entries) {
      console.error('Drive search encountered an error.');
      this.lastChunkReceived();
      return;
    }
    this.nextFeed_ = nextFeed;
    var remaining =
        DirectoryContentsDriveSearch.MAX_RESULTS - this.fetchedResultsNum_;
    if (entries.length >= remaining) {
      entries = entries.slice(0, remaining);
      this.nextFeed_ = '';
    }
    this.fetchedResultsNum_ += entries.length;

    this.done_ = (this.nextFeed_ == '');

    this.onNewEntries(entries);
  }).bind(this);

  var searchParams = {
    'query': this.query_,
    'nextFeed': this.nextFeed_
  };
  chrome.fileBrowserPrivate.searchDrive(searchParams, searchCallback);
};


/**
 * @param {FileListContext} context File list context.
 * @param {DirectoryEntry} dirEntry Current directory.
 * @param {string} query Search query.
 * @constructor
 * @extends {DirectoryContents}
 */
function DirectoryContentsLocalSearch(context, dirEntry, query) {
  DirectoryContents.call(this, context);
  this.directoryEntry_ = dirEntry;
  this.query_ = query.toLowerCase();
}

/**
 * Extends DirectoryContents
 */
DirectoryContentsLocalSearch.prototype.__proto__ = DirectoryContents.prototype;

/**
 * Create the copy of the object, but without scan started.
 * @return {DirectoryContentsBasic} Object copy.
 */
DirectoryContentsLocalSearch.prototype.clone = function() {
  return new DirectoryContentsLocalSearch(
      this.context_, this.directoryEntry_, this.query_);
};

/**
 * @return {boolean} True if search results (drive or local).
 */
DirectoryContentsLocalSearch.prototype.isSearch = function() {
  return true;
};

/**
 * @return {DirectoryEntry} A DirectoryEntry for the top directory from which
 *     search is run.
 */
DirectoryContentsLocalSearch.prototype.getDirectoryEntry = function() {
  return this.directoryEntry_;
};

/**
 * @return {DirectoryEntry} DirectoryEntry for current directory (the search is
 *     run from the directory that was current before search).
 */
DirectoryContentsLocalSearch.prototype.getLastNonSearchDirectoryEntry =
    function() {
  return this.directoryEntry_;
};

/**
 * Start directory scan/search operation. Either 'scan-completed' or
 * 'scan-failed' event will be fired upon completion.
 */
DirectoryContentsLocalSearch.prototype.scan = function() {
  this.pendingScans_ = 0;
  this.scanDirectory_(this.directoryEntry_);
};

/**
 * Scan a directory.
 * @param {DirectoryEntry} entry A directory to scan.
 * @private
 */
DirectoryContentsLocalSearch.prototype.scanDirectory_ = function(entry) {
  this.pendingScans_++;
  var reader = entry.createReader();
  var found = [];

  var onChunkComplete = function(entries) {
    if (this.scanCancelled_)
      return;

    if (entries.length === 0) {
      if (found.length > 0)
        this.onNewEntries(found);
      this.pendingScans_--;
      if (this.pendingScans_ === 0)
        this.lastChunkReceived();
      return;
    }

    for (var i = 0; i < entries.length; i++) {
      if (entries[i].name.toLowerCase().indexOf(this.query_) != -1) {
        found.push(entries[i]);
      }

      if (entries[i].isDirectory)
        this.scanDirectory_(entries[i]);
    }

    getNextChunk();
  }.bind(this);

  var getNextChunk = function() {
    reader.readEntries(onChunkComplete, this.onError.bind(this));
  }.bind(this);

  getNextChunk();
};

/**
 * We get results for each directory in one go in scanDirectory_.
 */
DirectoryContentsLocalSearch.prototype.readNextChunk = function() {
};

/**
 * List of search types for DirectoryContentsDriveSearch.
 * TODO(haruki): SHARED_WITH_ME support for searchDriveMetadata is not yet
 * implemented. Update this when it's done.
 * SEARCH_ALL uses no filtering.
 * SEARCH_SHARED_WITH_ME searches for the shared-with-me entries.
 * SEARCH_RECENT_FILES searches for recently accessed file entries.
 * @enum {number}
 */
DirectoryContentsDriveSearchMetadata.SearchType = {
  SEARCH_ALL: 0,
  SEARCH_SHARED_WITH_ME: 1,
  SEARCH_RECENT_FILES: 2,
  SEARCH_OFFLINE: 3
};

/**
 * DirectoryContents to list Drive files using searchDriveMetadata().
 *
 * @param {FileListContext} context File list context.
 * @param {DirectoryEntry} driveDirEntry Directory for actual Drive.
 * @param {DirectoryEntry} fakeDirEntry Fake directory representing the set of
 *     result files. This serves as a top directory for this search.
 * @param {string} query Search query to filter the files.
 * @param {DirectoryContentsDriveSearchMetadata.SearchType} searchType
 *     Type of search. searchDriveMetadata will restricts the entries based on
 *     the given search type.
 * @constructor
 * @extends {DirectoryContents}
 */
function DirectoryContentsDriveSearchMetadata(context,
                                              driveDirEntry,
                                              fakeDirEntry,
                                              query,
                                              searchType) {
  DirectoryContents.call(this, context);
  this.driveDirEntry_ = driveDirEntry;
  this.fakeDirEntry_ = fakeDirEntry;
  this.query_ = query;
  this.searchType_ = searchType;
}

/**
 * Creates a copy of the object, but without scan started.
 * @return {DirectoryContents} Object copy.
 */
DirectoryContentsDriveSearchMetadata.prototype.clone = function() {
  return new DirectoryContentsDriveSearchMetadata(
      this.context_, this.driveDirEntry_, this.fakeDirEntry_, this.query_,
      this.searchType_);
};

/**
 * Extends DirectoryContents.
 */
DirectoryContentsDriveSearchMetadata.prototype.__proto__ =
    DirectoryContents.prototype;

/**
 * @return {boolean} True if this is search results (yes).
 */
DirectoryContentsDriveSearchMetadata.prototype.isSearch = function() {
  return true;
};

/**
 * @return {DirectoryEntry} An Entry representing the current contents
 *     (i.e. fake root for "Shared with me").
 */
DirectoryContentsDriveSearchMetadata.prototype.getDirectoryEntry = function() {
  return this.fakeDirEntry_;
};

/**
 * @return {DirectoryEntry} DirectoryEntry for the directory that was current
 *     before the search.
 */
DirectoryContentsDriveSearchMetadata.prototype.getLastNonSearchDirectoryEntry =
    function() {
  return this.driveDirEntry_;
};

/**
 * Start directory scan/search operation. Either 'scan-completed' or
 * 'scan-failed' event will be fired upon completion.
 */
DirectoryContentsDriveSearchMetadata.prototype.scan = function() {
  this.readNextChunk();
};

/**
 * All the results are read in one chunk, so when we try to read second chunk,
 * it means we're done.
 */
DirectoryContentsDriveSearchMetadata.prototype.readNextChunk = function() {
  if (this.scanCancelled_)
    return;

  if (this.done_) {
    this.lastChunkReceived();
    return;
  }

  var searchCallback = (function(results, nextFeed) {
    if (!results) {
      console.error('Drive search encountered an error.');
      this.lastChunkReceived();
      return;
    }
    this.done_ = true;

    var entries = results.map(function(r) { return r.entry; });
    this.onNewEntries(entries);
    this.lastChunkReceived();
  }).bind(this);

  var type;
  switch (this.searchType_) {
    case DirectoryContentsDriveSearchMetadata.SearchType.SEARCH_ALL:
      type = 'ALL';
      break;
    case DirectoryContentsDriveSearchMetadata.SearchType.SEARCH_SHARED_WITH_ME:
      type = 'SHARED_WITH_ME';
      break;
    case DirectoryContentsDriveSearchMetadata.SearchType.SEARCH_RECENT_FILES:
      type = 'EXCLUDE_DIRECTORIES';
      break;
    case DirectoryContentsDriveSearchMetadata.SearchType.SEARCH_OFFLINE:
      type = 'OFFLINE';
      break;
    default:
      throw Error('Unknown search type: ' + this.searchType_);
  }
  var searchParams = {
    'query': this.query_,
    'types': type,
    'maxResults': 500
  };
  chrome.fileBrowserPrivate.searchDriveMetadata(searchParams, searchCallback);
};
