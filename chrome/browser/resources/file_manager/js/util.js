// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

/**
 * Namespace for utility functions.
 */
var util = {};

/**
 * Returns a function that console.log's its arguments, prefixed by |msg|.
 *
 * @param {string} msg The message prefix to use in the log.
 * @param {function(...string)=} opt_callback A function to invoke after
 *     logging.
 * @return {function(...string)} Function that logs.
 */
util.flog = function(msg, opt_callback) {
  return function() {
    var ary = Array.apply(null, arguments);
    console.log(msg + ': ' + ary.join(', '));
    if (opt_callback)
      opt_callback.apply(null, arguments);
  };
};

/**
 * Returns a function that throws an exception that includes its arguments
 * prefixed by |msg|.
 *
 * @param {string} msg The message prefix to use in the exception.
 * @return {function(...string)} Function that throws.
 */
util.ferr = function(msg) {
  return function() {
    var ary = Array.apply(null, arguments);
    throw new Error(msg + ': ' + ary.join(', '));
  };
};

/**
 * Install a sensible toString() on the FileError object.
 *
 * FileError.prototype.code is a numeric code describing the cause of the
 * error.  The FileError constructor has a named property for each possible
 * error code, but provides no way to map the code to the named property.
 * This toString() implementation fixes that.
 */
util.installFileErrorToString = function() {
  FileError.prototype.toString = function() {
    return '[object FileError: ' + util.getFileErrorMnemonic(this.code) + ']';
  };
};

/**
 * @param {number} code The file error code.
 * @return {string} The file error mnemonic.
 */
util.getFileErrorMnemonic = function(code) {
  for (var key in FileError) {
    if (key.search(/_ERR$/) != -1 && FileError[key] == code)
      return key;
  }

  return code;
};

/**
 * @param {number} code File error code (from FileError object).
 * @return {string} Translated file error string.
 */
util.getFileErrorString = function(code) {
  for (var key in FileError) {
    var match = /(.*)_ERR$/.exec(key);
    if (match && FileError[key] == code) {
      // This would convert 1 to 'NOT_FOUND'.
      code = match[1];
      break;
    }
  }
  console.warn('File error: ' + code);
  return loadTimeData.getString('FILE_ERROR_' + code) ||
      loadTimeData.getString('FILE_ERROR_GENERIC');
};

/**
 * @param {string} str String to escape.
 * @return {string} Escaped string.
 */
util.htmlEscape = function(str) {
  return str.replace(/[<>&]/g, function(entity) {
    switch (entity) {
      case '<': return '&lt;';
      case '>': return '&gt;';
      case '&': return '&amp;';
    }
  });
};

/**
 * @param {string} str String to unescape.
 * @return {string} Unescaped string.
 */
util.htmlUnescape = function(str) {
  return str.replace(/&(lt|gt|amp);/g, function(entity) {
    switch (entity) {
      case '&lt;': return '<';
      case '&gt;': return '>';
      case '&amp;': return '&';
    }
  });
};

/**
 * Given a list of Entries, recurse any DirectoryEntries if |recurse| is true,
 * and call back with a list of all file and directory entries encountered
 * (including the original set).
 * @param {Array.<Entry>} entries List of entries.
 * @param {boolean} recurse Whether to recurse.
 * @param {function(Object)} successCallback Object has the fields dirEntries,
 *     fileEntries and fileBytes.
 */
