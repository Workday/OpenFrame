// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Support modularity by calling to load a new SDCH filter dictionary.
// Note that this sort of calling can't be done in the /net directory, as it has
// no concept of the HTTP cache (which is only visible at the browser level).

#ifndef CHROME_BROWSER_NET_SDCH_DICTIONARY_FETCHER_H_
#define CHROME_BROWSER_NET_SDCH_DICTIONARY_FETCHER_H_

#include <queue>
#include <set>
#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/non_thread_safe.h"
#include "net/base/sdch_manager.h"
#include "net/url_request/url_fetcher_delegate.h"

namespace net {
class URLFetcher;
class URLRequestContextGetter;
}  // namespace net

class SdchDictionaryFetcher
    : public net::URLFetcherDelegate,
      public net::SdchFetcher,
      public base::NonThreadSafe {
 public:
  explicit SdchDictionaryFetcher(net::URLRequestContextGetter* context);
  virtual ~SdchDictionaryFetcher();

  // Stop fetching dictionaries, and abandon any current URLFetcheer operations
  // so that the IO thread can be stopped.
  static void Shutdown();

  // Implementation of SdchFetcher class.
  // This method gets the requested dictionary, and then calls back into the
  // SdchManager class with the dictionary's text.
  virtual void Schedule(const GURL& dictionary_url) OVERRIDE;

 private:
  // Delay in ms between Schedule and actual download.
  // This leaves the URL in a queue, which is de-duped, so that there is less
  // chance we'll try to load the same URL multiple times when a pile of
  // page subresources (or tabs opened in parallel) all suggest the dictionary.
  static const int kMsDelayFromRequestTillDownload = 100;

  // Ensure the download after the above delay.
  void ScheduleDelayedRun();

  // Make sure we're processing (or waiting for) the the arrival of the next URL
  // in the  |fetch_queue_|.
  void StartFetching();

  // Implementation of net::URLFetcherDelegate. Called after transmission
  // completes (either successfully or with failure).
  virtual void OnURLFetchComplete(const net::URLFetcher* source) OVERRIDE;

  // A queue of URLs that are being used to download dictionaries.
  std::queue<GURL> fetch_queue_;
  // The currently outstanding URL fetch of a dicitonary.
  // If this is null, then there is no outstanding request.
  scoped_ptr<net::URLFetcher> current_fetch_;

  // Always spread out the dictionary fetches, so that they don't steal
  // bandwidth from the actual page load.  Create delayed tasks to spread out
  // the download.
  base::WeakPtrFactory<SdchDictionaryFetcher> weak_factory_;
  bool task_is_pending_;

  // Althought the SDCH spec does not preclude a server from using a single URL
  // to load several distinct dictionaries (by telling a client to load a
  // dictionary from an URL several times), current implementations seem to have
  // that 1-1 relationship (i.e., each URL points at a single dictionary, and
  // the dictionary content does not change over time, and hence is not worth
  // trying to load more than once).  In addition, some dictionaries prove
  // unloadable only after downloading them (because they are too large?  ...or
  // malformed?). As a protective element, Chromium will *only* load a
  // dictionary at most once from a given URL (so that it doesn't waste
  // bandwidth trying repeatedly).
  // The following set lists all the dictionary URLs that we've tried to load,
  // so that we won't try to load from an URL more than once.
  // TODO(jar): Try to augment the SDCH proposal to include this restiction.
  std::set<GURL> attempted_load_;

  // Store the system_url_request_context_getter to use it when we start
  // fetching.
  scoped_refptr<net::URLRequestContextGetter> context_;

  DISALLOW_COPY_AND_ASSIGN(SdchDictionaryFetcher);
};

#endif  // CHROME_BROWSER_NET_SDCH_DICTIONARY_FETCHER_H_
