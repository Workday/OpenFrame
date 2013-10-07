// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//------------------------------------------------------------------------------
// Description of the life cycle of a instance of MetricsService.
//
//  OVERVIEW
//
// A MetricsService instance is typically created at application startup.  It is
// the central controller for the acquisition of log data, and the automatic
// transmission of that log data to an external server.  Its major job is to
// manage logs, grouping them for transmission, and transmitting them.  As part
// of its grouping, MS finalizes logs by including some just-in-time gathered
// memory statistics, snapshotting the current stats of numerous histograms,
// closing the logs, translating to protocol buffer format, and compressing the
// results for transmission.  Transmission includes submitting a compressed log
// as data in a URL-post, and retransmitting (or retaining at process
// termination) if the attempted transmission failed.  Retention across process
// terminations is done using the the PrefServices facilities. The retained logs
// (the ones that never got transmitted) are compressed and base64-encoded
// before being persisted.
//
// Logs fall into one of two categories: "initial logs," and "ongoing logs."
// There is at most one initial log sent for each complete run of the chromium
// product (from startup, to browser shutdown).  An initial log is generally
// transmitted some short time (1 minute?) after startup, and includes stats
// such as recent crash info, the number and types of plugins, etc.  The
// external server's response to the initial log conceptually tells this MS if
// it should continue transmitting logs (during this session). The server
// response can actually be much more detailed, and always includes (at a
// minimum) how often additional ongoing logs should be sent.
//
// After the above initial log, a series of ongoing logs will be transmitted.
// The first ongoing log actually begins to accumulate information stating when
// the MS was first constructed.  Note that even though the initial log is
// commonly sent a full minute after startup, the initial log does not include
// much in the way of user stats.   The most common interlog period (delay)
// is 30 minutes. That time period starts when the first user action causes a
// logging event.  This means that if there is no user action, there may be long
// periods without any (ongoing) log transmissions.  Ongoing logs typically
// contain very detailed records of user activities (ex: opened tab, closed
// tab, fetched URL, maximized window, etc.)  In addition, just before an
// ongoing log is closed out, a call is made to gather memory statistics.  Those
// memory statistics are deposited into a histogram, and the log finalization
// code is then called.  In the finalization, a call to a Histogram server
// acquires a list of all local histograms that have been flagged for upload
// to the UMA server.  The finalization also acquires a the most recent number
// of page loads, along with any counts of renderer or plugin crashes.
//
// When the browser shuts down, there will typically be a fragment of an ongoing
// log that has not yet been transmitted.  At shutdown time, that fragment
// is closed (including snapshotting histograms), and persisted, for
// potential transmission during a future run of the product.
//
// There are two slightly abnormal shutdown conditions.  There is a
// "disconnected scenario," and a "really fast startup and shutdown" scenario.
// In the "never connected" situation, the user has (during the running of the
// process) never established an internet connection.  As a result, attempts to
// transmit the initial log have failed, and a lot(?) of data has accumulated in
// the ongoing log (which didn't yet get closed, because there was never even a
// contemplation of sending it).  There is also a kindred "lost connection"
// situation, where a loss of connection prevented an ongoing log from being
// transmitted, and a (still open) log was stuck accumulating a lot(?) of data,
// while the earlier log retried its transmission.  In both of these
// disconnected situations, two logs need to be, and are, persistently stored
// for future transmission.
//
// The other unusual shutdown condition, termed "really fast startup and
// shutdown," involves the deliberate user termination of the process before
// the initial log is even formed or transmitted. In that situation, no logging
// is done, but the historical crash statistics remain (unlogged) for inclusion
// in a future run's initial log.  (i.e., we don't lose crash stats).
//
// With the above overview, we can now describe the state machine's various
// stats, based on the State enum specified in the state_ member.  Those states
// are:
//
//    INITIALIZED,            // Constructor was called.
//    INIT_TASK_SCHEDULED,    // Waiting for deferred init tasks to complete.
//    INIT_TASK_DONE,         // Waiting for timer to send initial log.
//    INITIAL_LOG_READY,      // Initial log generated, and waiting for reply.
//    SENDING_OLD_LOGS,       // Sending unsent logs from previous session.
//    SENDING_CURRENT_LOGS,   // Sending standard current logs as they accrue.
//
// In more detail, we have:
//
//    INITIALIZED,            // Constructor was called.
// The MS has been constructed, but has taken no actions to compose the
// initial log.
//
//    INIT_TASK_SCHEDULED,    // Waiting for deferred init tasks to complete.
// Typically about 30 seconds after startup, a task is sent to a second thread
// (the file thread) to perform deferred (lower priority and slower)
// initialization steps such as getting the list of plugins.  That task will
// (when complete) make an async callback (via a Task) to indicate the
// completion.
//
//    INIT_TASK_DONE,         // Waiting for timer to send initial log.
// The callback has arrived, and it is now possible for an initial log to be
// created.  This callback typically arrives back less than one second after
// the deferred init task is dispatched.
//
//    INITIAL_LOG_READY,      // Initial log generated, and waiting for reply.
// This state is entered only after an initial log has been composed, and
// prepared for transmission.  It is also the case that any previously unsent
// logs have been loaded into instance variables for possible transmission.
//
//    SENDING_OLD_LOGS,       // Sending unsent logs from previous session.
// This state indicates that the initial log for this session has been
// successfully sent and it is now time to send any logs that were
// saved from previous sessions.  All such logs will be transmitted before
// exiting this state, and proceeding with ongoing logs from the current session
// (see next state).
//
//    SENDING_CURRENT_LOGS,   // Sending standard current logs as they accrue.
// Current logs are being accumulated.  Typically every 20 minutes a log is
// closed and finalized for transmission, at the same time as a new log is
// started.
//
// The progression through the above states is simple, and sequential, in the
// most common use cases.  States proceed from INITIAL to SENDING_CURRENT_LOGS,
// and remain in the latter until shutdown.
//
// The one unusual case is when the user asks that we stop logging.  When that
// happens, any staged (transmission in progress) log is persisted, and any log
// that is currently accumulating is also finalized and persisted.  We then
// regress back to the SEND_OLD_LOGS state in case the user enables log
// recording again during this session.  This way anything we have persisted
// will be sent automatically if/when we progress back to SENDING_CURRENT_LOG
// state.
//
// Another similar case is on mobile, when the application is backgrounded and
// then foregrounded again. Backgrounding created new "old" stored logs, so the
// state drops back from SENDING_CURRENT_LOGS to SENDING_OLD_LOGS so those logs
// will be sent.
//
// Also note that whenever we successfully send an old log, we mirror the list
// of logs into the PrefService. This ensures that IF we crash, we won't start
// up and retransmit our old logs again.
//
// Due to race conditions, it is always possible that a log file could be sent
// twice.  For example, if a log file is sent, but not yet acknowledged by
// the external server, and the user shuts down, then a copy of the log may be
// saved for re-transmission.  These duplicates could be filtered out server
// side, but are not expected to be a significant problem.
//
//
//------------------------------------------------------------------------------

#include "chrome/browser/metrics/metrics_service.h"

#include <algorithm>

#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/guid.h"
#include "base/md5.h"
#include "base/metrics/histogram.h"
#include "base/metrics/sparse_histogram.h"
#include "base/metrics/statistics_recorder.h"
#include "base/prefs/pref_registry_simple.h"
#include "base/prefs/pref_service.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "base/tracked_objects.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/process_map.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/memory_details.h"
#include "chrome/browser/metrics/compression_utils.h"
#include "chrome/browser/metrics/gzipped_protobufs_field_trial.h"
#include "chrome/browser/metrics/metrics_log.h"
#include "chrome/browser/metrics/metrics_log_serializer.h"
#include "chrome/browser/metrics/metrics_reporting_scheduler.h"
#include "chrome/browser/metrics/time_ticks_experiment_win.h"
#include "chrome/browser/metrics/tracking_synchronizer.h"
#include "chrome/browser/net/http_pipelining_compatibility_client.h"
#include "chrome/browser/net/network_stats.h"
#include "chrome/browser/omnibox/omnibox_log.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_otr_state.h"
#include "chrome/browser/ui/search/search_tab_helper.h"
#include "chrome/common/child_process_logging.h"
#include "chrome/common/chrome_result_codes.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/metrics/caching_permuted_entropy_provider.h"
#include "chrome/common/metrics/entropy_provider.h"
#include "chrome/common/metrics/metrics_log_manager.h"
#include "chrome/common/net/test_server_locations.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/render_messages.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/histogram_fetcher.h"
#include "content/public/browser/load_notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/process_type.h"
#include "content/public/common/webplugininfo.h"
#include "net/base/load_flags.h"
#include "net/url_request/url_fetcher.h"