util.recurseAndResolveEntries = function(entries, recurse, successCallback) {
  var pendingSubdirectories = 0;
  var pendingFiles = 0;

  var dirEntries = [];
  var fileEntries = [];
  var fileBytes = 0;

  var steps = {
    // Start operations.
    start: function() {
      for (var i = 0; i < entries.length; i++) {
        var parentPath = PathUtil.getParentDirectory(entries[i].fullPath);
        steps.tallyEntry(entries[i], parentPath);
      }
      steps.areWeThereYet();
    },

    // Process one entry.
    tallyEntry: function(entry, originalSourcePath) {
      entry.originalSourcePath = originalSourcePath;
      if (entry.isDirectory) {
        dirEntries.push(entry);
        if (!recurse)
          return;
        pendingSubdirectories++;
        util.forEachDirEntry(entry, function(inEntry) {
          if (inEntry == null) {
            // Null entry indicates we're done scanning this directory.
            pendingSubdirectories--;
            steps.areWeThereYet();
            return;
          }
          steps.tallyEntry(inEntry, originalSourcePath);
        });
      } else {
        fileEntries.push(entry);
        pendingFiles++;
        entry.getMetadata(function(metadata) {
          fileBytes += metadata.size;
          pendingFiles--;
          steps.areWeThereYet();
        });
      }
    },

    // We invoke this after each async callback to see if we've received all
    // the expected callbacks.  If so, we're done.
    areWeThereYet: function() {
      if (!successCallback || pendingSubdirectories != 0 || pendingFiles != 0)
        return;
      var pathCompare = function(a, b) {
        if (a.fullPath > b.fullPath)
          return 1;
        if (a.fullPath < b.fullPath)
          return -1;
        return 0;
      };
      var result = {
        dirEntries: dirEntries.sort(pathCompare),
        fileEntries: fileEntries.sort(pathCompare),
        fileBytes: fileBytes
      };
      successCallback(result);
    }
  };

  steps.start();
};

/**
 * Utility function to invoke callback once for each entry in dirEntry.
 * callback is called with 'null' after all entries are visited to indicate
 * the end of the directory scan.
 *
 * @param {DirectoryEntry} dirEntry The directory entry to enumerate.
 * @param {function(Entry)} callback The function to invoke for each entry in
 *     dirEntry.
 */
util.forEachDirEntry = function(dirEntry, callback) {
  var reader;

  var onError = function(err) {
    console.error('Failed to read  dir entries at ' + dirEntry.fullPath);
  };

  var onReadSome = function(results) {
    if (results.length == 0)
      return callback(null);

    for (var i = 0; i < results.length; i++)
      callback(results[i]);

    reader.readEntries(onReadSome, onError);
  };

  reader = dirEntry.createReader();
  reader.readEntries(onReadSome, onError);
};

/**
 * Reads contents of directory.
 * @param {DirectoryEntry} root Root entry.
 * @param {string} path Directory path.
 * @param {function(Array.<Entry>)} callback List of entries passed to callback.
 */
util.readDirectory = function(root, path, callback) {
  var onError = function(e) {
    callback([], e);
  };
  root.getDirectory(path, {create: false}, function(entry) {
    var reader = entry.createReader();
    var r = [];
    var readNext = function() {
      reader.readEntries(function(results) {
        if (results.length == 0) {
          callback(r, null);
          return;
        }
        r.push.apply(r, results);
        readNext();
      }, onError);
    };
    readNext();
  }, onError);
};

/**
 * Utility function to resolve multiple directories with a single call.
 *
 * The successCallback will be invoked once for each directory object
 * found.  The errorCallback will be invoked once for each
 * path that could not be resolved.
 *
 * The successCallback is invoked with a null entry when all paths have
 * been processed.
 *
 * @param {DirEntry} dirEntry The base directory.
 * @param {Object} params The parameters to pass to the underlying
 *     getDirectory calls.
 * @param {Array.<string>} paths The list of directories to resolve.
 * @param {function(!DirEntry)} successCallback The function to invoke for
 *     each DirEntry found.  Also invoked once with null at the end of the
 *     process.
 * @param {function(FileError)} errorCallback The function to invoke
 *     for each path that cannot be resolved.
 */
util.getDirectories = function(dirEntry, params, paths, successCallback,
                               errorCallback) {

  // Copy the params array, since we're going to destroy it.
  params = [].slice.call(params);

  var onComplete = function() {
    successCallback(null);
  };

  var getNextDirectory = function() {
    var path = paths.shift();
    if (!path)
      return onComplete();

    dirEntry.getDirectory(
      path, params,
      function(entry) {
        successCallback(entry);
        getNextDirectory();
      },
      function(err) {
        errorCallback(err);
        getNextDirectory();
      });
  };

  getNextDirectory();
};

/**
 * Utility function to resolve multiple files with a single call.
 *
 * The successCallback will be invoked once for each directory object
 * found.  The errorCallback will be invoked once for each
 * path that could not be resolved.
 *
 * The successCallback is invoked with a null entry when all paths have
 * been processed.
 *
 * @param {DirEntry} dirEntry The base directory.
 * @param {Object} params The parameters to pass to the underlying
 *     getFile calls.
 * @param {Array.<string>} paths The list of files to resolve.
 * @param {function(!FileEntry)} successCallback The function to invoke for
 *     each FileEntry found.  Also invoked once with null at the end of the
 *     process.
 * @param {function(FileError)} errorCallback The function to invoke
 *     for each path that cannot be resolved.
 */
