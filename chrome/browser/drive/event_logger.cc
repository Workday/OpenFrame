// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/drive/event_logger.h"

#include "base/logging.h"
#include "base/strings/stringprintf.h"

namespace drive {

EventLogger::Event::Event(
    int id, logging::LogSeverity severity, const std::string& what)
    : id(id),
      severity(severity),
      when(base::Time::Now()),
      what(what) {
}

EventLogger::EventLogger()
    : history_size_(kDefaultHistorySize),
      next_event_id_(0) {
}

EventLogger::~EventLogger() {
}

void EventLogger::Log(logging::LogSeverity severity, const std::string& what) {
  base::AutoLock auto_lock(lock_);
  history_.push_back(Event(next_event_id_, severity, what));
  ++next_event_id_;
  if (history_.size() > history_size_)
    history_.pop_front();
}

void EventLogger::SetHistorySize(size_t history_size) {
  base::AutoLock auto_lock(lock_);
  history_.clear();
  history_size_ = history_size;
}

std::vector<EventLogger::Event> EventLogger::GetHistory() {
  base::AutoLock auto_lock(lock_);
  std::vector<Event> output;
  output.assign(history_.begin(), history_.end());
  return output;
}


}  // namespace drive
