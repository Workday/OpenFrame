// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_HISTORY_HISTORY_PUBLISHER_H_
#define CHROME_BROWSER_HISTORY_HISTORY_PUBLISHER_H_

#include <vector>

#include "base/basictypes.h"
#include "base/strings/string16.h"

#if defined(OS_WIN)
#include "base/win/scoped_com_initializer.h"
#include "base/win/scoped_comptr.h"
#include "history_indexer.h"
#endif

class GURL;

namespace base {
class Time;
}

namespace history {

class HistoryPublisher {
 public:
  HistoryPublisher();
  ~HistoryPublisher();

  // Must call this function to complete initialization. Returns true if we
  // need to publish data to any indexers registered with us. Returns false if
  // there are none registered. On false, no other function should be called.
  bool Init();

  void PublishPageThumbnail(const std::vector<unsigned char>& thumbnail,
                            const GURL& url, const base::Time& time) const;
  void PublishPageContent(const base::Time& time, const GURL& url,
                          const string16& title,
                          const string16& contents) const;

 private:
  struct PageData {
    const base::Time& time;
    const GURL& url;
    const char16* html;
    const char16* title;
    const char* thumbnail_format;
    const std::vector<unsigned char>* thumbnail;
  };

  void PublishDataToIndexers(const PageData& page_data) const;

#if defined(OS_WIN)
  // Initializes the indexer_list_ with the list of indexers that registered
  // with us to index history. Returns true if there are any registered.
  bool ReadRegisteredIndexersFromRegistry();

  // Converts time represented by the Time class object to variant time in UTC.
  // Returns '0' if the time object is NULL.
  static double TimeToUTCVariantTime(const base::Time& time);

  typedef std::vector< base::win::ScopedComPtr<
      IChromeHistoryIndexer> > IndexerList;

  // The list of indexers registered to receive history data from us.
  IndexerList indexers_;

  // The Registry key under HKCU where the indexers need to register their
  // CLSID.
  static const wchar_t* const kRegKeyRegisteredIndexersInfo;

  base::win::ScopedCOMInitializer com_initializer_;
#endif

  // The format of the thumbnail we pass to indexers.
  static const char* const kThumbnailImageFormat;

  DISALLOW_COPY_AND_ASSIGN(HistoryPublisher);
};

}  // namespace history

#endif  // CHROME_BROWSER_HISTORY_HISTORY_PUBLISHER_H_