util.getFiles = function(dirEntry, params, paths, successCallback,
                         errorCallback) {
  // Copy the params array, since we're going to destroy it.
  params = [].slice.call(params);

  var onComplete = function() {
    successCallback(null);
  };

  var getNextFile = function() {
    var path = paths.shift();
    if (!path)
      return onComplete();

    dirEntry.getFile(
      path, params,
      function(entry) {
        successCallback(entry);
        getNextFile();
      },
      function(err) {
        errorCallback(err);
        getNextFile();
      });
  };

  getNextFile();
};

/**
 * Resolve a path to either a DirectoryEntry or a FileEntry, regardless of
 * whether the path is a directory or file.
 *
 * @param {DirectoryEntry} root The root of the filesystem to search.
 * @param {string} path The path to be resolved.
 * @param {function(Entry)} resultCallback Called back when a path is
 *     successfully resolved. Entry will be either a DirectoryEntry or
 *     a FileEntry.
 * @param {function(FileError)} errorCallback Called back if an unexpected
 *     error occurs while resolving the path.
 */
util.resolvePath = function(root, path, resultCallback, errorCallback) {
  if (path == '' || path == '/') {
    resultCallback(root);
    return;
  }

  root.getFile(
      path, {create: false},
      resultCallback,
      function(err) {
        if (err.code == FileError.TYPE_MISMATCH_ERR) {
          // Bah.  It's a directory, ask again.
          root.getDirectory(
              path, {create: false},
              resultCallback,
              errorCallback);
        } else {
          errorCallback(err);
        }
      });
};

/**
 * Locate the file referred to by path, creating directories or the file
 * itself if necessary.
 * @param {DirEntry} root The root entry.
 * @param {string} path The file path.
 * @param {function(FileEntry)} successCallback The callback.
 * @param {function(FileError)} errorCallback The callback.
 */
util.getOrCreateFile = function(root, path, successCallback, errorCallback) {
  var dirname = null;
  var basename = null;

  var onDirFound = function(dirEntry) {
    dirEntry.getFile(basename, { create: true },
                     successCallback, errorCallback);
  };

  var i = path.lastIndexOf('/');
  if (i > -1) {
    dirname = path.substr(0, i);
    basename = path.substr(i + 1);
  } else {
    basename = path;
  }

  if (!dirname) {
    onDirFound(root);
    return;
  }

  util.getOrCreateDirectory(root, dirname, onDirFound, errorCallback);
};

/**
 * Locate the directory referred to by path, creating directories along the
 * way.
 * @param {DirEntry} root The root entry.
 * @param {string} path The directory path.
 * @param {function(FileEntry)} successCallback The callback.
 * @param {function(FileError)} errorCallback The callback.
 */
util.getOrCreateDirectory = function(root, path, successCallback,
                                     errorCallback) {
  var names = path.split('/');

  var getOrCreateNextName = function(dir) {
    if (!names.length)
      return successCallback(dir);

    var name;
    do {
      name = names.shift();
    } while (!name || name == '.');

    dir.getDirectory(name, { create: true }, getOrCreateNextName,
                     errorCallback);
  };

  getOrCreateNextName(root);
};

/**
 * Remove a file or a directory.
 * @param {Entry} entry The entry to remove.
 * @param {function()} onSuccess The success callback.
 * @param {function(FileError)} onError The error callback.
 */
util.removeFileOrDirectory = function(entry, onSuccess, onError) {
  if (entry.isDirectory)
    entry.removeRecursively(onSuccess, onError);
  else
    entry.remove(onSuccess, onError);
};

/**
 * Checks if an entry exists at |relativePath| in |dirEntry|.
 * If exists, tries to deduplicate the path by inserting parenthesized number,
 * such as " (1)", before the extension. If it still exists, tries the
 * deduplication again by increasing the number up to 10 times.
 * For example, suppose "file.txt" is given, "file.txt", "file (1).txt",
 * "file (2).txt", ..., "file (9).txt" will be tried.
 *
 * @param {DirectoryEntry} dirEntry The target directory entry.
 * @param {string} relativePath The path to be deduplicated.
 * @param {function(string)} onSuccess Called with the deduplicated path on
 *     success.
 * @param {function(FileError)} onError Called on error.
 */
