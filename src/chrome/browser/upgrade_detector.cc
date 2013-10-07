// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/upgrade_detector.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/prefs/pref_registry_simple.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/ui/browser_otr_state.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_service.h"
#include "grit/theme_resources.h"

// How long to wait between checks for whether the user has been idle.
static const int kIdleRepeatingTimerWait = 10;  // Minutes (seconds if testing).

// How much idle time (since last input even was detected) must have passed
// until we notify that a critical update has occurred.
static const int kIdleAmount = 2;  // Hours (or seconds, if testing).

bool UseTestingIntervals() {
  // If a command line parameter specifying how long the upgrade check should
  // be, we assume it is for testing and switch to using seconds instead of
  // hours.
  const CommandLine& cmd_line = *CommandLine::ForCurrentProcess();
  return !cmd_line.GetSwitchValueASCII(
      switches::kCheckForUpdateIntervalSec).empty();
}

// static
void UpgradeDetector::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(prefs::kRestartLastSessionOnShutdown, false);
  registry->RegisterBooleanPref(prefs::kWasRestarted, false);
}

int UpgradeDetector::GetIconResourceID(UpgradeNotificationIconType type) {
  if (type == UPGRADE_ICON_TYPE_BADGE) {
    // Badges do not exist on Views and Mac OS X.
#if !defined(TOOLKIT_VIEWS) && !defined(OS_MACOSX)
    switch (upgrade_notification_stage_) {
      case UPGRADE_ANNOYANCE_NONE:
        return 0;
      case UPGRADE_ANNOYANCE_LOW:
        return IDR_UPDATE_BADGE;
      case UPGRADE_ANNOYANCE_ELEVATED:
        return IDR_UPDATE_BADGE2;
      case UPGRADE_ANNOYANCE_HIGH:
        return IDR_UPDATE_BADGE3;
      case UPGRADE_ANNOYANCE_SEVERE:
        return IDR_UPDATE_BADGE3;
      case UPGRADE_ANNOYANCE_CRITICAL:
        return IDR_UPDATE_BADGE3;
    }
#endif
    NOTREACHED();
    return 0;
  }

  switch (upgrade_notification_stage_) {
    case UPGRADE_ANNOYANCE_NONE:
      return 0;
    case UPGRADE_ANNOYANCE_LOW:
      return IDR_UPDATE_MENU_SEVERITY_LOW;
    case UPGRADE_ANNOYANCE_ELEVATED:
      return IDR_UPDATE_MENU_SEVERITY_MEDIUM;
    case UPGRADE_ANNOYANCE_HIGH:
      return IDR_UPDATE_MENU_SEVERITY_HIGH;
    case UPGRADE_ANNOYANCE_SEVERE:
      return IDR_UPDATE_MENU_SEVERITY_HIGH;
    case UPGRADE_ANNOYANCE_CRITICAL:
      return IDR_UPDATE_MENU_SEVERITY_HIGH;
  }
  NOTREACHED();
  return 0;
}

UpgradeDetector::UpgradeDetector()
    : upgrade_available_(UPGRADE_AVAILABLE_NONE),
      critical_update_acknowledged_(false),
      upgrade_notification_stage_(UPGRADE_ANNOYANCE_NONE),
      notify_upgrade_(false) {
}

UpgradeDetector::~UpgradeDetector() {
}

void UpgradeDetector::NotifyUpgradeDetected() {
  upgrade_detected_time_ = base::Time::Now();
  critical_update_acknowledged_ = false;
}

void UpgradeDetector::NotifyUpgradeRecommended() {
  notify_upgrade_ = true;

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_UPGRADE_RECOMMENDED,
      content::Source<UpgradeDetector>(this),
      content::NotificationService::NoDetails());

  switch (upgrade_available_) {
    case UPGRADE_NEEDED_OUTDATED_INSTALL: {
      content::NotificationService::current()->Notify(
          chrome::NOTIFICATION_OUTDATED_INSTALL,
          content::Source<UpgradeDetector>(this),
          content::NotificationService::NoDetails());
      break;
    }
    case UPGRADE_AVAILABLE_CRITICAL: {
      int idle_timer = UseTestingIntervals() ?
          kIdleRepeatingTimerWait :
          kIdleRepeatingTimerWait * 60;  // To minutes.
      idle_check_timer_.Start(FROM_HERE,
          base::TimeDelta::FromSeconds(idle_timer),
          this, &UpgradeDetector::CheckIdle);
      break;
    }
    default:
      break;
  }
}

void UpgradeDetector::CheckIdle() {
  // CalculateIdleState expects an interval in seconds.
  int idle_time_allowed = UseTestingIntervals() ? kIdleAmount :
                                                  kIdleAmount * 60 * 60;

  CalculateIdleState(
      idle_time_allowed, base::Bind(&UpgradeDetector::IdleCallback,
                                    base::Unretained(this)));
}

void UpgradeDetector::IdleCallback(IdleState state) {
  // Don't proceed while an incognito window is open. The timer will still
  // keep firing, so this function will get a chance to re-evaluate this.
  if (chrome::IsOffTheRecordSessionActive())
    return;

  switch (state) {
    case IDLE_STATE_LOCKED:
      // Computer is locked, auto-restart.
      idle_check_timer_.Stop();
      chrome::AttemptRestart();
      break;
    case IDLE_STATE_IDLE:
      // Computer has been idle for long enough, show warning.
      idle_check_timer_.Stop();
      content::NotificationService::current()->Notify(
          chrome::NOTIFICATION_CRITICAL_UPGRADE_INSTALLED,
          content::Source<UpgradeDetector>(this),
          content::NotificationService::NoDetails());
      break;
    case IDLE_STATE_ACTIVE:
    case IDLE_STATE_UNKNOWN:
      break;
    default:
      NOTREACHED();  // Need to add any new value above (either providing
                     // automatic restart or show notification to user).
      break;
  }
}
