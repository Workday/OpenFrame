// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

/**
 * Butter bar is shown on top of the file list and is used to show the copy
 * progress and other messages.
 * @param {HTMLElement} dialogDom FileManager top-level div.
 * @param {FileCopyManagerWrapper} copyManager The copy manager.
 * @constructor
 */
function ButterBar(dialogDom, copyManager) {
  this.dialogDom_ = dialogDom;
  this.butter_ = this.dialogDom_.querySelector('#butter-bar');
  this.document_ = this.butter_.ownerDocument;
  this.copyManager_ = copyManager;
  this.hideTimeout_ = null;
  this.showTimeout_ = null;
  this.lastShowTime_ = 0;
  this.deleteTaskId_ = null;
  this.currentMode_ = null;
  this.totalDeleted_ = 0;
  this.lastProgressValue_ = 0;
  this.alert_ = new ErrorDialog(this.dialogDom_);

  this.onCopyProgressBound_ = this.onCopyProgress_.bind(this);
  this.copyManager_.addEventListener(
      'copy-progress', this.onCopyProgressBound_);
  this.onDeleteBound_ = this.onDelete_.bind(this);
  this.copyManager_.addEventListener('delete', this.onDeleteBound_);
}

/**
 * The default amount of milliseconds time, before a butter bar will hide after
 * the last update.
 * @type {number}
 * @private
 * @const
 */
ButterBar.HIDE_DELAY_TIME_MS_ = 2000;

/**
 * Name of action which should be displayed as an 'x' button instead of
 * link with text.
 * @const
 */
ButterBar.ACTION_X = '--action--x--';

/**
 * Butter bar mode.
 * @const
 */
ButterBar.Mode = {
  COPY: 1,
  DELETE: 2
};

/**
 * Disposes the instance. No methods should be called after this method's
 * invocation.
 */
ButterBar.prototype.dispose = function() {
  // Unregister listeners from FileCopyManager.
  this.copyManager_.removeEventListener(
      'copy-progress', this.onCopyProgressBound_);
  this.copyManager_.removeEventListener('delete', this.onDeleteBound_);
};

/**
 * @return {boolean} True if visible.
 * @private
 */
ButterBar.prototype.isVisible_ = function() {
  return this.butter_.classList.contains('visible');
};

/**
 * Show butter bar.
 * @param {ButterBar.Mode} mode Butter bar mode.
 * @param {string} message The message to be shown.
 * @param {Object=} opt_options Options: 'actions', 'progress', 'timeout'. If
 *     'timeout' is not specified, HIDE_DELAY_TIME_MS_ is used. If 'timeout' is
 *     false, the butter bar will not be hidden.
 */
ButterBar.prototype.show = function(mode, message, opt_options) {
  this.currentMode_ = mode;

  this.clearShowTimeout_();
  this.clearHideTimeout_();

  var actions = this.butter_.querySelector('.actions');
  actions.textContent = '';
  if (opt_options && 'actions' in opt_options) {
    for (var label in opt_options.actions) {
      var link = this.document_.createElement('a');
      link.addEventListener('click', function(callback) {
        callback();
        return false;
      }.bind(null, opt_options.actions[label]));
      if (label == ButterBar.ACTION_X) {
        link.className = 'x';
      } else {
        link.textContent = label;
      }
      actions.appendChild(link);
    }
    actions.hidden = false;
  } else {
    actions.hidden = true;
  }

  this.butter_.querySelector('.progress-bar').hidden =
    !(opt_options && 'progress' in opt_options);

  this.butter_.classList.remove('error');
  this.butter_.classList.remove('visible');  // Will be shown in update_
  this.update_(message, opt_options);
};

/**
 * Show an error message in a popup dialog.
 * @param {string} message Message.
 * @private
 */