util.deduplicatePath = function(dirEntry, relativePath, onSuccess, onError) {
  // The trial is up to 10.
  var MAX_RETRY = 10;

  // Crack the path into three part. The parenthesized number (if exists) will
  // be replaced by incremented number for retry. For example, suppose
  // |relativePath| is "file (10).txt", the second check path will be
  // "file (11).txt".
  var match = /^(.*?)(?: \((\d+)\))?(\.[^.]*?)?$/.exec(relativePath);
  var prefix = match[1];
  var copyNumber = match[2] ? parseInt(match[2], 10) : 0;
  var ext = match[3] ? match[3] : '';

  // The path currently checking the existence.
  var trialPath = relativePath;

  var onNotResolved = function(err) {
    // We expect to be unable to resolve the target file, since we're going
    // to create it during the copy.  However, if the resolve fails with
    // anything other than NOT_FOUND, that's trouble.
    if (err.code != FileError.NOT_FOUND_ERR) {
      onError(err);
      return;
    }

    // Found a path that doesn't exist.
    onSuccess(trialPath);
  }

  var numRetry = MAX_RETRY;
  var onResolved = function(entry) {
    if (--numRetry == 0) {
      // Hit the limit of the number of retrial.
      // Note that we cannot create FileError object directly, so here we use
      // Object.create instead.
      onError(util.createFileError(FileError.PATH_EXISTS_ERR));
      return;
    }

    ++copyNumber;
    trialPath = prefix + ' (' + copyNumber + ')' + ext;
    util.resolvePath(dirEntry, trialPath, onResolved, onNotResolved);
  };

  // Check to see if the target exists.
  util.resolvePath(dirEntry, trialPath, onResolved, onNotResolved);
};

/**
 * Convert a number of bytes into a human friendly format, using the correct
 * number separators.
 *
 * @param {number} bytes The number of bytes.
 * @return {string} Localized string.
 */
util.bytesToString = function(bytes) {
  // Translation identifiers for size units.
  var UNITS = ['SIZE_BYTES',
               'SIZE_KB',
               'SIZE_MB',
               'SIZE_GB',
               'SIZE_TB',
               'SIZE_PB'];

  // Minimum values for the units above.
  var STEPS = [0,
               Math.pow(2, 10),
               Math.pow(2, 20),
               Math.pow(2, 30),
               Math.pow(2, 40),
               Math.pow(2, 50)];

  var str = function(n, u) {
    // TODO(rginda): Switch to v8Locale's number formatter when it's
    // available.
    return strf(u, n.toLocaleString());
  };

  var fmt = function(s, u) {
    var rounded = Math.round(bytes / s * 10) / 10;
    return str(rounded, u);
  };

  // Less than 1KB is displayed like '80 bytes'.
  if (bytes < STEPS[1]) {
    return str(bytes, UNITS[0]);
  }

  // Up to 1MB is displayed as rounded up number of KBs.
  if (bytes < STEPS[2]) {
    var rounded = Math.ceil(bytes / STEPS[1]);
    return str(rounded, UNITS[1]);
  }

  // This loop index is used outside the loop if it turns out |bytes|
  // requires the largest unit.
  var i;

  for (i = 2 /* MB */; i < UNITS.length - 1; i++) {
    if (bytes < STEPS[i + 1])
      return fmt(STEPS[i], UNITS[i]);
  }

  return fmt(STEPS[i], UNITS[i]);
};

/**
 * Utility function to read specified range of bytes from file
 * @param {File} file The file to read.
 * @param {number} begin Starting byte(included).
 * @param {number} end Last byte(excluded).
 * @param {function(File, Uint8Array)} callback Callback to invoke.
 * @param {function(FileError)} onError Error handler.
 */
util.readFileBytes = function(file, begin, end, callback, onError) {
  var fileReader = new FileReader();
  fileReader.onerror = onError;
  fileReader.onloadend = function() {
    callback(file, new ByteReader(fileReader.result));
  };
  fileReader.readAsArrayBuffer(file.slice(begin, end));
};

