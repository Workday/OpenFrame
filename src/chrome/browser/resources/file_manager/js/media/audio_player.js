// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

/**
 * @param {HTMLElement} container Container element.
 * @constructor
 */
function AudioPlayer(container) {
  this.container_ = container;
  this.metadataCache_ = MetadataCache.createFull();
  this.currentTrack_ = -1;
  this.playlistGeneration_ = 0;
  this.volumeManager_ = VolumeManager.getInstance();

  this.container_.classList.add('collapsed');

  function createChild(opt_className, opt_tag) {
    var child = container.ownerDocument.createElement(opt_tag || 'div');
    if (opt_className)
      child.className = opt_className;
    container.appendChild(child);
    return child;
  }

  // We create two separate containers (for expanded and compact view) and keep
  // two sets of TrackInfo instances. We could fiddle with a single set instead
  // but it would make keeping the list scroll position very tricky.
  this.trackList_ = createChild('track-list');
  this.trackStack_ = createChild('track-stack');

  createChild('title-button collapse').addEventListener(
      'click', this.onExpandCollapse_.bind(this));

  this.audioControls_ = new FullWindowAudioControls(
      createChild(), this.advance_.bind(this), this.onError_.bind(this));

  this.audioControls_.attachMedia(createChild('', 'audio'));

  chrome.fileBrowserPrivate.getStrings(function(strings) {
    container.ownerDocument.title = strings['AUDIO_PLAYER_TITLE'];
    this.errorString_ = strings['AUDIO_ERROR'];
    this.offlineString_ = strings['AUDIO_OFFLINE'];
    AudioPlayer.TrackInfo.DEFAULT_ARTIST =
        strings['AUDIO_PLAYER_DEFAULT_ARTIST'];
  }.bind(this));

  this.volumeManager_.addEventListener('externally-unmounted',
      this.onExternallyUnmounted_.bind(this));
}

/**
 * Key in the local storage for the list of track urls.
 */
AudioPlayer.PLAYLIST_KEY = 'audioPlaylist';

/**
 * Key in the local storage for the number of the current track.
 */
AudioPlayer.TRACK_KEY = 'audioTrack';

/**
 * Initial load method (static).
 */
AudioPlayer.load = function() {
  document.ondragstart = function(e) { e.preventDefault() };

  // If the audio player is starting before the first instance of the File
  // Manager then it does not have access to filesystem URLs. Request it now.
  chrome.fileBrowserPrivate.requestFileSystem(function() {
    AudioPlayer.instance =
        new AudioPlayer(document.querySelector('.audio-player'));
    chrome.mediaPlayerPrivate.onPlaylistChanged.addListener(getPlaylist);
    reload();
  });
};

util.addPageLoadHandler(AudioPlayer.load);

/**
 * Unload the player.
 */
function unload() {
  AudioPlayer.instance.audioControls_.cleanup();
}

/**
 * Reload the player.
 */
function reload() {
  if (window.appState) {
    // Launching/reloading a v2 app.
    util.saveAppState();
    AudioPlayer.instance.load(window.appState);
    return;
  }

  // Lauching/reloading a v1 app.
  if (document.location.hash) {
    // The window is reloading, restore the state.
    AudioPlayer.instance.load(null);
  } else {
    getPlaylist();
  }
}

/**
 * Get the playlist from Chrome.
 */
function getPlaylist() {
  chrome.mediaPlayerPrivate.getPlaylist(
      AudioPlayer.instance.load.bind(AudioPlayer.instance));
}

/**
 * Load a new playlist.
 * @param {Playlist} playlist Playlist object passed via mediaPlayerPrivate.
 */