// TODO(port): port browser_distribution.h.
#if !defined(OS_POSIX)
#include "chrome/installer/util/browser_distribution.h"
#endif

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/external_metrics.h"
#include "chrome/browser/chromeos/system/statistics_provider.h"
#endif

#if defined(OS_WIN)
#include <windows.h>  // Needed for STATUS_* codes
#endif

using base::Time;
using content::BrowserThread;
using content::ChildProcessData;
using content::LoadNotificationDetails;
using content::PluginService;

namespace {

// Check to see that we're being called on only one thread.
bool IsSingleThreaded() {
  static base::PlatformThreadId thread_id = 0;
  if (!thread_id)
    thread_id = base::PlatformThread::CurrentId();
  return base::PlatformThread::CurrentId() == thread_id;
}

// The delay, in seconds, after starting recording before doing expensive
// initialization work.
#if defined(OS_ANDROID) || defined(OS_IOS)
// On mobile devices, a significant portion of sessions last less than a minute.
// Use a shorter timer on these platforms to avoid losing data.
// TODO(dfalcantara): To avoid delaying startup, tighten up initialization so
//                    that it occurs after the user gets their initial page.
const int kInitializationDelaySeconds = 5;
#else
const int kInitializationDelaySeconds = 30;
#endif

// This specifies the amount of time to wait for all renderers to send their
// data.
const int kMaxHistogramGatheringWaitDuration = 60000;  // 60 seconds.

// The maximum number of events in a log uploaded to the UMA server.
const int kEventLimit = 2400;

// If an upload fails, and the transmission was over this byte count, then we
// will discard the log, and not try to retransmit it.  We also don't persist
// the log to the prefs for transmission during the next chrome session if this
// limit is exceeded.
const size_t kUploadLogAvoidRetransmitSize = 50000;

// Interval, in minutes, between state saves.
const int kSaveStateIntervalMinutes = 5;

enum ResponseStatus {
  UNKNOWN_FAILURE,
  SUCCESS,
  BAD_REQUEST,  // Invalid syntax or log too large.
  NO_RESPONSE,
  NUM_RESPONSE_STATUSES
};

ResponseStatus ResponseCodeToStatus(int response_code) {
  switch (response_code) {
    case 200:
      return SUCCESS;
    case 400:
      return BAD_REQUEST;
    case net::URLFetcher::RESPONSE_CODE_INVALID:
      return NO_RESPONSE;
    default:
      return UNKNOWN_FAILURE;
  }
}

// The argument used to generate a non-identifying entropy source. We want no
// more than 13 bits of entropy, so use this max to return a number in the range
// [0, 7999] as the entropy source (12.97 bits of entropy).
const int kMaxLowEntropySize = 8000;

// Default prefs value for prefs::kMetricsLowEntropySource to indicate that the
// value has not yet been set.
const int kLowEntropySourceNotSet = -1;

// Generates a new non-identifying entropy source used to seed persistent
// activities.
int GenerateLowEntropySource() {
  return base::RandInt(0, kMaxLowEntropySize - 1);
}

// Converts an exit code into something that can be inserted into our
// histograms (which expect non-negative numbers less than MAX_INT).
int MapCrashExitCodeForHistogram(int exit_code) {
#if defined(OS_WIN)
  // Since |abs(STATUS_GUARD_PAGE_VIOLATION) == MAX_INT| it causes problems in
  // histograms.cc. Solve this by remapping it to a smaller value, which
  // hopefully doesn't conflict with other codes.
  if (exit_code == STATUS_GUARD_PAGE_VIOLATION)
    return 0x1FCF7EC3;  // Randomly picked number.
#endif

  return std::abs(exit_code);
}

void MarkAppCleanShutdownAndCommit() {
  PrefService* pref = g_browser_process->local_state();
  pref->SetBoolean(prefs::kStabilityExitedCleanly, true);
  // Start writing right away (write happens on a different thread).
  pref->CommitPendingWrite();
}

}  // namespace

// static
MetricsService::ShutdownCleanliness MetricsService::clean_shutdown_status_ =
    MetricsService::CLEANLY_SHUTDOWN;

// This is used to quickly log stats from child process related notifications in
// MetricsService::child_stats_buffer_.  The buffer's contents are transferred
// out when Local State is periodically saved.  The information is then
// reported to the UMA server on next launch.
struct MetricsService::ChildProcessStats {
 public:
  explicit ChildProcessStats(int process_type)
      : process_launches(0),
        process_crashes(0),
        instances(0),
        loading_errors(0),
        process_type(process_type) {}

  // This constructor is only used by the map to return some default value for
  // an index for which no value has been assigned.
  ChildProcessStats()
      : process_launches(0),
        process_crashes(0),
        instances(0),
        loading_errors(0),
        process_type(content::PROCESS_TYPE_UNKNOWN) {}

  // The number of times that the given child process has been launched
  int process_launches;

  // The number of times that the given child process has crashed
  int process_crashes;

  // The number of instances of this child process that have been created.
  // An instance is a DOM object rendered by this child process during a page
  // load.
  int instances;

  // The number of times there was an error loading an instance of this child
  // process.
  int loading_errors;

  int process_type;
};

// Handles asynchronous fetching of memory details.
// Will run the provided task after finished.
class MetricsMemoryDetails : public MemoryDetails {
 public:
  explicit MetricsMemoryDetails(const base::Closure& callback)
      : callback_(callback) {}

  virtual void OnDetailsAvailable() OVERRIDE {
    base::MessageLoop::current()->PostTask(FROM_HERE, callback_);
  }

 private:
  virtual ~MetricsMemoryDetails() {}

  base::Closure callback_;
  DISALLOW_COPY_AND_ASSIGN(MetricsMemoryDetails);
};

// static
void MetricsService::RegisterPrefs(PrefRegistrySimple* registry) {
  DCHECK(IsSingleThreaded());
  registry->RegisterStringPref(prefs::kMetricsClientID, std::string());
  registry->RegisterIntegerPref(prefs::kMetricsLowEntropySource,
                                kLowEntropySourceNotSet);
  registry->RegisterInt64Pref(prefs::kMetricsClientIDTimestamp, 0);
  registry->RegisterInt64Pref(prefs::kStabilityLaunchTimeSec, 0);
  registry->RegisterInt64Pref(prefs::kStabilityLastTimestampSec, 0);
  registry->RegisterStringPref(prefs::kStabilityStatsVersion, std::string());
  registry->RegisterInt64Pref(prefs::kStabilityStatsBuildTime, 0);
  registry->RegisterBooleanPref(prefs::kStabilityExitedCleanly, true);
  registry->RegisterBooleanPref(prefs::kStabilitySessionEndCompleted, true);
  registry->RegisterIntegerPref(prefs::kMetricsSessionID, -1);
  registry->RegisterIntegerPref(prefs::kStabilityLaunchCount, 0);
  registry->RegisterIntegerPref(prefs::kStabilityCrashCount, 0);
  registry->RegisterIntegerPref(prefs::kStabilityIncompleteSessionEndCount, 0);
  registry->RegisterIntegerPref(prefs::kStabilityPageLoadCount, 0);
  registry->RegisterIntegerPref(prefs::kStabilityRendererCrashCount, 0);
  registry->RegisterIntegerPref(prefs::kStabilityExtensionRendererCrashCount,
                                0);
  registry->RegisterIntegerPref(prefs::kStabilityRendererHangCount, 0);
  registry->RegisterIntegerPref(prefs::kStabilityChildProcessCrashCount, 0);
  registry->RegisterIntegerPref(prefs::kStabilityBreakpadRegistrationFail, 0);
  registry->RegisterIntegerPref(prefs::kStabilityBreakpadRegistrationSuccess,
                                0);
  registry->RegisterIntegerPref(prefs::kStabilityDebuggerPresent, 0);
  registry->RegisterIntegerPref(prefs::kStabilityDebuggerNotPresent, 0);
#if defined(OS_CHROMEOS)
  registry->RegisterIntegerPref(prefs::kStabilityOtherUserCrashCount, 0);
  registry->RegisterIntegerPref(prefs::kStabilityKernelCrashCount, 0);
  registry->RegisterIntegerPref(prefs::kStabilitySystemUncleanShutdownCount, 0);
#endif  // OS_CHROMEOS

  registry->RegisterListPref(prefs::kMetricsInitialLogs);
  registry->RegisterListPref(prefs::kMetricsOngoingLogs);

  registry->RegisterInt64Pref(prefs::kInstallDate, 0);
  registry->RegisterInt64Pref(prefs::kUninstallMetricsPageLoadCount, 0);
  registry->RegisterInt64Pref(prefs::kUninstallLaunchCount, 0);
  registry->RegisterInt64Pref(prefs::kUninstallMetricsUptimeSec, 0);
  registry->RegisterInt64Pref(prefs::kUninstallLastLaunchTimeSec, 0);
  registry->RegisterInt64Pref(prefs::kUninstallLastObservedRunTimeSec, 0);
}

