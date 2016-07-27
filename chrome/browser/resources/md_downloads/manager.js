// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('downloads', function() {
  var Manager = Polymer({
    is: 'downloads-manager',

    properties: {
      hasDownloads_: {
        observer: 'hasDownloadsChanged_',
        type: Boolean,
      },

      items_: {
        type: Array,
        value: function() { return []; },
      },
    },

    hostAttributes: {
      loading: true,
    },

    observers: [
      'itemsChanged_(items_.*)',
    ],

    /** @private */
    clearAll_: function() {
      this.set('items_', []);
    },

    /** @private */
    hasDownloadsChanged_: function() {
      if (loadTimeData.getBoolean('allowDeletingHistory'))
        this.$.toolbar.downloadsShowing = this.hasDownloads_;

      if (this.hasDownloads_) {
        this.$['downloads-list'].fire('iron-resize');
      } else {
        var isSearching = downloads.ActionService.getInstance().isSearching();
        var messageToShow = isSearching ? 'noSearchResults' : 'noDownloads';
        this.$['no-downloads'].querySelector('span').textContent =
            loadTimeData.getString(messageToShow);
      }
    },

    /**
     * @param {number} index
     * @param {!Array<!downloads.Data>} list
     * @private
     */
    insertItems_: function(index, list) {
      this.splice.apply(this, ['items_', index, 0].concat(list));
      this.updateHideDates_(index, index + list.length);
      this.removeAttribute('loading');
    },

    /** @private */
    itemsChanged_: function() {
      this.hasDownloads_ = this.items_.length > 0;
    },

    /**
     * @param {Event} e
     * @private
     */
    onCanExecute_: function(e) {
      e = /** @type {cr.ui.CanExecuteEvent} */(e);
      switch (e.command.id) {
        case 'undo-command':
          e.canExecute = this.$.toolbar.canUndo();
          break;
        case 'clear-all-command':
          e.canExecute = this.$.toolbar.canClearAll();
          break;
      }
    },

    /**
     * @param {Event} e
     * @private
     */
    onCommand_: function(e) {
      if (e.command.id == 'clear-all-command')
        downloads.ActionService.getInstance().clearAll();
      else if (e.command.id == 'undo-command')
        downloads.ActionService.getInstance().undo();
    },

    /** @private */
    onLoad_: function() {
      cr.ui.decorate('command', cr.ui.Command);
      document.addEventListener('canExecute', this.onCanExecute_.bind(this));
      document.addEventListener('command', this.onCommand_.bind(this));

      // Shows all downloads.
      downloads.ActionService.getInstance().search('');
    },

    /**
     * @param {number} index
     * @private
     */
    removeItem_: function(index) {
      this.splice('items_', index, 1);
      this.updateHideDates_(index, index);
    },

    /**
     * @param {number} start
     * @param {number} end
     * @private
     */
    updateHideDates_: function(start, end) {
      for (var i = start; i <= end; ++i) {
        var current = this.items_[i];
        if (!current)
          continue;
        var prev = this.items_[i - 1];
        current.hideDate = !!prev && prev.date_string == current.date_string;
      }
    },

    /**
     * @param {number} index
     * @param {!downloads.Data} data
     * @private
     */
    updateItem_: function(index, data) {
      this.set('items_.' + index, data);
      this.updateHideDates_(index, index);
      this.$['downloads-list'].updateSizeForItem(index);
    },
  });

  Manager.clearAll = function() {
    Manager.get().clearAll_();
  };

  /** @return {!downloads.Manager} */
  Manager.get = function() {
    return /** @type {!downloads.Manager} */(
        queryRequiredElement('downloads-manager'));
  };

  Manager.insertItems = function(index, list) {
    Manager.get().insertItems_(index, list);
  };

  Manager.onLoad = function() {
    Manager.get().onLoad_();
  };

  Manager.removeItem = function(index) {
    Manager.get().removeItem_(index);
  };

  Manager.updateItem = function(index, data) {
    Manager.get().updateItem_(index, data);
  };

  return {Manager: Manager};
});