AudioPlayer.prototype.load = function(playlist) {
  if (!playlist || !playlist.items.length) {
    // playlist is null if the window is being reloaded.
    // playlist is empty if ChromeOS has restarted with the Audio Player open.
    // Restore the playlist from the local storage.
    util.platform.getPreferences(function(prefs) {
      try {
        var restoredPlaylist = {
          items: JSON.parse(prefs[AudioPlayer.PLAYLIST_KEY]),
          position: Number(prefs[AudioPlayer.TRACK_KEY]),
          time: true // Force restoring time from document.location.
        };
        if (restoredPlaylist.items.length)
          this.load(restoredPlaylist);
      } catch (ignore) {}
    }.bind(this));
    return;
  }

  if (!window.appState) {
    // Remember the playlist for the restart.
    // App v2 handles that in the background page.
    util.platform.setPreference(
        AudioPlayer.PLAYLIST_KEY, JSON.stringify(playlist.items));
    util.platform.setPreference(
        AudioPlayer.TRACK_KEY, playlist.position);
  }

  this.playlistGeneration_++;

  this.audioControls_.pause();

  this.currentTrack_ = -1;

  this.urls_ = playlist.items;

  this.invalidTracks_ = {};
  this.cancelAutoAdvance_();

  if (this.urls_.length <= 1)
    this.container_.classList.add('single-track');
  else
    this.container_.classList.remove('single-track');

  this.syncHeight_();

  this.trackList_.textContent = '';
  this.trackStack_.textContent = '';

  this.trackListItems_ = [];
  this.trackStackItems_ = [];

  if (this.urls_.length == 0)
    return;

  for (var i = 0; i != this.urls_.length; i++) {
    var url = this.urls_[i];
    var onClick = this.select_.bind(this, i, false /* no restore */);
    this.trackListItems_.push(
        new AudioPlayer.TrackInfo(this.trackList_, url, onClick));
    this.trackStackItems_.push(
        new AudioPlayer.TrackInfo(this.trackStack_, url, onClick));
  }

  this.select_(playlist.position, !!playlist.time);

  // This class will be removed if at least one track has art.
  this.container_.classList.add('noart');

  // Load the selected track metadata first, then load the rest.
  this.loadMetadata_(playlist.position);
  for (i = 0; i != this.urls_.length; i++) {
    if (i != playlist.position)
      this.loadMetadata_(i);
  }
};

/**
 * Load metadata for a track.
 * @param {number} track Track number.
 * @private
 */
AudioPlayer.prototype.loadMetadata_ = function(track) {
  this.fetchMetadata_(
      this.urls_[track], this.displayMetadata_.bind(this, track));
};

/**
 * Display track's metadata.
 * @param {number} track Track number.
 * @param {Object} metadata Metadata object.
 * @param {string=} opt_error Error message.
 * @private
 */
AudioPlayer.prototype.displayMetadata_ = function(track, metadata, opt_error) {
  this.trackListItems_[track].
      setMetadata(metadata, this.container_, opt_error);
  this.trackStackItems_[track].
      setMetadata(metadata, this.container_, opt_error);
};

/**
 * Closes audio player when a volume containing the selected item is unmounted.
 * @param {Event} event The unmount event.
 * @private
 */
AudioPlayer.prototype.onExternallyUnmounted_ = function(event) {
  if (!this.selectedItemFilesystemPath_)
    return;
  if (this.selectedItemFilesystemPath_.indexOf(event.mountPath) == 0)
    close();
};

/**
 * Select a new track to play.
 * @param {number} newTrack New track number.
 * @param {boolean=} opt_restoreState True if restoring the play state from URL.
 * @private
 */
AudioPlayer.prototype.select_ = function(newTrack, opt_restoreState) {
  if (this.currentTrack_ == newTrack) return;

  this.changeSelectionInList_(this.currentTrack_, newTrack);
  this.changeSelectionInStack_(this.currentTrack_, newTrack);

  this.currentTrack_ = newTrack;

  if (window.appState) {
    window.appState.position = this.currentTrack_;
    window.appState.time = 0;
    util.saveAppState();
  } else {
    util.platform.setPreference(AudioPlayer.TRACK_KEY, this.currentTrack_);
  }

  this.scrollToCurrent_(false);

  var currentTrack = this.currentTrack_;
  var url = this.urls_[currentTrack];
  this.fetchMetadata_(url, function(metadata) {
    if (this.currentTrack_ != currentTrack)
      return;
    var src = url;
    this.audioControls_.load(src, opt_restoreState);

    // Resolve real filesystem path of the current audio file.
    this.selectedItemFilesystemPath_ = null;
    webkitResolveLocalFileSystemURL(src,
      function(entry) {
        if (this.currentTrack_ != currentTrack)
          return;
        this.selectedItemFilesystemPath_ = entry.fullPath;
      }.bind(this));
  }.bind(this));
};

/**
 * @param {string} url Track file url.
 * @param {function(object)} callback Callback.
 * @private
 */
AudioPlayer.prototype.fetchMetadata_ = function(url, callback) {
  this.metadataCache_.get(url, 'thumbnail|media|streaming',
      function(generation, metadata) {
        // Do nothing if another load happened since the metadata request.
        if (this.playlistGeneration_ == generation)
          callback(metadata);
      }.bind(this, this.playlistGeneration_));
};

/**
 * @param {number} oldTrack Old track number.
 * @param {number} newTrack New track number.
 * @private
 */
AudioPlayer.prototype.changeSelectionInList_ = function(oldTrack, newTrack) {
  this.trackListItems_[newTrack].getBox().classList.add('selected');

  if (oldTrack >= 0) {
    this.trackListItems_[oldTrack].getBox().classList.remove('selected');
  }
};

/**
 * @param {number} oldTrack Old track number.
 * @param {number} newTrack New track number.
 * @private
 */
