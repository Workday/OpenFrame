// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

/**
 * This object encapsulates everything related to tasks execution.
 *
 * @param {FileManager} fileManager FileManager instance.
 * @param {Object=} opt_params File manager load parameters.
 * @constructor
 */
function FileTasks(fileManager, opt_params) {
  this.fileManager_ = fileManager;
  this.params_ = opt_params;
  this.tasks_ = null;
  this.defaultTask_ = null;

  /**
   * List of invocations to be called once tasks are available.
   *
   * @private
   * @type {Array.<Object>}
   */
  this.pendingInvocations_ = [];
}

/**
 * Location of the FAQ about the file actions.
 *
 * @const
 * @type {string}
 */
FileTasks.NO_ACTION_FOR_FILE_URL = 'http://support.google.com/chromeos/bin/' +
    'answer.py?answer=1700055&topic=29026&ctx=topic';

/**
 * Base URL of apps list in the Chrome Web Store. This constant is used in
 * FileTasks.createWebStoreLink().
 * @const
 * @type {string}
 */
FileTasks.WEB_STORE_HANDLER_BASE_URL =
    'https://chrome.google.com/webstore/category/collection/file_handlers';

/**
 * Returns URL of the Chrome Web Store which show apps supporting the given
 * file-extension and mime-type.
 *
 * @param {string} extension Extension of the file.
 * @param {string} mimeType Mime type of the file.
 * @return {string} URL
 */
FileTasks.createWebStoreLink = function(extension, mimeType) {
  var url = FileTasks.WEB_STORE_HANDLER_BASE_URL;
  url += '?_fe=' + extension.toLowerCase().replace(/[^\w]/g, '');
  if (mimeType)
    url += '&_fmt=' + mimeType.replace(/[^-\w\/]/g, '');
  return url;
};

/**
 * Complete the initialization.
 *
 * @param {Array.<string>} urls List of file urls.
 * @param {Array.<string>=} opt_mimeTypes List of MIME types for each
 *     of the files.
 */
FileTasks.prototype.init = function(urls, opt_mimeTypes) {
  this.urls_ = urls;
  if (urls.length > 0)
    chrome.fileBrowserPrivate.getFileTasks(urls, opt_mimeTypes || [],
      this.onTasks_.bind(this));
};

/**
 * Returns amount of tasks.
 *
 * @return {number} amount of tasks.
 */
FileTasks.prototype.size = function() {
  return (this.tasks_ && this.tasks_.length) || 0;
};

/**
 * Callback when tasks found.
 *
 * @param {Array.<Object>} tasks The tasks.
 * @private
 */
FileTasks.prototype.onTasks_ = function(tasks) {
  this.processTasks_(tasks);
  for (var index = 0; index < this.pendingInvocations_.length; index++) {
    var name = this.pendingInvocations_[index][0];
    var args = this.pendingInvocations_[index][1];
    this[name].apply(this, args);
  }
  this.pendingInvocations_ = [];
};

/**
 * The list of known extensions to record UMA.
 * Note: Because the data is recorded by the index, so new item shouldn't be
 * inserted.
 *
 * @const
 * @type {Array.<string>}
 * @private
 */
FileTasks.knownExtensions_ = [
  'other', '.3ga', '.3gp', '.aac', '.alac', '.asf', '.avi', '.bmp', '.csv',
  '.doc', '.docx', '.flac', '.gif', '.jpeg', '.jpg', '.log', '.m3u', '.m3u8',
  '.m4a', '.m4v', '.mid', '.mkv', '.mov', '.mp3', '.mp4', '.mpg', '.odf',
  '.odp', '.ods', '.odt', '.oga', '.ogg', '.ogv', '.pdf', '.png', '.ppt',
  '.pptx', '.ra', '.ram', '.rar', '.rm', '.rtf', '.wav', '.webm', '.webp',
  '.wma', '.wmv', '.xls', '.xlsx',
];

/**
 * Records trial of opening file grouped by extensions.
 *
 * @param {Array.<string>} urls The path to be opened.
 * @private
 */
