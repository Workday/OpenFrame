// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sessions/base_session_service.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/pickle.h"
#include "base/stl_util.h"
#include "base/threading/thread.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_backend.h"
#include "chrome/browser/sessions/session_types.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/common/referrer.h"

using content::BrowserThread;
using content::NavigationEntry;

// BaseSessionService ---------------------------------------------------------

namespace {

// Helper used by CreateUpdateTabNavigationCommand(). It writes |str| to
// |pickle|, if and only if |str| fits within (|max_bytes| - |*bytes_written|).
// |bytes_written| is incremented to reflect the data written.
void WriteStringToPickle(Pickle& pickle, int* bytes_written, int max_bytes,
                         const std::string& str) {
  int num_bytes = str.size() * sizeof(char);
  if (*bytes_written + num_bytes < max_bytes) {
    *bytes_written += num_bytes;
    pickle.WriteString(str);
  } else {
    pickle.WriteString(std::string());
  }
}

// Helper used by ScheduleGetLastSessionCommands. It runs callback on TaskRunner
// thread if it's not canceled.
void RunIfNotCanceled(
    const CancelableTaskTracker::IsCanceledCallback& is_canceled,
    const BaseSessionService::InternalGetCommandsCallback& callback,
    ScopedVector<SessionCommand> commands) {
  if (is_canceled.Run())
    return;
  callback.Run(commands.Pass());
}

void PostOrRunInternalGetCommandsCallback(
    base::TaskRunner* task_runner,
    const BaseSessionService::InternalGetCommandsCallback& callback,
    ScopedVector<SessionCommand> commands) {
  if (task_runner->RunsTasksOnCurrentThread()) {
    callback.Run(commands.Pass());
  } else {
    task_runner->PostTask(FROM_HERE,
                          base::Bind(callback, base::Passed(&commands)));
  }
}

}  // namespace

// Delay between when a command is received, and when we save it to the
// backend.
static const int kSaveDelayMS = 2500;

// static
const int BaseSessionService::max_persist_navigation_count = 6;

BaseSessionService::BaseSessionService(SessionType type,
                                       Profile* profile,
                                       const base::FilePath& path)
    : profile_(profile),
      weak_factory_(this),
      pending_reset_(false),
      commands_since_reset_(0),
      sequence_token_(
          content::BrowserThread::GetBlockingPool()->GetSequenceToken()) {
  if (profile) {
    // We should never be created when incognito.
    DCHECK(!profile->IsOffTheRecord());
  }
  backend_ = new SessionBackend(type, profile_ ? profile_->GetPath() : path);
  DCHECK(backend_.get());
}

BaseSessionService::~BaseSessionService() {
}

void BaseSessionService::DeleteLastSession() {
  RunTaskOnBackendThread(
      FROM_HERE,
      base::Bind(&SessionBackend::DeleteLastSession, backend()));
}

void BaseSessionService::ScheduleCommand(SessionCommand* command) {
  DCHECK(command);
  commands_since_reset_++;
  pending_commands_.push_back(command);
  StartSaveTimer();
}

void BaseSessionService::StartSaveTimer() {
  // Don't start a timer when testing (profile == NULL or
  // MessageLoop::current() is NULL).
  if (base::MessageLoop::current() && profile() &&
      !weak_factory_.HasWeakPtrs()) {
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&BaseSessionService::Save, weak_factory_.GetWeakPtr()),
        base::TimeDelta::FromMilliseconds(kSaveDelayMS));
  }
}

void BaseSessionService::Save() {
  DCHECK(backend());

  if (pending_commands_.empty())
    return;

  RunTaskOnBackendThread(
      FROM_HERE,
      base::Bind(&SessionBackend::AppendCommands, backend(),
                 new std::vector<SessionCommand*>(pending_commands_),
                 pending_reset_));

  // Backend took ownership of commands.
  pending_commands_.clear();

  if (pending_reset_) {
    commands_since_reset_ = 0;
    pending_reset_ = false;
  }
}

SessionCommand* BaseSessionService::CreateUpdateTabNavigationCommand(
    SessionID::id_type command_id,
    SessionID::id_type tab_id,
    const sessions::SerializedNavigationEntry& navigation) {
  // Use pickle to handle marshalling.
  Pickle pickle;
  pickle.WriteInt(tab_id);
  // We only allow navigations up to 63k (which should be completely
  // reasonable).
  static const size_t max_state_size =
      std::numeric_limits<SessionCommand::size_type>::max() - 1024;
  navigation.WriteToPickle(max_state_size, &pickle);
  return new SessionCommand(command_id, pickle);
}

SessionCommand* BaseSessionService::CreateSetTabExtensionAppIDCommand(
    SessionID::id_type command_id,
    SessionID::id_type tab_id,
    const std::string& extension_id) {
  // Use pickle to handle marshalling.
  Pickle pickle;
  pickle.WriteInt(tab_id);

  // Enforce a max for ids. They should never be anywhere near this size.
  static const SessionCommand::size_type max_id_size =
      std::numeric_limits<SessionCommand::size_type>::max() - 1024;

  int bytes_written = 0;

  WriteStringToPickle(pickle, &bytes_written, max_id_size, extension_id);

  return new SessionCommand(command_id, pickle);
}