// static
void MetricsService::DiscardOldStabilityStats(PrefService* local_state) {
  local_state->SetBoolean(prefs::kStabilityExitedCleanly, true);
  local_state->SetBoolean(prefs::kStabilitySessionEndCompleted, true);

  local_state->SetInteger(prefs::kStabilityIncompleteSessionEndCount, 0);
  local_state->SetInteger(prefs::kStabilityBreakpadRegistrationSuccess, 0);
  local_state->SetInteger(prefs::kStabilityBreakpadRegistrationFail, 0);
  local_state->SetInteger(prefs::kStabilityDebuggerPresent, 0);
  local_state->SetInteger(prefs::kStabilityDebuggerNotPresent, 0);

  local_state->SetInteger(prefs::kStabilityLaunchCount, 0);
  local_state->SetInteger(prefs::kStabilityCrashCount, 0);

  local_state->SetInteger(prefs::kStabilityPageLoadCount, 0);
  local_state->SetInteger(prefs::kStabilityRendererCrashCount, 0);
  local_state->SetInteger(prefs::kStabilityRendererHangCount, 0);

  local_state->SetInt64(prefs::kStabilityLaunchTimeSec, 0);
  local_state->SetInt64(prefs::kStabilityLastTimestampSec, 0);

  local_state->ClearPref(prefs::kStabilityPluginStats);

  local_state->ClearPref(prefs::kMetricsInitialLogs);
  local_state->ClearPref(prefs::kMetricsOngoingLogs);
}

MetricsService::MetricsService()
    : recording_active_(false),
      reporting_active_(false),
      test_mode_active_(false),
      state_(INITIALIZED),
      low_entropy_source_(kLowEntropySourceNotSet),
      idle_since_last_transmission_(false),
      next_window_id_(0),
      self_ptr_factory_(this),
      state_saver_factory_(this),
      waiting_for_asynchronous_reporting_step_(false),
      entropy_source_returned_(LAST_ENTROPY_NONE) {
  DCHECK(IsSingleThreaded());
  InitializeMetricsState();

  base::Closure callback = base::Bind(&MetricsService::StartScheduledUpload,
                                      self_ptr_factory_.GetWeakPtr());
  scheduler_.reset(new MetricsReportingScheduler(callback));
  log_manager_.set_log_serializer(new MetricsLogSerializer());
  log_manager_.set_max_ongoing_log_store_size(kUploadLogAvoidRetransmitSize);

  BrowserChildProcessObserver::Add(this);
}

MetricsService::~MetricsService() {
  DisableRecording();

  BrowserChildProcessObserver::Remove(this);
}

void MetricsService::Start() {
  HandleIdleSinceLastTransmission(false);
  EnableRecording();
  EnableReporting();
}

void MetricsService::StartRecordingForTests() {
  test_mode_active_ = true;
  EnableRecording();
  DisableReporting();
}

void MetricsService::Stop() {
  HandleIdleSinceLastTransmission(false);
  DisableReporting();
  DisableRecording();
}

void MetricsService::EnableReporting() {
  if (reporting_active_)
    return;
  reporting_active_ = true;
  StartSchedulerIfNecessary();
}

void MetricsService::DisableReporting() {
  reporting_active_ = false;
}

std::string MetricsService::GetClientId() {
  return client_id_;
}

scoped_ptr<const base::FieldTrial::EntropyProvider>
    MetricsService::CreateEntropyProvider(bool reporting_will_be_enabled) {
  // For metrics reporting-enabled users, we combine the client ID and low
  // entropy source to get the final entropy source. Otherwise, only use the low
  // entropy source.
  // This has two useful properties:
  //  1) It makes the entropy source less identifiable for parties that do not
  //     know the low entropy source.
  //  2) It makes the final entropy source resettable.
  const int low_entropy_source_value = GetLowEntropySource();
  UMA_HISTOGRAM_SPARSE_SLOWLY("UMA.LowEntropySourceValue",
                              low_entropy_source_value);
  if (reporting_will_be_enabled) {
    if (entropy_source_returned_ == LAST_ENTROPY_NONE)
      entropy_source_returned_ = LAST_ENTROPY_HIGH;
    DCHECK_EQ(LAST_ENTROPY_HIGH, entropy_source_returned_);
    const std::string high_entropy_source =
        client_id_ + base::IntToString(low_entropy_source_value);
    return scoped_ptr<const base::FieldTrial::EntropyProvider>(
        new metrics::SHA1EntropyProvider(high_entropy_source));
  }

  if (entropy_source_returned_ == LAST_ENTROPY_NONE)
    entropy_source_returned_ = LAST_ENTROPY_LOW;
  DCHECK_EQ(LAST_ENTROPY_LOW, entropy_source_returned_);

#if defined(OS_ANDROID) || defined(OS_IOS)
  return scoped_ptr<const base::FieldTrial::EntropyProvider>(
      new metrics::CachingPermutedEntropyProvider(
          g_browser_process->local_state(),
          low_entropy_source_value,
          kMaxLowEntropySize));
#else
  return scoped_ptr<const base::FieldTrial::EntropyProvider>(
      new metrics::PermutedEntropyProvider(low_entropy_source_value,
                                           kMaxLowEntropySize));
#endif
}

void MetricsService::ForceClientIdCreation() {
  if (!client_id_.empty())
    return;
  PrefService* pref = g_browser_process->local_state();
  client_id_ = pref->GetString(prefs::kMetricsClientID);
  if (!client_id_.empty())
    return;

  client_id_ = GenerateClientID();
  pref->SetString(prefs::kMetricsClientID, client_id_);

  // Might as well make a note of how long this ID has existed
  pref->SetString(prefs::kMetricsClientIDTimestamp,
                  base::Int64ToString(Time::Now().ToTimeT()));
}

void MetricsService::EnableRecording() {
  DCHECK(IsSingleThreaded());

  if (recording_active_)
    return;
  recording_active_ = true;

  ForceClientIdCreation();
  child_process_logging::SetClientId(client_id_);
  if (!log_manager_.current_log())
    OpenNewLog();

  SetUpNotifications(&registrar_, this);
  content::RemoveActionCallback(action_callback_);
  action_callback_ = base::Bind(&MetricsService::OnUserAction,
                                base::Unretained(this));
  content::AddActionCallback(action_callback_);
}

void MetricsService::DisableRecording() {
  DCHECK(IsSingleThreaded());

  if (!recording_active_)
    return;
  recording_active_ = false;

  content::RemoveActionCallback(action_callback_);
  registrar_.RemoveAll();
  PushPendingLogsToPersistentStorage();
  DCHECK(!log_manager_.has_staged_log());
}

bool MetricsService::recording_active() const {
  DCHECK(IsSingleThreaded());
  return recording_active_;
}

bool MetricsService::reporting_active() const {
  DCHECK(IsSingleThreaded());
  return reporting_active_;
}

// static
void MetricsService::SetUpNotifications(
    content::NotificationRegistrar* registrar,
    content::NotificationObserver* observer) {
  registrar->Add(observer, chrome::NOTIFICATION_BROWSER_OPENED,
                 content::NotificationService::AllBrowserContextsAndSources());
  registrar->Add(observer, chrome::NOTIFICATION_BROWSER_CLOSED,
                 content::NotificationService::AllSources());
  registrar->Add(observer, chrome::NOTIFICATION_TAB_PARENTED,
                 content::NotificationService::AllSources());
  registrar->Add(observer, chrome::NOTIFICATION_TAB_CLOSING,
                 content::NotificationService::AllSources());
  registrar->Add(observer, content::NOTIFICATION_LOAD_START,
                 content::NotificationService::AllSources());
  registrar->Add(observer, content::NOTIFICATION_LOAD_STOP,
                 content::NotificationService::AllSources());
  registrar->Add(observer, content::NOTIFICATION_RENDERER_PROCESS_CLOSED,
                 content::NotificationService::AllSources());
  registrar->Add(observer, content::NOTIFICATION_RENDERER_PROCESS_HANG,
                 content::NotificationService::AllSources());
  registrar->Add(observer, chrome::NOTIFICATION_OMNIBOX_OPENED_URL,
                 content::NotificationService::AllSources());
}