/**
 * Write a blob to a file.
 * Truncates the file first, so the previous content is fully overwritten.
 * @param {FileEntry} entry File entry.
 * @param {Blob} blob The blob to write.
 * @param {function(Event)} onSuccess Completion callback. The first argument is
 *     a 'writeend' event.
 * @param {function(FileError)} onError Error handler.
 */
util.writeBlobToFile = function(entry, blob, onSuccess, onError) {
  var truncate = function(writer) {
    writer.onerror = onError;
    writer.onwriteend = write.bind(null, writer);
    writer.truncate(0);
  };

  var write = function(writer) {
    writer.onwriteend = onSuccess;
    writer.write(blob);
  };

  entry.createWriter(truncate, onError);
};

/**
 * Returns a string '[Ctrl-][Alt-][Shift-][Meta-]' depending on the event
 * modifiers. Convenient for writing out conditions in keyboard handlers.
 *
 * @param {Event} event The keyboard event.
 * @return {string} Modifiers.
 */
util.getKeyModifiers = function(event) {
  return (event.ctrlKey ? 'Ctrl-' : '') +
         (event.altKey ? 'Alt-' : '') +
         (event.shiftKey ? 'Shift-' : '') +
         (event.metaKey ? 'Meta-' : '');
};

/**
 * @param {HTMLElement} element Element to transform.
 * @param {Object} transform Transform object,
 *                           contains scaleX, scaleY and rotate90 properties.
 */
util.applyTransform = function(element, transform) {
  element.style.webkitTransform =
      transform ? 'scaleX(' + transform.scaleX + ') ' +
                  'scaleY(' + transform.scaleY + ') ' +
                  'rotate(' + transform.rotate90 * 90 + 'deg)' :
      '';
};

/**
 * Makes filesystem: URL from the path.
 * @param {string} path File or directory path.
 * @return {string} URL.
 */
util.makeFilesystemUrl = function(path) {
  path = path.split('/').map(encodeURIComponent).join('/');
  var prefix = 'external';
  return 'filesystem:' + document.location.origin + '/' + prefix + path;
};

/**
 * Extracts path from filesystem: URL.
 * @param {string} url Filesystem URL.
 * @return {string} The path.
 */
util.extractFilePath = function(url) {
  var match =
      /^filesystem:[\w-]*:\/\/[\w]*\/(external|persistent|temporary)(\/.*)$/.
      exec(url);
  var path = match && match[2];
  if (!path) return null;
  return decodeURIComponent(path);
};

/**
 * Traverses a tree up to a certain depth.
 * @param {FileEntry} root Root entry.
 * @param {function(Array.<Entry>)} callback The callback is called at the very
 *     end with a list of entries found.
 * @param {number?} max_depth Maximum depth. Pass zero to traverse everything.
 * @param {function(entry):boolean=} opt_filter Optional filter to skip some
 *     files/directories.
 */
util.traverseTree = function(root, callback, max_depth, opt_filter) {
  var list = [];
  util.forEachEntryInTree(root, function(entry) {
    if (entry) {
      list.push(entry);
    } else {
      callback(list);
    }
    return true;
  }, max_depth, opt_filter);
};

/**
 * Traverses a tree up to a certain depth, and calls a callback for each entry.
 * callback is called with 'null' after all entries are visited to indicate
 * the end of the traversal.
 * @param {FileEntry} root Root entry.
 * @param {function(Entry):boolean} callback The callback is called for each
 *     entry, and then once with null passed. If callback returns false once,
 *     the whole traversal is stopped.
 * @param {number?} max_depth Maximum depth. Pass zero to traverse everything.
 * @param {function(entry):boolean=} opt_filter Optional filter to skip some
 *     files/directories.
 */
util.forEachEntryInTree = function(root, callback, max_depth, opt_filter) {
  if (root.isFile) {
    if (opt_filter && !opt_filter(root)) {
      callback(null);
      return;
    }
    if (callback(root))
      callback(null);
    return;
  }

  var pending = 0;
  var cancelled = false;

  var maybeDone = function() {
    if (pending == 0 && !cancelled)
      callback(null);
  };

  var readEntry = function(entry, depth) {
    if (cancelled) return;
    if (opt_filter && !opt_filter(entry)) return;

    if (!callback(entry)) {
      cancelled = true;
      return;
    }

    // Do not recurse too deep and into files.
    if (entry.isFile || (max_depth != 0 && depth >= max_depth))
      return;

    pending++;
    util.forEachDirEntry(entry, function(childEntry) {
      if (childEntry == null) {
        // Null entry indicates we're done scanning this directory.
        pending--;
        maybeDone();
      } else {
        readEntry(childEntry, depth + 1);
      }
    });
  };

  readEntry(root, 0);
};