FileTasks.recordViewingFileTypeUMA_ = function(urls) {
  for (var i = 0; i < urls.length; i++) {
    var url = urls[i];
    var extension = FileType.getExtension(url).toLowerCase();
    if (FileTasks.knownExtensions_.indexOf(extension) < 0) {
      extension = 'other';
    }
    metrics.recordEnum(
        'ViewingFileType', extension, FileTasks.knownExtensions_);
  }
};

/**
 * Processes internal tasks.
 *
 * @param {Array.<Object>} tasks The tasks.
 * @private
 */
FileTasks.prototype.processTasks_ = function(tasks) {
  this.tasks_ = [];
  var id = chrome.runtime.id;
  var isOnDrive = false;
  for (var index = 0; index < this.urls_.length; ++index) {
    if (FileType.isOnDrive(this.urls_[index])) {
      isOnDrive = true;
      break;
    }
  }

  for (var i = 0; i < tasks.length; i++) {
    var task = tasks[i];
    var taskParts = task.taskId.split('|');

    // Skip Drive App if the file is not on Drive.
    if (!isOnDrive && task.driveApp)
      continue;

    // Skip internal Files.app's handlers.
    if (taskParts[0] == id && (taskParts[2] == 'auto-open' ||
        taskParts[2] == 'select' || taskParts[2] == 'open')) {
      continue;
    }

    // Tweak images, titles of internal tasks.
    if (taskParts[0] == id && taskParts[1] == 'file') {
      if (taskParts[2] == 'play') {
        // TODO(serya): This hack needed until task.iconUrl is working
        //             (see GetFileTasksFileBrowserFunction::RunImpl).
        task.iconType = 'audio';
        task.title = loadTimeData.getString('ACTION_LISTEN');
      } else if (taskParts[2] == 'mount-archive') {
        task.iconType = 'archive';
        task.title = loadTimeData.getString('MOUNT_ARCHIVE');
      } else if (taskParts[2] == 'gallery') {
        task.iconType = 'image';
        task.title = loadTimeData.getString('ACTION_OPEN');
      } else if (taskParts[2] == 'watch') {
        task.iconType = 'video';
        task.title = loadTimeData.getString('ACTION_WATCH');
      } else if (taskParts[2] == 'open-hosted-generic') {
        if (this.urls_.length > 1)
          task.iconType = 'generic';
        else // Use specific icon.
          task.iconType = FileType.getIcon(this.urls_[0]);
        task.title = loadTimeData.getString('ACTION_OPEN');
      } else if (taskParts[2] == 'open-hosted-gdoc') {
        task.iconType = 'gdoc';
        task.title = loadTimeData.getString('ACTION_OPEN_GDOC');
      } else if (taskParts[2] == 'open-hosted-gsheet') {
        task.iconType = 'gsheet';
        task.title = loadTimeData.getString('ACTION_OPEN_GSHEET');
      } else if (taskParts[2] == 'open-hosted-gslides') {
        task.iconType = 'gslides';
        task.title = loadTimeData.getString('ACTION_OPEN_GSLIDES');
      } else if (taskParts[2] == 'view-swf') {
        // Do not render this task if disabled.
        if (!loadTimeData.getBoolean('SWF_VIEW_ENABLED'))
          continue;
        task.iconType = 'generic';
        task.title = loadTimeData.getString('ACTION_VIEW');
      } else if (taskParts[2] == 'view-pdf') {
        // Do not render this task if disabled.
        if (!loadTimeData.getBoolean('PDF_VIEW_ENABLED'))
          continue;
        task.iconType = 'pdf';
        task.title = loadTimeData.getString('ACTION_VIEW');
      } else if (taskParts[2] == 'view-in-browser') {
        task.iconType = 'generic';
        task.title = loadTimeData.getString('ACTION_VIEW');
      } else if (taskParts[2] == 'install-crx') {
        task.iconType = 'generic';
        task.title = loadTimeData.getString('INSTALL_CRX');
      }
    }

    if (!task.iconType && taskParts[1] == 'web-intent') {
      task.iconType = 'generic';
    }

    this.tasks_.push(task);
    if (this.defaultTask_ == null && task.isDefault) {
      this.defaultTask_ = task;
    }
  }
  if (!this.defaultTask_ && this.tasks_.length > 0) {
    // If we haven't picked a default task yet, then just pick the first one.
    // This is not the preferred way we want to pick this, but better this than
    // no default at all if the C++ code didn't set one.
    this.defaultTask_ = this.tasks_[0];
  }
};