void MetricsService::BrowserChildProcessHostConnected(
    const content::ChildProcessData& data) {
  GetChildProcessStats(data).process_launches++;
}

void MetricsService::BrowserChildProcessCrashed(
    const content::ChildProcessData& data) {
  GetChildProcessStats(data).process_crashes++;
  // Exclude plugin crashes from the count below because we report them via
  // a separate UMA metric.
  if (!IsPluginProcess(data.process_type))
    IncrementPrefValue(prefs::kStabilityChildProcessCrashCount);
}

void MetricsService::BrowserChildProcessInstanceCreated(
    const content::ChildProcessData& data) {
  GetChildProcessStats(data).instances++;
}

void MetricsService::Observe(int type,
                             const content::NotificationSource& source,
                             const content::NotificationDetails& details) {
  DCHECK(log_manager_.current_log());
  DCHECK(IsSingleThreaded());

  if (!CanLogNotification())
    return;

  switch (type) {
    case chrome::NOTIFICATION_BROWSER_OPENED:
    case chrome::NOTIFICATION_BROWSER_CLOSED:
    case chrome::NOTIFICATION_TAB_PARENTED:
    case chrome::NOTIFICATION_TAB_CLOSING:
    case content::NOTIFICATION_LOAD_STOP:
      // These notifications are currently used only to break out of idle mode.
      break;

    case content::NOTIFICATION_LOAD_START: {
      content::NavigationController* controller =
          content::Source<content::NavigationController>(source).ptr();
      content::WebContents* web_contents = controller->GetWebContents();
      LogLoadStarted(web_contents);
      break;
    }

    case content::NOTIFICATION_RENDERER_PROCESS_CLOSED: {
        content::RenderProcessHost::RendererClosedDetails* process_details =
            content::Details<
                content::RenderProcessHost::RendererClosedDetails>(
                    details).ptr();
        content::RenderProcessHost* host =
            content::Source<content::RenderProcessHost>(source).ptr();
        LogRendererCrash(
            host, process_details->status, process_details->exit_code);
      }
      break;

    case content::NOTIFICATION_RENDERER_PROCESS_HANG:
      LogRendererHang();
      break;

    case chrome::NOTIFICATION_OMNIBOX_OPENED_URL: {
      MetricsLog* current_log =
          static_cast<MetricsLog*>(log_manager_.current_log());
      DCHECK(current_log);
      current_log->RecordOmniboxOpenedURL(
          *content::Details<OmniboxLog>(details).ptr());
      break;
    }

    default:
      NOTREACHED();
      break;
  }

  HandleIdleSinceLastTransmission(false);
}

void MetricsService::HandleIdleSinceLastTransmission(bool in_idle) {
  // If there wasn't a lot of action, maybe the computer was asleep, in which
  // case, the log transmissions should have stopped.  Here we start them up
  // again.
  if (!in_idle && idle_since_last_transmission_)
    StartSchedulerIfNecessary();
  idle_since_last_transmission_ = in_idle;
}

void MetricsService::RecordStartOfSessionEnd() {
  LogCleanShutdown();
  RecordBooleanPrefValue(prefs::kStabilitySessionEndCompleted, false);
}

void MetricsService::RecordCompletedSessionEnd() {
  LogCleanShutdown();
  RecordBooleanPrefValue(prefs::kStabilitySessionEndCompleted, true);
}

#if defined(OS_ANDROID) || defined(OS_IOS)
void MetricsService::OnAppEnterBackground() {
  scheduler_->Stop();

  MarkAppCleanShutdownAndCommit();

  // At this point, there's no way of knowing when the process will be
  // killed, so this has to be treated similar to a shutdown, closing and
  // persisting all logs. Unlinke a shutdown, the state is primed to be ready
  // to continue logging and uploading if the process does return.
  if (recording_active() && state_ >= INITIAL_LOG_READY) {
    PushPendingLogsToPersistentStorage();
    // Persisting logs closes the current log, so start recording a new log
    // immediately to capture any background work that might be done before the
    // process is killed.
    OpenNewLog();
  }
}

void MetricsService::OnAppEnterForeground() {
  PrefService* pref = g_browser_process->local_state();
  pref->SetBoolean(prefs::kStabilityExitedCleanly, false);

  StartSchedulerIfNecessary();
}
#else
void MetricsService::LogNeedForCleanShutdown() {
  PrefService* pref = g_browser_process->local_state();
  pref->SetBoolean(prefs::kStabilityExitedCleanly, false);
  // Redundant setting to be sure we call for a clean shutdown.
  clean_shutdown_status_ = NEED_TO_SHUTDOWN;
}
#endif  // defined(OS_ANDROID) || defined(OS_IOS)

void MetricsService::RecordBreakpadRegistration(bool success) {
  if (!success)
    IncrementPrefValue(prefs::kStabilityBreakpadRegistrationFail);
  else
    IncrementPrefValue(prefs::kStabilityBreakpadRegistrationSuccess);
}

void MetricsService::RecordBreakpadHasDebugger(bool has_debugger) {
  if (!has_debugger)
    IncrementPrefValue(prefs::kStabilityDebuggerNotPresent);
  else
    IncrementPrefValue(prefs::kStabilityDebuggerPresent);
}

//------------------------------------------------------------------------------
// private methods
//------------------------------------------------------------------------------


//------------------------------------------------------------------------------
// Initialization methods

void MetricsService::InitializeMetricsState() {
#if defined(OS_POSIX)
  network_stats_server_ = chrome_common_net::kEchoTestServerLocation;
  http_pipelining_test_server_ = chrome_common_net::kPipelineTestServerBaseUrl;
#else
  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  network_stats_server_ = dist->GetNetworkStatsServer();
  http_pipelining_test_server_ = dist->GetHttpPipeliningTestServer();
#endif

  PrefService* pref = g_browser_process->local_state();
  DCHECK(pref);

  if ((pref->GetInt64(prefs::kStabilityStatsBuildTime)
       != MetricsLog::GetBuildTime()) ||
      (pref->GetString(prefs::kStabilityStatsVersion)
       != MetricsLog::GetVersionString())) {
    // This is a new version, so we don't want to confuse the stats about the
    // old version with info that we upload.
    DiscardOldStabilityStats(pref);
    pref->SetString(prefs::kStabilityStatsVersion,
                    MetricsLog::GetVersionString());
    pref->SetInt64(prefs::kStabilityStatsBuildTime,
                   MetricsLog::GetBuildTime());
  }

  // Update session ID
  session_id_ = pref->GetInteger(prefs::kMetricsSessionID);
  ++session_id_;
  pref->SetInteger(prefs::kMetricsSessionID, session_id_);

  // Stability bookkeeping
  IncrementPrefValue(prefs::kStabilityLaunchCount);

  if (!pref->GetBoolean(prefs::kStabilityExitedCleanly)) {
    IncrementPrefValue(prefs::kStabilityCrashCount);
    // Reset flag, and wait until we call LogNeedForCleanShutdown() before
    // monitoring.
    pref->SetBoolean(prefs::kStabilityExitedCleanly, true);
  }

  if (!pref->GetBoolean(prefs::kStabilitySessionEndCompleted)) {
    IncrementPrefValue(prefs::kStabilityIncompleteSessionEndCount);
    // This is marked false when we get a WM_ENDSESSION.
    pref->SetBoolean(prefs::kStabilitySessionEndCompleted, true);
  }

  // Initialize uptime counters.
  int64 startup_uptime = MetricsLog::GetIncrementalUptime(pref);
  DCHECK_EQ(0, startup_uptime);
  // For backwards compatibility, leave this intact in case Omaha is checking
  // them.  prefs::kStabilityLastTimestampSec may also be useless now.
  // TODO(jar): Delete these if they have no uses.
  pref->SetInt64(prefs::kStabilityLaunchTimeSec, Time::Now().ToTimeT());

  // Bookkeeping for the uninstall metrics.
  IncrementLongPrefsValue(prefs::kUninstallLaunchCount);

  // Get stats on use of command line.
  const CommandLine* command_line(CommandLine::ForCurrentProcess());
  size_t common_commands = 0;
  if (command_line->HasSwitch(switches::kUserDataDir)) {
    ++common_commands;
    UMA_HISTOGRAM_COUNTS_100("Chrome.CommandLineDatDirCount", 1);
  }

  if (command_line->HasSwitch(switches::kApp)) {
    ++common_commands;
    UMA_HISTOGRAM_COUNTS_100("Chrome.CommandLineAppModeCount", 1);
  }

  size_t switch_count = command_line->GetSwitches().size();
  UMA_HISTOGRAM_COUNTS_100("Chrome.CommandLineFlagCount", switch_count);
  UMA_HISTOGRAM_COUNTS_100("Chrome.CommandLineUncommonFlagCount",
                           switch_count - common_commands);

  // Kick off the process of saving the state (so the uptime numbers keep
  // getting updated) every n minutes.
  ScheduleNextStateSave();
}