/**
 * A shortcut function to create a child element with given tag and class.
 *
 * @param {HTMLElement} parent Parent element.
 * @param {string=} opt_className Class name.
 * @param {string=} opt_tag Element tag, DIV is omitted.
 * @return {Element} Newly created element.
 */
util.createChild = function(parent, opt_className, opt_tag) {
  var child = parent.ownerDocument.createElement(opt_tag || 'div');
  if (opt_className)
    child.className = opt_className;
  parent.appendChild(child);
  return child;
};

/**
 * Update the app state.
 *
 * @param {string} path Path to be put in the address bar after the hash.
 *   If null the hash is left unchanged.
 * @param {string|Object=} opt_param Search parameter. Used directly if string,
 *   stringified if object. If omitted the search query is left unchanged.
 */
util.updateAppState = function(path, opt_param) {
  window.appState = window.appState || {};
  if (typeof opt_param == 'string')
    window.appState.params = {};
  else if (typeof opt_param == 'object')
    window.appState.params = opt_param;
  if (path)
    window.appState.defaultPath = path;
  util.saveAppState();
  return;
};

/**
 * Return a translated string.
 *
 * Wrapper function to make dealing with translated strings more concise.
 * Equivalent to loadTimeData.getString(id).
 *
 * @param {string} id The id of the string to return.
 * @return {string} The translated string.
 */
function str(id) {
  return loadTimeData.getString(id);
}

/**
 * Return a translated string with arguments replaced.
 *
 * Wrapper function to make dealing with translated strings more concise.
 * Equivilant to loadTimeData.getStringF(id, ...).
 *
 * @param {string} id The id of the string to return.
 * @param {...string} var_args The values to replace into the string.
 * @return {string} The translated string with replaced values.
 */
function strf(id, var_args) {
  return loadTimeData.getStringF.apply(loadTimeData, arguments);
}

/**
 * Adapter object that abstracts away the the difference between Chrome app APIs
 * v1 and v2. Is only necessary while the migration to v2 APIs is in progress.
 * TODO(mtomasz): Clean up this. crbug.com/240606.
 */
util.platform = {
  /**
   * @return {boolean} True if Files.app is running via "chrome://files", open
   * files or select folder dialog. False otherwise.
   */
  runningInBrowser: function() {
    return !window.appID;
  },

  /**
   * @param {function(Object)} callback Function accepting a preference map.
   */
  getPreferences: function(callback) {
    chrome.storage.local.get(callback);
  },

  /**
   * @param {string} key Preference name.
   * @param {function(string)} callback Function accepting the preference value.
   */
  getPreference: function(key, callback) {
    chrome.storage.local.get(key, function(items) {
      callback(items[key]);
    });
  },

  /**
   * @param {string} key Preference name.
   * @param {string|Object} value Preference value.
   * @param {function()=} opt_callback Completion callback.
   */
  setPreference: function(key, value, opt_callback) {
    if (typeof value != 'string')
      value = JSON.stringify(value);

    var items = {};
    items[key] = value;
    chrome.storage.local.set(items, opt_callback);
  }
};

/**
 * Attach page load handler.
 * @param {function()} handler Application-specific load handler.
 */
util.addPageLoadHandler = function(handler) {
  document.addEventListener('DOMContentLoaded', function() {
    handler();
  });
};

/**
 * Save app launch data to the local storage.
 */
util.saveAppState = function() {
  if (window.appState)
    util.platform.setPreference(window.appID, window.appState);
};

/**
 *  AppCache is a persistent timestamped key-value storage backed by
 *  HTML5 local storage.
 *
 *  It is not designed for frequent access. In order to avoid costly
 *  localStorage iteration all data is kept in a single localStorage item.
 *  There is no in-memory caching, so concurrent access is _almost_ safe.
 *
 *  TODO(kaznacheev) Reimplement this based on Indexed DB.
 */
util.AppCache = function() {};

/**
 * Local storage key.
 */
util.AppCache.KEY = 'AppCache';