/**
 * Executes default task.
 *
 * @private
 */
FileTasks.prototype.executeDefault_ = function() {
  var urls = this.urls_;
  FileTasks.recordViewingFileTypeUMA_(urls);
  this.executeDefaultInternal_(urls);
};

/**
 * Executes default task.
 *
 * @param {Array.<string>} urls Urls to execute.
 * @private
 */
FileTasks.prototype.executeDefaultInternal_ = function(urls) {
  if (this.defaultTask_ != null) {
    this.executeInternal_(this.defaultTask_.taskId, urls);
    return;
  }

  // We don't have tasks, so try to show a file in a browser tab.
  // We only do that for single selection to avoid confusion.
  if (urls.length == 1) {
    var callback = function(success) {
      if (!success) {
        var filename = decodeURIComponent(urls[0]);
        if (filename.indexOf('/') != -1)
          filename = filename.substr(filename.lastIndexOf('/') + 1);
        var extension = filename.lastIndexOf('.') != -1 ?
            filename.substr(filename.lastIndexOf('.') + 1) : '';

        this.fileManager_.metadataCache_.get(urls, 'drive', function(props) {
          var mimeType;
          if (props && props[0] && props[0].contentMimeType)
            mimeType = props[0].contentMimeType;

          var messageString = extension == 'exe' ? 'NO_ACTION_FOR_EXECUTABLE' :
                                                   'NO_ACTION_FOR_FILE';
          var webStoreUrl = FileTasks.createWebStoreLink(extension, mimeType);
          var text = loadTimeData.getStringF(messageString,
                                             webStoreUrl,
                                             FileTasks.NO_ACTION_FOR_FILE_URL);
          this.fileManager_.alert.showHtml(filename, text, function() {});
        }.bind(this));
      }
    }.bind(this);

    this.checkAvailability_(function() {
      chrome.fileBrowserPrivate.viewFiles(urls, callback);
    }.bind(this));
  }

  // Do nothing for multiple urls.
};

/**
 * Executes a single task.
 *
 * @param {string} taskId Task identifier.
 * @param {Array.<string>=} opt_urls Urls to execute on instead of |this.urls_|.
 * @private
 */
FileTasks.prototype.execute_ = function(taskId, opt_urls) {
  var urls = opt_urls || this.urls_;
  FileTasks.recordViewingFileTypeUMA_(urls);
  this.executeInternal_(taskId, urls);
};

/**
 * The core implementation to execute a single task.
 *
 * @param {string} taskId Task identifier.
 * @param {Array.<string>} urls Urls to execute.
 * @private
 */
FileTasks.prototype.executeInternal_ = function(taskId, urls) {
  this.checkAvailability_(function() {
    var taskParts = taskId.split('|');
    if (taskParts[0] == chrome.runtime.id && taskParts[1] == 'file') {
      // For internal tasks we do not listen to the event to avoid
      // handling the same task instance from multiple tabs.
      // So, we manually execute the task.
      this.executeInternalTask_(taskParts[2], urls);
    } else {
      chrome.fileBrowserPrivate.executeTask(taskId, urls);
    }
  }.bind(this));
};

/**
 * Checks whether the remote files are available right now.
 *
 * @param {function} callback The callback.
 * @private
 */