// static
void MetricsService::InitTaskGetHardwareClass(
    base::WeakPtr<MetricsService> self,
    base::MessageLoopProxy* target_loop) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  std::string hardware_class;
#if defined(OS_CHROMEOS)
  chromeos::system::StatisticsProvider::GetInstance()->GetMachineStatistic(
      "hardware_class", &hardware_class);
#endif  // OS_CHROMEOS

  target_loop->PostTask(FROM_HERE,
      base::Bind(&MetricsService::OnInitTaskGotHardwareClass,
          self, hardware_class));
}

void MetricsService::OnInitTaskGotHardwareClass(
    const std::string& hardware_class) {
  DCHECK_EQ(INIT_TASK_SCHEDULED, state_);
  hardware_class_ = hardware_class;

#if defined(ENABLE_PLUGINS)
  // Start the next part of the init task: loading plugin information.
  PluginService::GetInstance()->GetPlugins(
      base::Bind(&MetricsService::OnInitTaskGotPluginInfo,
          self_ptr_factory_.GetWeakPtr()));
#else
  std::vector<content::WebPluginInfo> plugin_list_empty;
  OnInitTaskGotPluginInfo(plugin_list_empty);
#endif  // defined(ENABLE_PLUGINS)
}

void MetricsService::OnInitTaskGotPluginInfo(
    const std::vector<content::WebPluginInfo>& plugins) {
  DCHECK_EQ(INIT_TASK_SCHEDULED, state_);
  plugins_ = plugins;

  // Schedules a task on a blocking pool thread to gather Google Update
  // statistics (requires Registry reads).
  BrowserThread::PostBlockingPoolTask(
      FROM_HERE,
      base::Bind(&MetricsService::InitTaskGetGoogleUpdateData,
                 self_ptr_factory_.GetWeakPtr(),
                 base::MessageLoop::current()->message_loop_proxy()));
}

// static
void MetricsService::InitTaskGetGoogleUpdateData(
    base::WeakPtr<MetricsService> self,
    base::MessageLoopProxy* target_loop) {
  GoogleUpdateMetrics google_update_metrics;

#if defined(OS_WIN) && defined(GOOGLE_CHROME_BUILD)
  const bool system_install = GoogleUpdateSettings::IsSystemInstall();

  google_update_metrics.is_system_install = system_install;
  google_update_metrics.last_started_au =
      GoogleUpdateSettings::GetGoogleUpdateLastStartedAU(system_install);
  google_update_metrics.last_checked =
      GoogleUpdateSettings::GetGoogleUpdateLastChecked(system_install);
  GoogleUpdateSettings::GetUpdateDetailForGoogleUpdate(
      system_install,
      &google_update_metrics.google_update_data);
  GoogleUpdateSettings::GetUpdateDetail(
      system_install,
      &google_update_metrics.product_data);
#endif  // defined(OS_WIN) && defined(GOOGLE_CHROME_BUILD)

  target_loop->PostTask(FROM_HERE,
      base::Bind(&MetricsService::OnInitTaskGotGoogleUpdateData,
          self, google_update_metrics));
}

void MetricsService::OnInitTaskGotGoogleUpdateData(
    const GoogleUpdateMetrics& google_update_metrics) {
  DCHECK_EQ(INIT_TASK_SCHEDULED, state_);

  google_update_metrics_ = google_update_metrics;

  // Start the next part of the init task: fetching performance data.  This will
  // call into |FinishedReceivingProfilerData()| when the task completes.
  chrome_browser_metrics::TrackingSynchronizer::FetchProfilerDataAsynchronously(
      self_ptr_factory_.GetWeakPtr());
}

void MetricsService::OnUserAction(const std::string& action) {
  if (!CanLogNotification())
    return;

  log_manager_.current_log()->RecordUserAction(action.c_str());
  HandleIdleSinceLastTransmission(false);
}

void MetricsService::ReceivedProfilerData(
    const tracked_objects::ProcessDataSnapshot& process_data,
    int process_type) {
  DCHECK_EQ(INIT_TASK_SCHEDULED, state_);

  // Upon the first callback, create the initial log so that we can immediately
  // save the profiler data.
  if (!initial_log_.get())
    initial_log_.reset(new MetricsLog(client_id_, session_id_));

  initial_log_->RecordProfilerData(process_data, process_type);
}

void MetricsService::FinishedReceivingProfilerData() {
  DCHECK_EQ(INIT_TASK_SCHEDULED, state_);
    state_ = INIT_TASK_DONE;
}

int MetricsService::GetLowEntropySource() {
  // Note that the default value for the low entropy source and the default pref
  // value are both kLowEntropySourceNotSet, which is used to identify if the
  // value has been set or not.
  if (low_entropy_source_ != kLowEntropySourceNotSet)
    return low_entropy_source_;

  PrefService* local_state = g_browser_process->local_state();
  const CommandLine* command_line(CommandLine::ForCurrentProcess());
  // Only try to load the value from prefs if the user did not request a reset.
  // Otherwise, skip to generating a new value.
  if (!command_line->HasSwitch(switches::kResetVariationState)) {
    int value = local_state->GetInteger(prefs::kMetricsLowEntropySource);
    // Old versions of the code would generate values in the range of [1, 8192],
    // before the range was switched to [0, 8191] and then to [0, 7999]. Map
    // 8192 to 0, so that the 0th bucket remains uniform, while re-generating
    // the low entropy source for old values in the [8000, 8191] range.
    if (value == 8192)
      value = 0;
    // If the value is outside the [0, kMaxLowEntropySize) range, re-generate
    // it below.
    if (value >= 0 && value < kMaxLowEntropySize) {
      low_entropy_source_ = value;
      UMA_HISTOGRAM_BOOLEAN("UMA.GeneratedLowEntropySource", false);
      return low_entropy_source_;
    }
  }

  UMA_HISTOGRAM_BOOLEAN("UMA.GeneratedLowEntropySource", true);
  low_entropy_source_ = GenerateLowEntropySource();
  local_state->SetInteger(prefs::kMetricsLowEntropySource, low_entropy_source_);
  metrics::CachingPermutedEntropyProvider::ClearCache(local_state);

  return low_entropy_source_;
}

// static
std::string MetricsService::GenerateClientID() {
  return base::GenerateGUID();
}

//------------------------------------------------------------------------------
// State save methods

void MetricsService::ScheduleNextStateSave() {
  state_saver_factory_.InvalidateWeakPtrs();

  base::MessageLoop::current()->PostDelayedTask(FROM_HERE,
      base::Bind(&MetricsService::SaveLocalState,
                 state_saver_factory_.GetWeakPtr()),
      base::TimeDelta::FromMinutes(kSaveStateIntervalMinutes));
}

void MetricsService::SaveLocalState() {
  PrefService* pref = g_browser_process->local_state();
  if (!pref) {
    NOTREACHED();
    return;
  }

  RecordCurrentState(pref);

  // TODO(jar):110021 Does this run down the batteries????
  ScheduleNextStateSave();
}


//------------------------------------------------------------------------------
// Recording control methods

void MetricsService::OpenNewLog() {
  DCHECK(!log_manager_.current_log());

  log_manager_.BeginLoggingWithLog(new MetricsLog(client_id_, session_id_),
                                   MetricsLogManager::ONGOING_LOG);
  if (state_ == INITIALIZED) {
    // We only need to schedule that run once.
    state_ = INIT_TASK_SCHEDULED;

    // Schedules a task on the file thread for execution of slower
    // initialization steps (such as plugin list generation) necessary
    // for sending the initial log.  This avoids blocking the main UI
    // thread.
    BrowserThread::PostDelayedTask(
        BrowserThread::FILE,
        FROM_HERE,
        base::Bind(&MetricsService::InitTaskGetHardwareClass,
            self_ptr_factory_.GetWeakPtr(),
            base::MessageLoop::current()->message_loop_proxy()),
        base::TimeDelta::FromSeconds(kInitializationDelaySeconds));
  }
}

