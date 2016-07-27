// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_DATA_USAGE_TAB_DATA_USE_ENTRY_H_
#define CHROME_BROWSER_ANDROID_DATA_USAGE_TAB_DATA_USE_ENTRY_H_

#include <deque>
#include <string>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/time/time.h"

namespace chrome {

namespace android {

// TabDataUseTrackingSession maintains the information about a single tracking
// session within a browser tab.
struct TabDataUseTrackingSession {
  TabDataUseTrackingSession(const std::string& label,
                            const base::TimeTicks& start_time)
      : label(label), start_time(start_time) {}

  // Tracking label to be associated with the data usage of this session.
  const std::string label;

  // Time the data use tracking session started.
  const base::TimeTicks start_time;

  // Time the data use tracking session ended. |end_time| will be null if the
  // tracking session is currently active.
  base::TimeTicks end_time;
};

// TabDataUseEntry contains the history of the disjoint tracking sessions for a
// single browser tab.
class TabDataUseEntry {
 public:
  TabDataUseEntry();

  TabDataUseEntry(const TabDataUseEntry& other);

  virtual ~TabDataUseEntry();

  TabDataUseEntry& operator=(const TabDataUseEntry& other) = default;

  // Initiates a new tracking session with the given |label|. Returns false if a
  // tracking session is already active, and true otherwise.
  bool StartTracking(const std::string& label);

  // Ends the active tracking session. Returns false if there is no active
  // tracking session, and true otherwise.
  bool EndTracking();

  // Ends the active tracking session if it is labeled with |label| and returns
  // true.
  bool EndTrackingWithLabel(const std::string& label);

  // Records that the tab has been closed, in preparation for deletion.
  void OnTabCloseEvent();

  // Gets the label of the session in history that was active at
  // |data_use_time|. |output_label| must not be null. If a session is found,
  // returns true and |output_label| is populated. Otherwise returns false and
  // |output_label| is set to empty string.
  bool GetLabel(const base::TimeTicks& data_use_time,
                std::string* output_label) const;

  // Returns true if the tracking session is currently active.
  bool IsTrackingDataUse() const;

  // Returns true if the tab has expired. A closed tab entry expires
  // |kClosedTabExpirationDurationSeconds| seconds after it was closed. An open
  // tab entry expires |kOpenTabExpirationDurationSeconds| seconds after the
  // most recent tracking session start or end event.
  bool IsExpired() const;

  // Returns the latest time a tracking session was started or ended. Returned
  // time will be null if no tracking session was ever started or ended.
  const base::TimeTicks GetLatestStartOrEndTime() const;

 private:
  friend class TabDataUseEntryTest;
  friend class MockTabDataUseEntryTest;
  FRIEND_TEST_ALL_PREFIXES(TabDataUseEntryTest, SingleTabSessionCloseEvent);
  FRIEND_TEST_ALL_PREFIXES(TabDataUseEntryTest, MultipleTabSessionCloseEvent);
  FRIEND_TEST_ALL_PREFIXES(TabDataUseEntryTest, EndTrackingWithLabel);
  FRIEND_TEST_ALL_PREFIXES(DataUseTabModelTest, TabCloseEvent);
  FRIEND_TEST_ALL_PREFIXES(DataUseTabModelTest,
                           ExpiredInactiveTabEntryRemovaltimeHistogram);
  FRIEND_TEST_ALL_PREFIXES(DataUseTabModelTest,
                           ExpiredActiveTabEntryRemovaltimeHistogram);
  FRIEND_TEST_ALL_PREFIXES(MockTabDataUseEntryTest, CompactTabSessionHistory);
  FRIEND_TEST_ALL_PREFIXES(MockTabDataUseEntryTest,
                           OldInactiveSessionRemovaltimeHistogram);

  typedef std::deque<TabDataUseTrackingSession> TabSessions;

  // Virtualized for unit test support.
  virtual base::TimeTicks Now() const;

  // Compacts the history of tracking sessions by removing oldest sessions to
  // keep the size of |sessions_| within |kMaxSessionsPerTab| entries.
  void CompactSessionHistory();

  // Contains the history of sessions in chronological order. Oldest sessions
  // will be at the front of the queue, and new sessions will get added to the
  // end of the queue.
  TabSessions sessions_;

  // Indicates the time the tab was closed. |tab_close_time_| will be null if
  // the tab is still open.
  base::TimeTicks tab_close_time_;

  // Maximum number of tracking sessions to maintain per tab.
  const size_t max_sessions_per_tab_;

  // Expiration duration for a closed tab entry and an open tab entry
  // respectively.
  const base::TimeDelta closed_tab_expiration_duration_;
  const base::TimeDelta open_tab_expiration_duration_;
};

}  // namespace android

}  // namespace chrome

#endif  // CHROME_BROWSER_ANDROID_DATA_USAGE_TAB_DATA_USE_ENTRY_H_