FileTasks.prototype.checkAvailability_ = function(callback) {
  var areAll = function(props, name) {
    var isOne = function(e) {
      // If got no properties, we safely assume that item is unavailable.
      return e && e[name];
    };
    return props.filter(isOne).length == props.length;
  };

  var fm = this.fileManager_;
  var urls = this.urls_;

  if (fm.isOnDrive() && fm.isDriveOffline()) {
    fm.metadataCache_.get(urls, 'drive', function(props) {
      if (areAll(props, 'availableOffline')) {
        callback();
        return;
      }

      fm.alert.showHtml(
          loadTimeData.getString('OFFLINE_HEADER'),
          props[0].hosted ?
            loadTimeData.getStringF(
                urls.length == 1 ?
                    'HOSTED_OFFLINE_MESSAGE' :
                    'HOSTED_OFFLINE_MESSAGE_PLURAL') :
            loadTimeData.getStringF(
                urls.length == 1 ?
                    'OFFLINE_MESSAGE' :
                    'OFFLINE_MESSAGE_PLURAL',
                loadTimeData.getString('OFFLINE_COLUMN_LABEL')));
    });
    return;
  }

  if (fm.isOnDrive() && fm.isDriveOnMeteredConnection()) {
    fm.metadataCache_.get(urls, 'drive', function(driveProps) {
      if (areAll(driveProps, 'availableWhenMetered')) {
        callback();
        return;
      }

      fm.metadataCache_.get(urls, 'filesystem', function(fileProps) {
        var sizeToDownload = 0;
        for (var i = 0; i != urls.length; i++) {
          if (!driveProps[i].availableWhenMetered)
            sizeToDownload += fileProps[i].size;
        }
        fm.confirm.show(
            loadTimeData.getStringF(
                urls.length == 1 ?
                    'CONFIRM_MOBILE_DATA_USE' :
                    'CONFIRM_MOBILE_DATA_USE_PLURAL',
                util.bytesToString(sizeToDownload)),
            callback);
      });
    });
    return;
  }

  callback();
};

/**
 * Executes an internal task.
 *
 * @param {string} id The short task id.
 * @param {Array.<string>} urls The urls to execute on.
 * @private
 */
FileTasks.prototype.executeInternalTask_ = function(id, urls) {
  var fm = this.fileManager_;

  if (id == 'play') {
    var position = 0;
    if (urls.length == 1) {
      // If just a single audio file is selected pass along every audio file
      // in the directory.
      var selectedUrl = urls[0];
      urls = fm.getAllUrlsInCurrentDirectory().filter(FileType.isAudio);
      position = urls.indexOf(selectedUrl);
    }
    chrome.runtime.getBackgroundPage(function(background) {
      background.launchAudioPlayer({ items: urls, position: position });
    });
    return;
  }

  if (id == 'watch') {
    console.assert(urls.length == 1, 'Cannot open multiple videos');
    chrome.runtime.getBackgroundPage(function(background) {
      background.launchVideoPlayer(urls[0]);
    });
    return;
  }

  if (id == 'mount-archive') {
    this.mountArchivesInternal_(urls);
    return;
  }

  if (id == 'format-device') {
    fm.confirm.show(loadTimeData.getString('FORMATTING_WARNING'), function() {
      chrome.fileBrowserPrivate.formatDevice(urls[0]);
    });
    return;
  }

  if (id == 'gallery') {
    this.openGalleryInternal_(urls);
    return;
  }

  if (id == 'view-pdf' || id == 'view-swf' || id == 'view-in-browser' ||
      id == 'install-crx' || id.match(/^open-hosted-/) || id == 'watch') {
    chrome.fileBrowserPrivate.viewFiles(urls, function(success) {
      if (!success)
        console.error('chrome.fileBrowserPrivate.viewFiles failed', urls);
    });
  }
};

/**
 * Mounts archives.
 *
 * @param {Array.<string>} urls Mount file urls list.
 */
FileTasks.prototype.mountArchives = function(urls) {
  FileTasks.recordViewingFileTypeUMA_(urls);
  this.mountArchivesInternal_(urls);
};

/**
 * The core implementation of mounts archives.
 *
 * @param {Array.<string>} urls Mount file urls list.
 * @private
 */