void MetricsService::CloseCurrentLog() {
  if (!log_manager_.current_log())
    return;

  // TODO(jar): Integrate bounds on log recording more consistently, so that we
  // can stop recording logs that are too big much sooner.
  if (log_manager_.current_log()->num_events() > kEventLimit) {
    UMA_HISTOGRAM_COUNTS("UMA.Discarded Log Events",
                         log_manager_.current_log()->num_events());
    log_manager_.DiscardCurrentLog();
    OpenNewLog();  // Start trivial log to hold our histograms.
  }

  // Adds to ongoing logs.
  log_manager_.current_log()->set_hardware_class(hardware_class_);

  // Put incremental data (histogram deltas, and realtime stats deltas) at the
  // end of all log transmissions (initial log handles this separately).
  // RecordIncrementalStabilityElements only exists on the derived
  // MetricsLog class.
  MetricsLog* current_log =
      static_cast<MetricsLog*>(log_manager_.current_log());
  DCHECK(current_log);
  current_log->RecordEnvironmentProto(plugins_, google_update_metrics_);
  current_log->RecordIncrementalStabilityElements(plugins_);
  RecordCurrentHistograms();

  log_manager_.FinishCurrentLog();
}

void MetricsService::PushPendingLogsToPersistentStorage() {
  if (state_ < INITIAL_LOG_READY)
    return;  // We didn't and still don't have time to get plugin list etc.

  if (log_manager_.has_staged_log()) {
    // We may race here, and send second copy of the log later.
    MetricsLogManager::StoreType store_type;
    if (current_fetch_.get())
      store_type = MetricsLogManager::PROVISIONAL_STORE;
    else
      store_type = MetricsLogManager::NORMAL_STORE;
    log_manager_.StoreStagedLogAsUnsent(store_type);
  }
  DCHECK(!log_manager_.has_staged_log());
  CloseCurrentLog();
  StoreUnsentLogs();

  // If there was a staged and/or current log, then there is now at least one
  // log waiting to be uploaded.
  if (log_manager_.has_unsent_logs())
    state_ = SENDING_OLD_LOGS;
}

//------------------------------------------------------------------------------
// Transmission of logs methods

void MetricsService::StartSchedulerIfNecessary() {
  // Never schedule cutting or uploading of logs in test mode.
  if (test_mode_active_)
    return;

  // Even if reporting is disabled, the scheduler is needed to trigger the
  // creation of the initial log, which must be done in order for any logs to be
  // persisted on shutdown or backgrounding.
  if (recording_active() && (reporting_active() || state_ < INITIAL_LOG_READY))
    scheduler_->Start();
}

void MetricsService::StartScheduledUpload() {
  // If we're getting no notifications, then the log won't have much in it, and
  // it's possible the computer is about to go to sleep, so don't upload and
  // stop the scheduler.
  // If recording has been turned off, the scheduler doesn't need to run.
  // If reporting is off, proceed if the initial log hasn't been created, since
  // that has to happen in order for logs to be cut and stored when persisting.
  // TODO(stuartmorgan): Call Stop() on the schedule when reporting and/or
  // recording are turned off instead of letting it fire and then aborting.
  if (idle_since_last_transmission_ ||
      !recording_active() ||
      (!reporting_active() && state_ >= INITIAL_LOG_READY)) {
    scheduler_->Stop();
    scheduler_->UploadCancelled();
    return;
  }

  // If the callback was to upload an old log, but there no longer is one,
  // just report success back to the scheduler to begin the ongoing log
  // callbacks.
  // TODO(stuartmorgan): Consider removing the distinction between
  // SENDING_OLD_LOGS and SENDING_CURRENT_LOGS to simplify the state machine
  // now that the log upload flow is the same for both modes.
  if (state_ == SENDING_OLD_LOGS && !log_manager_.has_unsent_logs()) {
    state_ = SENDING_CURRENT_LOGS;
    scheduler_->UploadFinished(true /* healthy */, false /* no unsent logs */);
    return;
  }
  // If there are unsent logs, send the next one. If not, start the asynchronous
  // process of finalizing the current log for upload.
  if (state_ == SENDING_OLD_LOGS) {
    DCHECK(log_manager_.has_unsent_logs());
    log_manager_.StageNextLogForUpload();
    SendStagedLog();
  } else {
    StartFinalLogInfoCollection();
  }
}

void MetricsService::StartFinalLogInfoCollection() {
  // Begin the multi-step process of collecting memory usage histograms:
  // First spawn a task to collect the memory details; when that task is
  // finished, it will call OnMemoryDetailCollectionDone. That will in turn
  // call HistogramSynchronization to collect histograms from all renderers and
  // then call OnHistogramSynchronizationDone to continue processing.
  DCHECK(!waiting_for_asynchronous_reporting_step_);
  waiting_for_asynchronous_reporting_step_ = true;

  base::Closure callback =
      base::Bind(&MetricsService::OnMemoryDetailCollectionDone,
                 self_ptr_factory_.GetWeakPtr());

  scoped_refptr<MetricsMemoryDetails> details(
      new MetricsMemoryDetails(callback));
  details->StartFetch(MemoryDetails::UPDATE_USER_METRICS);

  // Collect WebCore cache information to put into a histogram.
  for (content::RenderProcessHost::iterator i(
          content::RenderProcessHost::AllHostsIterator());
       !i.IsAtEnd(); i.Advance())
    i.GetCurrentValue()->Send(new ChromeViewMsg_GetCacheResourceStats());
}

void MetricsService::OnMemoryDetailCollectionDone() {
  DCHECK(IsSingleThreaded());
  // This function should only be called as the callback from an ansynchronous
  // step.
  DCHECK(waiting_for_asynchronous_reporting_step_);

  // Create a callback_task for OnHistogramSynchronizationDone.
  base::Closure callback = base::Bind(
      &MetricsService::OnHistogramSynchronizationDone,
      self_ptr_factory_.GetWeakPtr());

  // Set up the callback to task to call after we receive histograms from all
  // child processes. Wait time specifies how long to wait before absolutely
  // calling us back on the task.
  content::FetchHistogramsAsynchronously(
      base::MessageLoop::current(), callback,
      base::TimeDelta::FromMilliseconds(kMaxHistogramGatheringWaitDuration));
}

void MetricsService::OnHistogramSynchronizationDone() {
  DCHECK(IsSingleThreaded());
  // This function should only be called as the callback from an ansynchronous
  // step.
  DCHECK(waiting_for_asynchronous_reporting_step_);

  waiting_for_asynchronous_reporting_step_ = false;
  OnFinalLogInfoCollectionDone();
}

void MetricsService::OnFinalLogInfoCollectionDone() {
  // If somehow there is a fetch in progress, we return and hope things work
  // out. The scheduler isn't informed since if this happens, the scheduler
  // will get a response from the upload.
  DCHECK(!current_fetch_.get());
  if (current_fetch_.get())
    return;

  // Abort if metrics were turned off during the final info gathering.
  if (!recording_active()) {
    scheduler_->Stop();
    scheduler_->UploadCancelled();
    return;
  }

  StageNewLog();

  // If logs shouldn't be uploaded, stop here. It's important that this check
  // be after StageNewLog(), otherwise the previous logs will never be loaded,
  // and thus the open log won't be persisted.
  // TODO(stuartmorgan): This is unnecessarily complicated; restructure loading
  // of previous logs to not require running part of the upload logic.
  // http://crbug.com/157337
  if (!reporting_active()) {
    scheduler_->Stop();
    scheduler_->UploadCancelled();
    return;
  }

  SendStagedLog();
}

void MetricsService::StageNewLog() {
  if (log_manager_.has_staged_log())
    return;

  switch (state_) {
    case INITIALIZED:
    case INIT_TASK_SCHEDULED:  // We should be further along by now.
      NOTREACHED();
      return;

    case INIT_TASK_DONE:
      PrepareInitialLog();
      DCHECK_EQ(INIT_TASK_DONE, state_);
      log_manager_.LoadPersistedUnsentLogs();
      state_ = INITIAL_LOG_READY;
      break;

    case SENDING_OLD_LOGS:
      NOTREACHED();  // Shouldn't be staging a new log during old log sending.
      return;

    case SENDING_CURRENT_LOGS:
      CloseCurrentLog();
      OpenNewLog();
      log_manager_.StageNextLogForUpload();
      break;

    default:
      NOTREACHED();
      return;
  }

  DCHECK(log_manager_.has_staged_log());
}