/**
 * Max number of items.
 */
util.AppCache.CAPACITY = 100;

/**
 * Default lifetime.
 */
util.AppCache.LIFETIME = 30 * 24 * 60 * 60 * 1000;  // 30 days.

/**
 * @param {string} key Key.
 * @param {function(number)} callback Callback accepting a value.
 */
util.AppCache.getValue = function(key, callback) {
  util.AppCache.read_(function(map) {
    var entry = map[key];
    callback(entry && entry.value);
  });
};

/**
 * Update the cache.
 *
 * @param {string} key Key.
 * @param {string} value Value. Remove the key if value is null.
 * @param {number=} opt_lifetime Maximim time to keep an item (in milliseconds).
 */
util.AppCache.update = function(key, value, opt_lifetime) {
  util.AppCache.read_(function(map) {
    if (value != null) {
      map[key] = {
        value: value,
        expire: Date.now() + (opt_lifetime || util.AppCache.LIFETIME)
      };
    } else if (key in map) {
      delete map[key];
    } else {
      return;  // Nothing to do.
    }
    util.AppCache.cleanup_(map);
    util.AppCache.write_(map);
  });
};

/**
 * @param {function(Object)} callback Callback accepting a map of timestamped
 *   key-value pairs.
 * @private
 */
util.AppCache.read_ = function(callback) {
  util.platform.getPreference(util.AppCache.KEY, function(json) {
    if (json) {
      try {
        callback(JSON.parse(json));
      } catch (e) {
        // The local storage item somehow got messed up, start fresh.
      }
    }
    callback({});
  });
};

/**
 * @param {Object} map A map of timestamped key-value pairs.
 * @private
 */
util.AppCache.write_ = function(map) {
  util.platform.setPreference(util.AppCache.KEY, JSON.stringify(map));
};

/**
 * Remove over-capacity and obsolete items.
 *
 * @param {Object} map A map of timestamped key-value pairs.
 * @private
 */
util.AppCache.cleanup_ = function(map) {
  // Sort keys by ascending timestamps.
  var keys = [];
  for (var key in map) {
    if (map.hasOwnProperty(key))
      keys.push(key);
  }
  keys.sort(function(a, b) { return map[a].expire > map[b].expire });

  var cutoff = Date.now();

  var obsolete = 0;
  while (obsolete < keys.length &&
         map[keys[obsolete]].expire < cutoff) {
    obsolete++;
  }

  var overCapacity = Math.max(0, keys.length - util.AppCache.CAPACITY);

  var itemsToDelete = Math.max(obsolete, overCapacity);
  for (var i = 0; i != itemsToDelete; i++) {
    delete map[keys[i]];
  }
};

/**
 * Load an image.
 *
 * @param {Image} image Image element.
 * @param {string} url Source url.
 * @param {Object=} opt_options Hash array of options, eg. width, height,
 *     maxWidth, maxHeight, scale, cache.
 * @param {function()=} opt_isValid Function returning false iff the task
 *     is not valid and should be aborted.
 * @return {?number} Task identifier or null if fetched immediately from
 *     cache.
 */
util.loadImage = function(image, url, opt_options, opt_isValid) {
  return ImageLoaderClient.loadToImage(url,
                                      image,
                                      opt_options || {},
                                      function() {},
                                      function() { image.onerror(); },
                                      opt_isValid);
};

/**
 * Cancels loading an image.
 * @param {number} taskId Task identifier returned by util.loadImage().
 */
util.cancelLoadImage = function(taskId) {
  ImageLoaderClient.getInstance().cancel(taskId);
};

/**
 * Finds proerty descriptor in the object prototype chain.
 * @param {Object} object The object.
 * @param {string} propertyName The property name.
 * @return {Object} Property descriptor.
 */
util.findPropertyDescriptor = function(object, propertyName) {
  for (var p = object; p; p = Object.getPrototypeOf(p)) {
    var d = Object.getOwnPropertyDescriptor(p, propertyName);
    if (d)
      return d;
  }
  return null;
};

/**
 * Calls inherited property setter (useful when property is
 * overriden).
 * @param {Object} object The object.
 * @param {string} propertyName The property name.
 * @param {*} value Value to set.
 */
util.callInheritedSetter = function(object, propertyName, value) {
  var d = util.findPropertyDescriptor(Object.getPrototypeOf(object),
                                      propertyName);
  d.set.call(object, value);
};