FileTasks.prototype.mountArchivesInternal_ = function(urls) {
  var fm = this.fileManager_;

  var tracker = fm.directoryModel_.createDirectoryChangeTracker();
  tracker.start();

  fm.resolveSelectResults_(urls, function(urls) {
    for (var index = 0; index < urls.length; ++index) {
      fm.volumeManager_.mountArchive(urls[index], function(mountPath) {
        tracker.stop();
        if (!tracker.hasChanged)
          fm.directoryModel_.changeDirectory(mountPath);
      }, function(url, error) {
        var path = util.extractFilePath(url);
        tracker.stop();
        var namePos = path.lastIndexOf('/');
        fm.alert.show(strf('ARCHIVE_MOUNT_FAILED',
                           path.substr(namePos + 1), error));
      }.bind(null, urls[index]));
    }
  });
};

/**
 * Open the Gallery.
 *
 * @param {Array.<string>} urls List of selected urls.
 */
FileTasks.prototype.openGallery = function(urls) {
  FileTasks.recordViewingFileTypeUMA_(urls);
  this.openGalleryInternal_(urls);
};

/**
 * The core implementation to open the Gallery.
 *
 * @param {Array.<string>} urls List of selected urls.
 * @private
 */
FileTasks.prototype.openGalleryInternal_ = function(urls) {
  var fm = this.fileManager_;

  var allUrls =
      fm.getAllUrlsInCurrentDirectory().filter(FileType.isImageOrVideo);

  var galleryFrame = fm.document_.createElement('iframe');
  galleryFrame.className = 'overlay-pane';
  galleryFrame.scrolling = 'no';
  galleryFrame.setAttribute('webkitallowfullscreen', true);

  if (this.params_ && this.params_.gallery) {
    // Remove the Gallery state from the location, we do not need it any more.
    util.updateAppState(null /* keep path */, '' /* remove search. */);
  }

  var savedAppState = window.appState;
  var savedTitle = document.title;

  // Push a temporary state which will be replaced every time the selection
  // changes in the Gallery and popped when the Gallery is closed.
  util.updateAppState();

  var onBack = function(selectedUrls) {
    fm.directoryModel_.selectUrls(selectedUrls);
    fm.closeFilePopup_();  // Will call Gallery.unload.
    window.appState = savedAppState;
    util.saveAppState();
    document.title = savedTitle;
  };

  var onClose = function() {
    fm.onClose();
  };

  var onMaximize = function() {
    fm.onMaximize();
  };

  galleryFrame.onload = function() {
    galleryFrame.contentWindow.ImageUtil.metrics = metrics;
    window.galleryTestAPI = galleryFrame.contentWindow.galleryTestAPI;

    // TODO(haruki): isOnReadonlyDirectory() only checks the permission for the
    // root. We should check more granular permission to know whether the file
    // is writable or not.
    var readonly = fm.isOnReadonlyDirectory();
    var currentDir = fm.directoryModel_.getCurrentDirEntry();
    var downloadsDir = fm.directoryModel_.getRootsList().item(0);
    var readonlyDirName = null;
    if (readonly) {
      readonlyDirName = fm.isOnDrive() ?
          PathUtil.getRootLabel(PathUtil.getRootPath(currentDir.fullPath)) :
          fm.directoryModel_.getCurrentRootName();
    }

    var context = {
      // We show the root label in readonly warning (e.g. archive name).
      readonlyDirName: readonlyDirName,
      curDirEntry: currentDir,
      saveDirEntry: readonly ? downloadsDir : null,
      searchResults: fm.directoryModel_.isSearching(),
      metadataCache: fm.metadataCache_,
      pageState: this.params_,
      appWindow: chrome.app.window.current(),
      onBack: onBack,
      onClose: onClose,
      onMaximize: onMaximize,
      displayStringFunction: strf
    };
    galleryFrame.contentWindow.Gallery.open(context, allUrls, urls);
  }.bind(this);

  galleryFrame.src = 'gallery.html';
  fm.openFilePopup_(galleryFrame, fm.updateTitle_.bind(fm));
};