void MetricsService::PrepareInitialLog() {
  DCHECK_EQ(INIT_TASK_DONE, state_);

  DCHECK(initial_log_.get());
  initial_log_->set_hardware_class(hardware_class_);
  initial_log_->RecordEnvironment(plugins_, google_update_metrics_);

  // Histograms only get written to the current log, so make the new log current
  // before writing them.
  log_manager_.PauseCurrentLog();
  log_manager_.BeginLoggingWithLog(initial_log_.release(),
                                   MetricsLogManager::INITIAL_LOG);
  RecordCurrentHistograms();
  log_manager_.FinishCurrentLog();
  log_manager_.ResumePausedLog();

  DCHECK(!log_manager_.has_staged_log());
  log_manager_.StageNextLogForUpload();
}

void MetricsService::StoreUnsentLogs() {
  if (state_ < INITIAL_LOG_READY)
    return;  // We never Recalled the prior unsent logs.

  log_manager_.PersistUnsentLogs();
}

void MetricsService::SendStagedLog() {
  DCHECK(log_manager_.has_staged_log());

  PrepareFetchWithStagedLog();

  bool upload_created = (current_fetch_.get() != NULL);
  UMA_HISTOGRAM_BOOLEAN("UMA.UploadCreation", upload_created);
  if (!upload_created) {
    // Compression failed, and log discarded :-/.
    // Skip this upload and hope things work out next time.
    log_manager_.DiscardStagedLog();
    scheduler_->UploadCancelled();
    return;
  }

  DCHECK(!waiting_for_asynchronous_reporting_step_);
  waiting_for_asynchronous_reporting_step_ = true;

  current_fetch_->Start();

  HandleIdleSinceLastTransmission(true);
}

void MetricsService::PrepareFetchWithStagedLog() {
  DCHECK(log_manager_.has_staged_log());

  // Prepare the protobuf version.
  DCHECK(!current_fetch_.get());
  if (log_manager_.has_staged_log()) {
    current_fetch_.reset(net::URLFetcher::Create(
        GURL(kServerUrl), net::URLFetcher::POST, this));
    current_fetch_->SetRequestContext(
        g_browser_process->system_request_context());

    // Compress the protobufs 50% of the time. This can be used to see if
    // compressed protobufs are being mishandled by machines between the
    // client/server or monitoring programs/firewalls on the client.
    bool gzip_protobuf_before_uploading =
        metrics::ShouldGzipProtobufsBeforeUploading();
    if (gzip_protobuf_before_uploading) {
      std::string log_text = log_manager_.staged_log_text();
      std::string compressed_log_text;
      bool compression_successful = chrome::GzipCompress(log_text,
                                                         &compressed_log_text);
      DCHECK(compression_successful);
      if (compression_successful) {
        current_fetch_->SetUploadData(kMimeType, compressed_log_text);
        // Tell the server that we're uploading gzipped protobufs.
        current_fetch_->SetExtraRequestHeaders("content-encoding: gzip");
        UMA_HISTOGRAM_PERCENTAGE(
            "UMA.ProtoCompressionRatio",
            100 * compressed_log_text.size() / log_text.size());
        UMA_HISTOGRAM_CUSTOM_COUNTS(
            "UMA.ProtoGzippedKBSaved",
            (log_text.size() - compressed_log_text.size()) / 1024,
            1, 2000, 50);
      }
    } else {
      current_fetch_->SetUploadData(kMimeType, log_manager_.staged_log_text());
    }
    UMA_HISTOGRAM_BOOLEAN("UMA.ProtoGzipped",
                          gzip_protobuf_before_uploading);

    // We already drop cookies server-side, but we might as well strip them out
    // client-side as well.
    current_fetch_->SetLoadFlags(net::LOAD_DO_NOT_SAVE_COOKIES |
                                 net::LOAD_DO_NOT_SEND_COOKIES);
  }
}

void MetricsService::OnURLFetchComplete(const net::URLFetcher* source) {
  DCHECK(waiting_for_asynchronous_reporting_step_);

  // We're not allowed to re-use the existing |URLFetcher|s, so free them here.
  // Note however that |source| is aliased to the fetcher, so we should be
  // careful not to delete it too early.
  DCHECK_EQ(current_fetch_.get(), source);
  scoped_ptr<net::URLFetcher> s(current_fetch_.Pass());

  int response_code = source->GetResponseCode();

  // Log a histogram to track response success vs. failure rates.
  UMA_HISTOGRAM_ENUMERATION("UMA.UploadResponseStatus.Protobuf",
                            ResponseCodeToStatus(response_code),
                            NUM_RESPONSE_STATUSES);

  // If the upload was provisionally stored, drop it now that the upload is
  // known to have gone through.
  log_manager_.DiscardLastProvisionalStore();

  bool upload_succeeded = response_code == 200;

  // Provide boolean for error recovery (allow us to ignore response_code).
  bool discard_log = false;
  const size_t log_size = log_manager_.staged_log_text().length();
  if (!upload_succeeded && log_size > kUploadLogAvoidRetransmitSize) {
    UMA_HISTOGRAM_COUNTS("UMA.Large Rejected Log was Discarded",
                         static_cast<int>(log_size));
    discard_log = true;
  } else if (response_code == 400) {
    // Bad syntax.  Retransmission won't work.
    discard_log = true;
  }

  if (upload_succeeded || discard_log)
    log_manager_.DiscardStagedLog();

  waiting_for_asynchronous_reporting_step_ = false;

  if (!log_manager_.has_staged_log()) {
    switch (state_) {
      case INITIAL_LOG_READY:
        state_ = log_manager_.has_unsent_logs() ? SENDING_OLD_LOGS
                                                : SENDING_CURRENT_LOGS;
        break;

      case SENDING_OLD_LOGS:
        // Store the updated list to disk now that the removed log is uploaded.
        StoreUnsentLogs();
        if (!log_manager_.has_unsent_logs())
          state_ = SENDING_CURRENT_LOGS;
        break;

      case SENDING_CURRENT_LOGS:
        break;

      default:
        NOTREACHED();
        break;
    }

    if (log_manager_.has_unsent_logs())
      DCHECK_LT(state_, SENDING_CURRENT_LOGS);
  }

  // Error 400 indicates a problem with the log, not with the server, so
  // don't consider that a sign that the server is in trouble.
  bool server_is_healthy = upload_succeeded || response_code == 400;
  scheduler_->UploadFinished(server_is_healthy, log_manager_.has_unsent_logs());

  // Collect network stats if UMA upload succeeded.
  IOThread* io_thread = g_browser_process->io_thread();
  if (server_is_healthy && io_thread) {
    chrome_browser_net::CollectNetworkStats(network_stats_server_, io_thread);
    chrome_browser_net::CollectPipeliningCapabilityStatsOnUIThread(
        http_pipelining_test_server_, io_thread);
#if defined(OS_WIN)
    chrome::CollectTimeTicksStats();
#endif
  }
}

void MetricsService::IncrementPrefValue(const char* path) {
  PrefService* pref = g_browser_process->local_state();
  DCHECK(pref);
  int value = pref->GetInteger(path);
  pref->SetInteger(path, value + 1);
}

void MetricsService::IncrementLongPrefsValue(const char* path) {
  PrefService* pref = g_browser_process->local_state();
  DCHECK(pref);
  int64 value = pref->GetInt64(path);
  pref->SetInt64(path, value + 1);
}

void MetricsService::LogLoadStarted(content::WebContents* web_contents) {
  content::RecordAction(content::UserMetricsAction("PageLoad"));
  HISTOGRAM_ENUMERATION("Chrome.UmaPageloadCounter", 1, 2);
  IncrementPrefValue(prefs::kStabilityPageLoadCount);
  IncrementLongPrefsValue(prefs::kUninstallMetricsPageLoadCount);
  // We need to save the prefs, as page load count is a critical stat, and it
  // might be lost due to a crash :-(.
}

void MetricsService::LogRendererCrash(content::RenderProcessHost* host,
                                      base::TerminationStatus status,
                                      int exit_code) {
  Profile* profile = Profile::FromBrowserContext(host->GetBrowserContext());
  ExtensionService* service = profile->GetExtensionService();
  bool was_extension_process =
      service && service->process_map()->Contains(host->GetID());
  if (status == base::TERMINATION_STATUS_PROCESS_CRASHED ||
      status == base::TERMINATION_STATUS_ABNORMAL_TERMINATION) {
    if (was_extension_process) {
      IncrementPrefValue(prefs::kStabilityExtensionRendererCrashCount);

      UMA_HISTOGRAM_SPARSE_SLOWLY("CrashExitCodes.Extension",
                                  MapCrashExitCodeForHistogram(exit_code));
    } else {
      IncrementPrefValue(prefs::kStabilityRendererCrashCount);

      UMA_HISTOGRAM_SPARSE_SLOWLY("CrashExitCodes.Renderer",
                                  MapCrashExitCodeForHistogram(exit_code));
    }

    UMA_HISTOGRAM_PERCENTAGE("BrowserRenderProcessHost.ChildCrashes",
                             was_extension_process ? 2 : 1);
  } else if (status == base::TERMINATION_STATUS_PROCESS_WAS_KILLED) {
    UMA_HISTOGRAM_PERCENTAGE("BrowserRenderProcessHost.ChildKills",
                             was_extension_process ? 2 : 1);
  } else if (status == base::TERMINATION_STATUS_STILL_RUNNING) {
    UMA_HISTOGRAM_PERCENTAGE("BrowserRenderProcessHost.DisconnectedAlive",
                               was_extension_process ? 2 : 1);
    }
  }