/**
 * Returns true if the board of the device matches the given prefix.
 * @param {string} boardPrefix The board prefix to match against.
 *     (ex. "x86-mario". Prefix is used as the actual board name comes with
 *     suffix like "x86-mario-something".
 * @return {boolean} True if the board of the device matches the given prefix.
 */
util.boardIs = function(boardPrefix) {
  // The board name should be lower-cased, but making it case-insensitive for
  // backward compatibility just in case.
  var board = str('CHROMEOS_RELEASE_BOARD');
  var pattern = new RegExp('^' + boardPrefix, 'i');
  return board.match(pattern) != null;
};

/**
 * Adds an isFocused method to the current window object.
 */
util.addIsFocusedMethod = function() {
  var focused = true;

  window.addEventListener('focus', function() {
    focused = true;
  });

  window.addEventListener('blur', function() {
    focused = false;
  });

  /**
   * @return {boolean} True if focused.
   */
  window.isFocused = function() {
    return focused;
  };
};

/**
 * Makes a redirect to the specified Files.app's window from another window.
 * @param {number} id Window id.
 * @param {string} url Target url.
 * @return {boolean} True if the window has been found. False otherwise.
 */
util.redirectMainWindow = function(id, url) {
  // TODO(mtomasz): Implement this for Apps V2, once the photo importer is
  // restored.
  return false;
};

/**
 * Checks, if the Files.app's window is in a full screen mode.
 *
 * @param {AppWindow} appWindow App window to be maximized.
 * @return {boolean} True if the full screen mode is enabled.
 */
util.isFullScreen = function(appWindow) {
  if (appWindow) {
    return appWindow.isFullscreen();
  } else {
    console.error('App window not passed. Unable to check status of ' +
                  'the full screen mode.');
    return false;
  }
};

/**
 * Toggles the full screen mode.
 *
 * @param {AppWindow} appWindow App window to be maximized.
 * @param {boolean} enabled True for enabling, false for disabling.
 */
util.toggleFullScreen = function(appWindow, enabled) {
  if (appWindow) {
    if (enabled)
      appWindow.fullscreen();
    else
      appWindow.restore();
    return;
  }

  console.error(
      'App window not passed. Unable to toggle the full screen mode.');
};

/**
 * The type of a file operation error.
 * @enum {number}
 */
util.FileOperationErrorType = {
  UNEXPECTED_SOURCE_FILE: 0,
  TARGET_EXISTS: 1,
  FILESYSTEM_ERROR: 2,
};

/**
 * The type of an entry changed event.
 * @enum {number}
 */
util.EntryChangedType = {
  CREATED: 0,
  DELETED: 1,
};

/**
 * @param {DirectoryEntry|Object} entry DirectoryEntry to be checked.
 * @return {boolean} True if the given entry is fake.
 */
util.isFakeDirectoryEntry = function(entry) {
  // Currently, fake entry doesn't support createReader.
  return !('createReader' in entry);
};

/**
 * Creates a FileError instance with given code.
 * Note that we cannot create FileError instance by "new FileError(code)",
 * unfortunately, so here we use Object.create.
 * @param {number} code Error code for the FileError.
 * @return {FileError} FileError instance
 */
util.createFileError = function(code) {
  return Object.create(FileError.prototype, {
    code: { get: function() { return code; } }
  });
};

/**
 * @param {Entry|Object} entry1 The entry to be compared. Can be a fake.
 * @param {Entry|Object} entry2 The entry to be compared. Can be a fake.
 * @return {boolean} True if the both entry represents a same file or directory.
 */
util.isSameEntry = function(entry1, entry2) {
  // Currently, we can assume there is only one root.
  // When we support multi-file system, we need to look at filesystem, too.
  return entry1.fullPath == entry2.fullPath;
};

/**
 * @param {Entry|Object} parent The parent entry. Can be a fake.
 * @param {Entry|Object} child The child entry. Can be a fake.
 * @return {boolean} True if parent entry is actualy the parent of the child
 *     entry.
 */
util.isParentEntry = function(parent, child) {
  // Currently, we can assume there is only one root.
  // When we support multi-file system, we need to look at filesystem, too.
  return PathUtil.isParentPath(parent.fullPath, child.fullPath);
};
