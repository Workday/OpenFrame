// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOWNLOAD_DOWNLOAD_STATS_H_
#define CHROME_BROWSER_DOWNLOAD_DOWNLOAD_STATS_H_

// Record the total number of items and the number of in-progress items showing
// in the shelf when it closes.  Set |autoclose| to true when the shelf is
// closing itself, false when the user explicitly closed it.
void RecordDownloadShelfClose(int size, int in_progress, bool autoclose);

// Used for counting UMA stats. Similar to content's
// download_stats::DownloadCountTypes but from the chrome layer.
enum ChromeDownloadCountTypes {
  // Stale enum values left around os that values passed to UMA don't
  // change.
  CHROME_DOWNLOAD_COUNT_UNUSED_0 = 0,
  CHROME_DOWNLOAD_COUNT_UNUSED_1,
  CHROME_DOWNLOAD_COUNT_UNUSED_2,
  CHROME_DOWNLOAD_COUNT_UNUSED_3,

  // A download *would* have been initiated, but it was blocked
  // by the DownloadThrottlingResourceHandler.
  CHROME_DOWNLOAD_COUNT_BLOCKED_BY_THROTTLING,

  CHROME_DOWNLOAD_COUNT_TYPES_LAST_ENTRY
};

// Used for counting UMA stats. Similar to content's
// download_stats::DownloadInitiattionSources but from the chrome layer.
enum ChromeDownloadSource {
  // The download was initiated by navigating to a URL (e.g. by user click).
  DOWNLOAD_INITIATED_BY_NAVIGATION = 0,

  // The download was initiated by invoking a context menu within a page.
  DOWNLOAD_INITIATED_BY_CONTEXT_MENU,

  // The download was initiated by the WebStore installer.
  DOWNLOAD_INITIATED_BY_WEBSTORE_INSTALLER,

  // The download was initiated by the ImageBurner (cros).
  DOWNLOAD_INITIATED_BY_IMAGE_BURNER,

  // The download was initiated by the plugin installer.
  DOWNLOAD_INITIATED_BY_PLUGIN_INSTALLER,

  // The download was initiated by the PDF plugin..
  DOWNLOAD_INITIATED_BY_PDF_SAVE,

  // The download was initiated by chrome.downloads.download().
  DOWNLOAD_INITIATED_BY_EXTENSION,

  CHROME_DOWNLOAD_SOURCE_LAST_ENTRY,
};

// Increment one of the above counts.
void RecordDownloadCount(ChromeDownloadCountTypes type);

// Record initiation of a download from a specific source.
void RecordDownloadSource(ChromeDownloadSource source);

#endif  // CHROME_BROWSER_DOWNLOAD_DOWNLOAD_STATS_H_