void MetricsService::LogRendererHang() {
  IncrementPrefValue(prefs::kStabilityRendererHangCount);
}

bool MetricsService::UmaMetricsProperlyShutdown() {
  CHECK(clean_shutdown_status_ == CLEANLY_SHUTDOWN ||
        clean_shutdown_status_ == NEED_TO_SHUTDOWN);
  return clean_shutdown_status_ == CLEANLY_SHUTDOWN;
}

void MetricsService::LogCleanShutdown() {
  // Redundant hack to write pref ASAP.
  MarkAppCleanShutdownAndCommit();

  // Redundant setting to assure that we always reset this value at shutdown
  // (and that we don't use some alternate path, and not call LogCleanShutdown).
  clean_shutdown_status_ = CLEANLY_SHUTDOWN;

  RecordBooleanPrefValue(prefs::kStabilityExitedCleanly, true);
}

#if defined(OS_CHROMEOS)
void MetricsService::LogChromeOSCrash(const std::string &crash_type) {
  if (crash_type == "user")
    IncrementPrefValue(prefs::kStabilityOtherUserCrashCount);
  else if (crash_type == "kernel")
    IncrementPrefValue(prefs::kStabilityKernelCrashCount);
  else if (crash_type == "uncleanshutdown")
    IncrementPrefValue(prefs::kStabilitySystemUncleanShutdownCount);
  else
    NOTREACHED() << "Unexpected Chrome OS crash type " << crash_type;
  // Wake up metrics logs sending if necessary now that new
  // log data is available.
  HandleIdleSinceLastTransmission(false);
}
#endif  // OS_CHROMEOS

void MetricsService::LogPluginLoadingError(const base::FilePath& plugin_path) {
  content::WebPluginInfo plugin;
  bool success =
      content::PluginService::GetInstance()->GetPluginInfoByPath(plugin_path,
                                                                 &plugin);
  DCHECK(success);
  ChildProcessStats& stats = child_process_stats_buffer_[plugin.name];
  // Initialize the type if this entry is new.
  if (stats.process_type == content::PROCESS_TYPE_UNKNOWN) {
    // The plug-in process might not actually of type PLUGIN (which means
    // NPAPI), but we only care that it is *a* plug-in process.
    stats.process_type = content::PROCESS_TYPE_PLUGIN;
  } else {
    DCHECK(IsPluginProcess(stats.process_type));
  }
  stats.loading_errors++;
}

MetricsService::ChildProcessStats& MetricsService::GetChildProcessStats(
    const content::ChildProcessData& data) {
  const string16& child_name = data.name;
  if (!ContainsKey(child_process_stats_buffer_, child_name)) {
    child_process_stats_buffer_[child_name] =
        ChildProcessStats(data.process_type);
  }
  return child_process_stats_buffer_[child_name];
}

void MetricsService::RecordPluginChanges(PrefService* pref) {
  ListPrefUpdate update(pref, prefs::kStabilityPluginStats);
  ListValue* plugins = update.Get();
  DCHECK(plugins);

  for (ListValue::iterator value_iter = plugins->begin();
       value_iter != plugins->end(); ++value_iter) {
    if (!(*value_iter)->IsType(Value::TYPE_DICTIONARY)) {
      NOTREACHED();
      continue;
    }

    DictionaryValue* plugin_dict = static_cast<DictionaryValue*>(*value_iter);
    std::string plugin_name;
    plugin_dict->GetString(prefs::kStabilityPluginName, &plugin_name);
    if (plugin_name.empty()) {
      NOTREACHED();
      continue;
    }

    // TODO(viettrungluu): remove conversions
    string16 name16 = UTF8ToUTF16(plugin_name);
    if (child_process_stats_buffer_.find(name16) ==
        child_process_stats_buffer_.end()) {
      continue;
    }

    ChildProcessStats stats = child_process_stats_buffer_[name16];
    if (stats.process_launches) {
      int launches = 0;
      plugin_dict->GetInteger(prefs::kStabilityPluginLaunches, &launches);
      launches += stats.process_launches;
      plugin_dict->SetInteger(prefs::kStabilityPluginLaunches, launches);
    }
    if (stats.process_crashes) {
      int crashes = 0;
      plugin_dict->GetInteger(prefs::kStabilityPluginCrashes, &crashes);
      crashes += stats.process_crashes;
      plugin_dict->SetInteger(prefs::kStabilityPluginCrashes, crashes);
    }
    if (stats.instances) {
      int instances = 0;
      plugin_dict->GetInteger(prefs::kStabilityPluginInstances, &instances);
      instances += stats.instances;
      plugin_dict->SetInteger(prefs::kStabilityPluginInstances, instances);
    }
    if (stats.loading_errors) {
      int loading_errors = 0;
      plugin_dict->GetInteger(prefs::kStabilityPluginLoadingErrors,
                              &loading_errors);
      loading_errors += stats.loading_errors;
      plugin_dict->SetInteger(prefs::kStabilityPluginLoadingErrors,
                              loading_errors);
    }

    child_process_stats_buffer_.erase(name16);
  }

  // Now go through and add dictionaries for plugins that didn't already have
  // reports in Local State.
  for (std::map<string16, ChildProcessStats>::iterator cache_iter =
           child_process_stats_buffer_.begin();
       cache_iter != child_process_stats_buffer_.end(); ++cache_iter) {
    ChildProcessStats stats = cache_iter->second;

    // Insert only plugins information into the plugins list.
    if (!IsPluginProcess(stats.process_type))
      continue;

    // TODO(viettrungluu): remove conversion
    std::string plugin_name = UTF16ToUTF8(cache_iter->first);

    DictionaryValue* plugin_dict = new DictionaryValue;

    plugin_dict->SetString(prefs::kStabilityPluginName, plugin_name);
    plugin_dict->SetInteger(prefs::kStabilityPluginLaunches,
                            stats.process_launches);
    plugin_dict->SetInteger(prefs::kStabilityPluginCrashes,
                            stats.process_crashes);
    plugin_dict->SetInteger(prefs::kStabilityPluginInstances,
                            stats.instances);
    plugin_dict->SetInteger(prefs::kStabilityPluginLoadingErrors,
                            stats.loading_errors);
    plugins->Append(plugin_dict);
  }
  child_process_stats_buffer_.clear();
}

bool MetricsService::CanLogNotification() {
  // We simply don't log anything to UMA if there is a single incognito
  // session visible. The problem is that we always notify using the orginal
  // profile in order to simplify notification processing.
  return !chrome::IsOffTheRecordSessionActive();
}

void MetricsService::RecordBooleanPrefValue(const char* path, bool value) {
  DCHECK(IsSingleThreaded());

  PrefService* pref = g_browser_process->local_state();
  DCHECK(pref);

  pref->SetBoolean(path, value);
  RecordCurrentState(pref);
}

void MetricsService::RecordCurrentState(PrefService* pref) {
  pref->SetInt64(prefs::kStabilityLastTimestampSec, Time::Now().ToTimeT());

  RecordPluginChanges(pref);
}

// static
bool MetricsService::IsPluginProcess(int process_type) {
  return (process_type == content::PROCESS_TYPE_PLUGIN ||
          process_type == content::PROCESS_TYPE_PPAPI_PLUGIN ||
          process_type == content::PROCESS_TYPE_PPAPI_BROKER);
}

#if defined(OS_CHROMEOS)
void MetricsService::StartExternalMetrics() {
  external_metrics_ = new chromeos::ExternalMetrics;
  external_metrics_->Start();
}
#endif

// static
bool MetricsServiceHelper::IsMetricsReportingEnabled() {
  bool result = false;
  const PrefService* local_state = g_browser_process->local_state();
  if (local_state) {
    const PrefService::Preference* uma_pref =
        local_state->FindPreference(prefs::kMetricsReportingEnabled);
    if (uma_pref) {
      bool success = uma_pref->GetValue()->GetAsBoolean(&result);
      DCHECK(success);
    }
  }
  return result;
}