/**
 * Displays the list of tasks in a task picker combobutton.
 *
 * @param {cr.ui.ComboButton} combobutton The task picker element.
 * @private
 */
FileTasks.prototype.display_ = function(combobutton) {
  if (this.tasks_.length == 0) {
    combobutton.hidden = true;
    return;
  }

  combobutton.clear();
  combobutton.hidden = false;
  combobutton.defaultItem = this.createCombobuttonItem_(this.defaultTask_);

  var items = this.createItems_();

  if (items.length > 1) {
    var defaultIdx = 0;

    for (var j = 0; j < items.length; j++) {
      combobutton.addDropDownItem(items[j]);
      if (items[j].task.taskId == this.defaultTask_.taskId)
        defaultIdx = j;
    }

    combobutton.addSeparator();
    var changeDefaultMenuItem = combobutton.addDropDownItem({
        label: loadTimeData.getString('CHANGE_DEFAULT_MENU_ITEM')
    });
    changeDefaultMenuItem.classList.add('change-default');
  }
};

/**
 * Creates sorted array of available task descriptions such as title and icon.
 *
 * @return {Array} created array can be used to feed combobox, menus and so on.
 * @private
 */
FileTasks.prototype.createItems_ = function() {
  var items = [];
  var title = this.defaultTask_.title + ' ' +
              loadTimeData.getString('DEFAULT_ACTION_LABEL');
  items.push(this.createCombobuttonItem_(this.defaultTask_, title, true));

  for (var index = 0; index < this.tasks_.length; index++) {
    var task = this.tasks_[index];
    if (task != this.defaultTask_)
      items.push(this.createCombobuttonItem_(task));
  }

  items.sort(function(a, b) {
    return a.label.localeCompare(b.label);
  });

  return items;
};

/**
 * Updates context menu with default item.
 * @private
 */

FileTasks.prototype.updateMenuItem_ = function() {
  this.fileManager_.updateContextMenuActionItems(this.defaultTask_,
      this.tasks_.length > 1);
};

/**
 * Creates combobutton item based on task.
 *
 * @param {Object} task Task to convert.
 * @param {string=} opt_title Title.
 * @param {boolean=} opt_bold Make a menu item bold.
 * @return {Object} Item appendable to combobutton drop-down list.
 * @private
 */
FileTasks.prototype.createCombobuttonItem_ = function(task, opt_title,
                                                      opt_bold) {
  return {
    label: opt_title || task.title,
    iconUrl: task.iconUrl,
    iconType: task.iconType,
    task: task,
    bold: opt_bold || false
  };
};


/**
 * Decorates a FileTasks method, so it will be actually executed after the tasks
 * are available.
 * This decorator expects an implementation called |method + '_'|.
 *
 * @param {string} method The method name.
 */
FileTasks.decorate = function(method) {
  var privateMethod = method + '_';
  FileTasks.prototype[method] = function() {
    if (this.tasks_) {
      this[privateMethod].apply(this, arguments);
    } else {
      this.pendingInvocations_.push([privateMethod, arguments]);
    }
    return this;
  };
};

/**
 * Shows modal action picker dialog with currently available list of tasks.
 *
 * @param {DefaultActionDialog} actionDialog Action dialog to show and update.
 * @param {string} title Title to use.
 * @param {string} message Message to use.
 * @param {function(Object)} onSuccess Callback to pass selected task.
 */
FileTasks.prototype.showTaskPicker = function(actionDialog, title, message,
                                              onSuccess) {
  var items = this.createItems_();

  var defaultIdx = 0;
  for (var j = 0; j < items.length; j++) {
    if (items[j].task.taskId == this.defaultTask_.taskId)
      defaultIdx = j;
  }

  actionDialog.show(
      title,
      message,
      items, defaultIdx,
      function(item) {
        onSuccess(item.task);
      });
};

FileTasks.decorate('display');
FileTasks.decorate('updateMenuItem');
FileTasks.decorate('execute');
FileTasks.decorate('executeDefault');