AudioPlayer.prototype.changeSelectionInStack_ = function(oldTrack, newTrack) {
  var newBox = this.trackStackItems_[newTrack].getBox();
  newBox.classList.add('selected');  // Put on top immediately.
  newBox.classList.add('visible');  // Start fading in.

  if (oldTrack >= 0) {
    var oldBox = this.trackStackItems_[oldTrack].getBox();
    oldBox.classList.remove('selected'); // Put under immediately.
    setTimeout(function() {
      if (!oldBox.classList.contains('selected')) {
        // This will start fading out which is not really necessary because
        // oldBox is already completely obscured by newBox.
        oldBox.classList.remove('visible');
      }
    }, 300);
  }
};

/**
 * Scrolls the current track into the viewport.
 *
 * @param {boolean} keepAtBottom If true, make the selected track the last
 *   of the visible (if possible). If false, perform minimal scrolling.
 * @private
 */
AudioPlayer.prototype.scrollToCurrent_ = function(keepAtBottom) {
  var box = this.trackListItems_[this.currentTrack_].getBox();
  this.trackList_.scrollTop = Math.max(
      keepAtBottom ? 0 : Math.min(box.offsetTop, this.trackList_.scrollTop),
      box.offsetTop + box.offsetHeight - this.trackList_.clientHeight);
};

/**
 * @return {boolean} True if the player is be displayed in compact mode.
 * @private
 */
AudioPlayer.prototype.isCompact_ = function() {
  return this.container_.classList.contains('collapsed') ||
         this.container_.classList.contains('single-track');
};

/**
 * Go to the previous or the next track.
 * @param {boolean} forward True if next, false if previous.
 * @param {boolean=} opt_onlyIfValid True if invalid tracks should be selected.
 * @private
 */
AudioPlayer.prototype.advance_ = function(forward, opt_onlyIfValid) {
  this.cancelAutoAdvance_();

  var newTrack = this.currentTrack_ + (forward ? 1 : -1);
  if (newTrack < 0) newTrack = this.urls_.length - 1;
  if (newTrack == this.urls_.length) newTrack = 0;
  if (opt_onlyIfValid && this.invalidTracks_[newTrack])
    return;
  this.select_(newTrack);
};

/**
 * Media error handler.
 * @private
 */
AudioPlayer.prototype.onError_ = function() {
  var track = this.currentTrack_;

  this.invalidTracks_[track] = true;

  this.fetchMetadata_(
      this.urls_[track],
      function(metadata) {
        var error = (!navigator.onLine && metadata.streaming) ?
            this.offlineString_ : this.errorString_;
        this.displayMetadata_(track, metadata, error);
        this.scheduleAutoAdvance_();
      }.bind(this));
};

/**
 * Schedule automatic advance to the next track after a timeout.
 * @private
 */
AudioPlayer.prototype.scheduleAutoAdvance_ = function() {
  this.cancelAutoAdvance_();
  this.autoAdvanceTimer_ = setTimeout(
      function() {
        this.autoAdvanceTimer_ = null;
        // We are advancing only if the next track is not known to be invalid.
        // This prevents an endless auto-advancing in the case when all tracks
        // are invalid (we will only visit each track once).
        this.advance_(true /* forward */, true /* only if valid */);
      }.bind(this),
      3000);
};

/**
 * Cancel the scheduled auto advance.
 * @private
 */
AudioPlayer.prototype.cancelAutoAdvance_ = function() {
  if (this.autoAdvanceTimer_) {
    clearTimeout(this.autoAdvanceTimer_);
    this.autoAdvanceTimer_ = null;
  }
};

/**
 * Expand/collapse button click handler.
 * @private
 */
AudioPlayer.prototype.onExpandCollapse_ = function() {
  this.container_.classList.toggle('collapsed');
  this.syncHeight_();
  if (!this.isCompact_())
    this.scrollToCurrent_(true);
};

/* Keep the below constants in sync with the CSS. */

/**
 * Player header height.
 * TODO(kaznacheev): Set to 30 when the audio player is title-less.
 */
AudioPlayer.HEADER_HEIGHT = 0;

/**
 * Track height.
 */
AudioPlayer.TRACK_HEIGHT = 58;

/**
 * Controls bar height.
 */
AudioPlayer.CONTROLS_HEIGHT = 35;

/**
 * Set the correct player window height.
 * @private
 */
AudioPlayer.prototype.syncHeight_ = function() {
  var expandedListHeight =
      Math.min(this.urls_.length, 3) * AudioPlayer.TRACK_HEIGHT;
  this.trackList_.style.height = expandedListHeight + 'px';

  var targetClientHeight = AudioPlayer.CONTROLS_HEIGHT +
      (this.isCompact_() ?
      AudioPlayer.TRACK_HEIGHT :
      AudioPlayer.HEADER_HEIGHT + expandedListHeight);

  var appWindow = chrome.app.window.current();
  var oldHeight = appWindow.contentWindow.outerHeight;
  var bottom = appWindow.contentWindow.screenY + oldHeight;
  var newTop = Math.max(0, bottom - targetClientHeight);
  appWindow.moveTo(appWindow.contentWindow.screenX, newTop);
  appWindow.resizeTo(appWindow.contentWindow.outerWidth,
      oldHeight + targetClientHeight - this.container_.clientHeight);
};