ButterBar.prototype.showError_ = function(message) {
  // Wait in case there are previous dialogs being closed.
  setTimeout(function() {
    this.alert_.showHtml('',  // Title.
                         message);
    this.hide_();
  }.bind(this), cr.ui.dialogs.BaseDialog.ANIMATE_STABLE_DURATION);
};

/**
 * Set message and/or progress.
 * @param {string} message Message.
 * @param {Object=} opt_options Same as in show().
 * @private
 */
ButterBar.prototype.update_ = function(message, opt_options) {
  if (!opt_options)
    opt_options = {};

  this.clearHideTimeout_();

  var butterMessage = this.butter_.querySelector('.butter-message');
   butterMessage.textContent = message;
  if (message && !this.isVisible_()) {
    // The butter bar is made visible on the first non-empty message.
    this.butter_.classList.add('visible');
  }
  if (opt_options && 'progress' in opt_options) {
    butterMessage.classList.add('single-line');
    var progressTrack = this.butter_.querySelector('.progress-track');
    // Smoothen the progress only when it goes forward. Especially do not
    // do the transition effect if resetting to 0.
    if (opt_options.progress > this.lastProgressValue_)
      progressTrack.classList.add('smoothed');
    else
      progressTrack.classList.remove('smoothed');
    progressTrack.style.width = (opt_options.progress * 100) + '%';
    this.lastProgressValue_ = opt_options.progress;
  } else {
    butterMessage.classList.remove('single-line');
  }

  if (opt_options.timeout !== false)
    this.hide_(opt_options.timeout);
};

/**
 * Hide butter bar. There might be the delay before hiding so that butter bar
 * would be shown for no less than the minimal time.
 * @param {number=} opt_timeout Delay time in milliseconds before hidding. If it
 *     is zero, butter bar is hidden immediatelly. If it is not specified,
 *     HIDE_DELAY_TIME_MS_ is used.
 * @private
 */
ButterBar.prototype.hide_ = function(opt_timeout) {
  this.clearHideTimeout_();

  if (!this.isVisible_())
    return;

  var delay = typeof opt_timeout != 'undefined' ?
    opt_timeout : ButterBar.HIDE_DELAY_TIME_MS_;
  if (delay <= 0) {
    this.currentMode_ = null;
    this.butter_.classList.remove('visible');
    this.butter_.querySelector('.progress-bar').hidden = true;
  } else {
    // Reschedule hide to comply with the minimal display time.
    this.hideTimeout_ = setTimeout(function() {
      this.hideTimeout_ = null;
      this.hide_(0);
    }.bind(this), delay);
  }
};

/**
 * Clear the show timeout if it is set.
 * @private
 */
ButterBar.prototype.clearShowTimeout_ = function() {
  if (this.showTimeout_) {
    clearTimeout(this.showTimeout_);
    this.showTimeout_ = null;
  }
};

/**
 * Clear the hide timeout if it is set.
 * @private
 */
ButterBar.prototype.clearHideTimeout_ = function() {
  if (this.hideTimeout_) {
    clearTimeout(this.hideTimeout_);
    this.hideTimeout_ = null;
  }
};

/**
 * @return {string?} The type of operation.
 * @private
 */
ButterBar.prototype.transferType_ = function() {
  var progress = this.progress_;
  if (!progress)
    return 'TRANSFER';

  var pendingTransferTypesCount =
      (progress.pendingMoves === 0 ? 0 : 1) +
      (progress.pendingCopies === 0 ? 0 : 1) +
      (progress.pendingZips === 0 ? 0 : 1);

  if (pendingTransferTypesCount != 1)
    return 'TRANSFER';
  else if (progress.pendingMoves > 0)
    return 'MOVE';
  else if (progress.pendingCopies > 0)
    return 'COPY';
  else
    return 'ZIP';
};

/**
 * Set up butter bar for showing copy progress.
 *
 * @param {Object} progress Copy status object created by
 *     FileCopyManager.getStatus().
 * @private
 */
