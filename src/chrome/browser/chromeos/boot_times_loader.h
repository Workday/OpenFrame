// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_BOOT_TIMES_LOADER_H_
#define CHROME_BROWSER_CHROMEOS_BOOT_TIMES_LOADER_H_

#include <set>
#include <string>

#include "base/atomic_sequence_num.h"
#include "base/callback_forward.h"
#include "base/compiler_specific.h"
#include "base/time/time.h"
#include "chrome/common/cancelable_task_tracker.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/render_widget_host.h"

namespace chromeos {

// BootTimesLoader loads the bootimes of Chrome OS from the file system.
// Loading is done asynchronously on the file thread. Once loaded,
// BootTimesLoader calls back to a method of your choice with the boot times.
// To use BootTimesLoader, do the following:
//
// . In your class define a member field of type chromeos::BootTimesLoader and
//   CancelableTaskTracker.
// . Define the callback method, something like:
//   void OnBootTimesLoaded(const BootTimesLoader::BootTimes& boot_times);
// . When you want the version invoke: loader.GetBootTimes(callback, &tracker_);
class BootTimesLoader : public content::NotificationObserver {
 public:
  BootTimesLoader();
  virtual ~BootTimesLoader();

  static BootTimesLoader* Get();

  // Add a time marker for login. A timeline will be dumped to
  // /tmp/login-times-sent after login is done. If |send_to_uma| is true
  // the time between this marker and the last will be sent to UMA with
  // the identifier BootTime.|marker_name|.
  void AddLoginTimeMarker(const std::string& marker_name, bool send_to_uma);

  // Add a time marker for logout. A timeline will be dumped to
  // /tmp/logout-times-sent after logout is done. If |send_to_uma| is true
  // the time between this marker and the last will be sent to UMA with
  // the identifier ShutdownTime.|marker_name|.
  void AddLogoutTimeMarker(const std::string& marker_name, bool send_to_uma);

  // Records current uptime and disk usage for metrics use.
  // Posts task to file thread.
  // name will be used as part of file names in /tmp.
  // Existing stats files will not be overwritten.
  void RecordCurrentStats(const std::string& name);

  // Saves away the stats at main, so the can be recorded later. At main() time
  // the necessary threads don't exist yet for recording the data.
  void SaveChromeMainStats();

  // Records the data previously saved by SaveChromeMainStats(), using the
  // file thread. Existing stats files will not be overwritten.
  void RecordChromeMainStats();

  // Records the time that a login was attempted. This will overwrite any
  // previous login attempt times.
  void RecordLoginAttempted();

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Writes the logout times to a /tmp/logout-times-sent. Unlike login
  // times, we manually call this function for logout times, as we cannot
  // rely on notification service to tell when the logout is done.
  void WriteLogoutTimes();

 private:
  // BootTimesLoader calls into the Backend on the file thread to load
  // the boot times.
  class Backend : public base::RefCountedThreadSafe<Backend> {
   public:
    Backend() {}

   private:
    friend class base::RefCountedThreadSafe<Backend>;

    ~Backend() {}

    DISALLOW_COPY_AND_ASSIGN(Backend);
  };

  class TimeMarker {
   public:
    TimeMarker(const std::string& name, bool send_to_uma)
        : name_(name),
          time_(base::Time::NowFromSystemTime()),
          send_to_uma_(send_to_uma) {}
    std::string name() const { return name_; }
    base::Time time() const { return time_; }
    bool send_to_uma() const { return send_to_uma_; }

    // comparitor for sorting
    bool operator<(const TimeMarker& other) const {
      return time_ < other.time_;
    }

   private:
    friend class std::vector<TimeMarker>;
    std::string name_;
    base::Time time_;
    bool send_to_uma_;
  };

  struct Stats {
   public:
    std::string uptime;
    std::string disk;
  };

  static void RecordStats(
      const std::string& name, const Stats& stats);
  static Stats GetCurrentStats();
  static void WriteTimes(const std::string base_name,
                         const std::string uma_name,
                         const std::string uma_prefix,
                         std::vector<TimeMarker> login_times);
  static void AddMarker(std::vector<TimeMarker>* vector, TimeMarker marker);

  void LoginDone();

  // Used to hold the stats at main().
  Stats chrome_main_stats_;
  scoped_refptr<Backend> backend_;

  // Used to track notifications for login.
  content::NotificationRegistrar registrar_;
  base::AtomicSequenceNumber num_tabs_;
  bool have_registered_;

  std::vector<TimeMarker> login_time_markers_;
  std::vector<TimeMarker> logout_time_markers_;
  std::set<content::RenderWidgetHost*> render_widget_hosts_loading_;

  DISALLOW_COPY_AND_ASSIGN(BootTimesLoader);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_BOOT_TIMES_LOADER_H_