/**
 * Create a TrackInfo object encapsulating the information about one track.
 *
 * @param {HTMLElement} container Container element.
 * @param {string} url Track url.
 * @param {function} onClick Click handler.
 * @constructor
 */
AudioPlayer.TrackInfo = function(container, url, onClick) {
  this.url_ = url;

  var doc = container.ownerDocument;

  this.box_ = doc.createElement('div');
  this.box_.className = 'track';
  this.box_.addEventListener('click', onClick);
  container.appendChild(this.box_);

  this.art_ = doc.createElement('div');
  this.art_.className = 'art blank';
  this.box_.appendChild(this.art_);

  this.img_ = doc.createElement('img');
  this.art_.appendChild(this.img_);

  this.data_ = doc.createElement('div');
  this.data_.className = 'data';
  this.box_.appendChild(this.data_);

  this.title_ = doc.createElement('div');
  this.title_.className = 'data-title';
  this.data_.appendChild(this.title_);

  this.artist_ = doc.createElement('div');
  this.artist_.className = 'data-artist';
  this.data_.appendChild(this.artist_);
};

/**
 * @return {HTMLDivElement} The wrapper element for the track.
 */
AudioPlayer.TrackInfo.prototype.getBox = function() { return this.box_ };

/**
 * @return {string} Default track title (file name extracted from the url).
 */
AudioPlayer.TrackInfo.prototype.getDefaultTitle = function() {
  var title = this.url_.split('/').pop();
  var dotIndex = title.lastIndexOf('.');
  if (dotIndex >= 0) title = title.substr(0, dotIndex);
  title = decodeURIComponent(title);
  return title;
};

/**
 * TODO(kaznacheev): Localize.
 */
AudioPlayer.TrackInfo.DEFAULT_ARTIST = 'Unknown Artist';

/**
 * @return {string} 'Unknown artist' string.
 */
AudioPlayer.TrackInfo.prototype.getDefaultArtist = function() {
  return AudioPlayer.TrackInfo.DEFAULT_ARTIST;
};

/**
 * @param {Object} metadata The metadata object.
 * @param {HTMLElement} container The container for the tracks.
 * @param {string} error Error string.
 */
AudioPlayer.TrackInfo.prototype.setMetadata = function(
    metadata, container, error) {
  if (error) {
    this.art_.classList.add('blank');
    this.art_.classList.add('error');
    container.classList.remove('noart');
  } else if (metadata.thumbnail && metadata.thumbnail.url) {
    this.img_.onload = function() {
      // Only display the image if the thumbnail loaded successfully.
      this.art_.classList.remove('blank');
      container.classList.remove('noart');
    }.bind(this);
    this.img_.src = metadata.thumbnail.url;
  }
  this.title_.textContent = (metadata.media && metadata.media.title) ||
      this.getDefaultTitle();
  this.artist_.textContent = error ||
      (metadata.media && metadata.media.artist) || this.getDefaultArtist();
};

/**
 * Audio controls specific for the Audio Player.
 *
 * @param {HTMLElement} container Parent container.
 * @param {function(boolean)} advanceTrack Parameter: true=forward.
 * @param {function} onError Error handler.
 * @constructor
 */
function FullWindowAudioControls(container, advanceTrack, onError) {
  AudioControls.apply(this, arguments);

  document.addEventListener('keydown', function(e) {
    if (e.keyIdentifier == 'U+0020') {
      this.togglePlayState();
      e.preventDefault();
    }
  }.bind(this));
}

FullWindowAudioControls.prototype = { __proto__: AudioControls.prototype };

/**
 * Enable play state restore from the location hash.
 * @param {string} src Source URL.
 * @param {boolean} restore True if need to restore the play state.
 */
FullWindowAudioControls.prototype.load = function(src, restore) {
  this.media_.src = src;
  this.media_.load();
  this.restoreWhenLoaded_ = restore;
};

/**
 * Save the current state so that it survives page/app reload.
 */
FullWindowAudioControls.prototype.onPlayStateChanged = function() {
  this.encodeState();
};

/**
 * Restore the state after page/app reload.
 */
FullWindowAudioControls.prototype.restorePlayState = function() {
  if (this.restoreWhenLoaded_) {
    this.restoreWhenLoaded_ = false;  // This should only work once.
    if (this.decodeState())
      return;
  }
  this.play();
};