ButterBar.prototype.showProgress_ = function(progress) {
  this.progress_ = progress;
  var options = {
    progress: progress.completedBytes / progress.totalBytes,
    actions: {},
    timeout: false
  };

  var pendingItems = progress.totalItems - progress.completedItems;
  var type = this.transferType_();
  var progressString = (pendingItems === 1) ?
          strf(type + '_FILE_NAME', progress.filename) :
          strf(type + '_ITEMS_REMAINING', pendingItems);

  if (this.currentMode_ == ButterBar.Mode.COPY) {
    this.update_(progressString, options);
  } else {
    options.actions[ButterBar.ACTION_X] =
        this.copyManager_.requestCancel.bind(this.copyManager_);
    this.show(ButterBar.Mode.COPY, progressString, options);
  }
};

/**
 * 'copy-progress' event handler. Show progress or an appropriate message.
 * @param {cr.Event} event A 'copy-progress' event from FileCopyManager.
 * @private
 */
ButterBar.prototype.onCopyProgress_ = function(event) {
  // Delete operation has higher priority.
  if (this.currentMode_ == ButterBar.Mode.DELETE)
    return;

  if (event.reason != 'PROGRESS')
    this.clearShowTimeout_();

  switch (event.reason) {
    case 'BEGIN':
      this.showTimeout_ = setTimeout(function() {
        this.showTimeout_ = null;
        this.showProgress_(event.status);
      }.bind(this), 500);
      break;

    case 'PROGRESS':
      this.showProgress_(event.status);
      break;

    case 'SUCCESS':
      this.hide_();
      break;

    case 'CANCELLED':
      this.show(ButterBar.Mode.DELETE,
                str(this.transferType_() + '_CANCELLED'));
      break;

    case 'ERROR':
      this.progress_ = event.status;
      var error = event.error;
      if (error.code === util.FileOperationErrorType.TARGET_EXISTS) {
        var name = error.data.name;
        if (error.data.isDirectory)
          name += '/';
        this.showError_(
            strf(this.transferType_() + '_TARGET_EXISTS_ERROR', name));
      } else if (error.code === util.FileOperationErrorType.FILESYSTEM_ERROR) {
        if (error.data.toDrive &&
            error.data.code === FileError.QUOTA_EXCEEDED_ERR) {
          // The alert will be shown in FileManager.onCopyProgress_.
          this.hide_();
        } else {
          this.showError_(strf(this.transferType_() + '_FILESYSTEM_ERROR',
                               util.getFileErrorString(error.data.code)));
          }
      } else {
        this.showError_(
            strf(this.transferType_() + '_UNEXPECTED_ERROR', error));
      }
      break;

    default:
      console.warn('Unknown "copy-progress" event reason: ' + event.code);
  }
};

/**
 * 'delete' event handler. Shows information about deleting files.
 * @param {cr.Event} event A 'delete' event from FileCopyManager.
 * @private
 */
ButterBar.prototype.onDelete_ = function(event) {
  switch (event.reason) {
    case 'BEGIN':
      if (this.currentMode_ != ButterBar.Mode.DELETE)
        this.totalDeleted_ = 0;

    case 'PROGRESS':
      this.totalDeleted_ += event.urls.length;
      var title = strf('DELETED_MESSAGE_PLURAL', this.totalDeleted_);
      if (this.totalDeleted_ == 1) {
        var fullPath = util.extractFilePath(event.urls[0]);
        var fileName = PathUtil.split(fullPath).pop();
        title = strf('DELETED_MESSAGE', fileName);
      }

      if (this.currentMode_ == ButterBar.Mode.DELETE)
        this.update_(title);
      else
        this.show(ButterBar.Mode.DELETE, title);
      break;

    case 'SUCCESS':
      break;

    case 'ERROR':
      this.showError_(str('DELETE_ERROR'));
      break;

    default:
      console.warn('Unknown "delete" event reason: ' + event.reason);
  }
};