SessionCommand* BaseSessionService::CreateSetTabUserAgentOverrideCommand(
    SessionID::id_type command_id,
    SessionID::id_type tab_id,
    const std::string& user_agent_override) {
  // Use pickle to handle marshalling.
  Pickle pickle;
  pickle.WriteInt(tab_id);

  // Enforce a max for the user agent length.  They should never be anywhere
  // near this size.
  static const SessionCommand::size_type max_user_agent_size =
      std::numeric_limits<SessionCommand::size_type>::max() - 1024;

  int bytes_written = 0;

  WriteStringToPickle(pickle, &bytes_written, max_user_agent_size,
      user_agent_override);

  return new SessionCommand(command_id, pickle);
}

SessionCommand* BaseSessionService::CreateSetWindowAppNameCommand(
    SessionID::id_type command_id,
    SessionID::id_type window_id,
    const std::string& app_name) {
  // Use pickle to handle marshalling.
  Pickle pickle;
  pickle.WriteInt(window_id);

  // Enforce a max for ids. They should never be anywhere near this size.
  static const SessionCommand::size_type max_id_size =
      std::numeric_limits<SessionCommand::size_type>::max() - 1024;

  int bytes_written = 0;

  WriteStringToPickle(pickle, &bytes_written, max_id_size, app_name);

  return new SessionCommand(command_id, pickle);
}

bool BaseSessionService::RestoreUpdateTabNavigationCommand(
    const SessionCommand& command,
    sessions::SerializedNavigationEntry* navigation,
    SessionID::id_type* tab_id) {
  scoped_ptr<Pickle> pickle(command.PayloadAsPickle());
  if (!pickle.get())
    return false;
  PickleIterator iterator(*pickle);
  return
      pickle->ReadInt(&iterator, tab_id) &&
      navigation->ReadFromPickle(&iterator);
}

bool BaseSessionService::RestoreSetTabExtensionAppIDCommand(
    const SessionCommand& command,
    SessionID::id_type* tab_id,
    std::string* extension_app_id) {
  scoped_ptr<Pickle> pickle(command.PayloadAsPickle());
  if (!pickle.get())
    return false;

  PickleIterator iterator(*pickle);
  return pickle->ReadInt(&iterator, tab_id) &&
      pickle->ReadString(&iterator, extension_app_id);
}

bool BaseSessionService::RestoreSetTabUserAgentOverrideCommand(
    const SessionCommand& command,
    SessionID::id_type* tab_id,
    std::string* user_agent_override) {
  scoped_ptr<Pickle> pickle(command.PayloadAsPickle());
  if (!pickle.get())
    return false;

  PickleIterator iterator(*pickle);
  return pickle->ReadInt(&iterator, tab_id) &&
      pickle->ReadString(&iterator, user_agent_override);
}

bool BaseSessionService::RestoreSetWindowAppNameCommand(
    const SessionCommand& command,
    SessionID::id_type* window_id,
    std::string* app_name) {
  scoped_ptr<Pickle> pickle(command.PayloadAsPickle());
  if (!pickle.get())
    return false;

  PickleIterator iterator(*pickle);
  return pickle->ReadInt(&iterator, window_id) &&
      pickle->ReadString(&iterator, app_name);
}

bool BaseSessionService::ShouldTrackEntry(const GURL& url) {
  return url.is_valid();
}

CancelableTaskTracker::TaskId
    BaseSessionService::ScheduleGetLastSessionCommands(
    const InternalGetCommandsCallback& callback,
    CancelableTaskTracker* tracker) {
  CancelableTaskTracker::IsCanceledCallback is_canceled;
  CancelableTaskTracker::TaskId id = tracker->NewTrackedTaskId(&is_canceled);

  InternalGetCommandsCallback run_if_not_canceled =
      base::Bind(&RunIfNotCanceled, is_canceled, callback);

  InternalGetCommandsCallback callback_runner =
      base::Bind(&PostOrRunInternalGetCommandsCallback,
                 base::MessageLoopProxy::current(), run_if_not_canceled);

  RunTaskOnBackendThread(
      FROM_HERE,
      base::Bind(&SessionBackend::ReadLastSessionCommands, backend(),
                 is_canceled, callback_runner));
  return id;
}

bool BaseSessionService::RunTaskOnBackendThread(
    const tracked_objects::Location& from_here,
    const base::Closure& task) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  base::SequencedWorkerPool* pool = content::BrowserThread::GetBlockingPool();
  if (!pool->IsShutdownInProgress()) {
    return pool->PostSequencedWorkerTask(sequence_token_,
                                         from_here,
                                         task);
  } else {
    // Fall back to executing on the main thread if the sequence
    // worker pool has been requested to shutdown (around shutdown
    // time).
    task.Run();
    return true;
  }
}
