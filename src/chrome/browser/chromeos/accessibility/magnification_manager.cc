// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/accessibility/magnification_manager.h"

#include <limits>

#include "ash/magnifier/magnification_controller.h"
#include "ash/magnifier/partial_magnification_controller.h"
#include "ash/shell.h"
#include "ash/shell_delegate.h"
#include "ash/system/tray/system_tray_notifier.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/prefs/pref_member.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/accessibility/accessibility_manager.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"

namespace chromeos {

namespace {
static MagnificationManager* g_magnification_manager = NULL;
}

class MagnificationManagerImpl : public MagnificationManager,
                                 public content::NotificationObserver {
 public:
  MagnificationManagerImpl()
      : first_time_update_(true),
        profile_(NULL),
        magnifier_enabled_pref_handler_(prefs::kScreenMagnifierEnabled),
        magnifier_type_pref_handler_(prefs::kScreenMagnifierType),
        magnifier_scale_pref_handler_(prefs::kScreenMagnifierScale),
        type_(ash::kDefaultMagnifierType),
        enabled_(false) {
    registrar_.Add(this,
                   chrome::NOTIFICATION_LOGIN_OR_LOCK_WEBUI_VISIBLE,
                   content::NotificationService::AllSources());
    registrar_.Add(this,
                   chrome::NOTIFICATION_SESSION_STARTED,
                   content::NotificationService::AllSources());
    registrar_.Add(this,
                   chrome::NOTIFICATION_PROFILE_DESTROYED,
                   content::NotificationService::AllSources());
  }

  virtual ~MagnificationManagerImpl() {
    CHECK(this == g_magnification_manager);
  }

  // MagnificationManager implimentation:
  virtual bool IsMagnifierEnabled() const OVERRIDE {
    return enabled_;
  }

  virtual ash::MagnifierType GetMagnifierType() const OVERRIDE {
    return type_;
  }

  virtual void SetMagnifierEnabled(bool enabled) OVERRIDE {
    if (!profile_)
      return;

    PrefService* prefs = profile_->GetPrefs();
    prefs->SetBoolean(prefs::kScreenMagnifierEnabled, enabled);
    prefs->CommitPendingWrite();
  }

  virtual void SetMagnifierType(ash::MagnifierType type) OVERRIDE {
    if (!profile_)
      return;

    PrefService* prefs = profile_->GetPrefs();
    prefs->SetInteger(prefs::kScreenMagnifierType, type);
    prefs->CommitPendingWrite();
  }

  virtual void SaveScreenMagnifierScale(double scale) OVERRIDE {
    if (!profile_)
      return;

    profile_->GetPrefs()->SetDouble(prefs::kScreenMagnifierScale, scale);
  }

  virtual double GetSavedScreenMagnifierScale() const OVERRIDE {
    if (!profile_)
      return std::numeric_limits<double>::min();

    return profile_->GetPrefs()->GetDouble(prefs::kScreenMagnifierScale);
  }

  virtual void SetProfileForTest(Profile* profile) OVERRIDE {
    SetProfile(profile);
  }

 private:
  void SetProfile(Profile* profile) {
    pref_change_registrar_.reset();

    if (profile) {
      // TODO(yoshiki): Move following code to PrefHandler.
      pref_change_registrar_.reset(new PrefChangeRegistrar);
      pref_change_registrar_->Init(profile->GetPrefs());
      pref_change_registrar_->Add(
          prefs::kScreenMagnifierEnabled,
          base::Bind(&MagnificationManagerImpl::UpdateMagnifierFromPrefs,
                     base::Unretained(this)));
      pref_change_registrar_->Add(
          prefs::kScreenMagnifierType,
          base::Bind(&MagnificationManagerImpl::UpdateMagnifierFromPrefs,
                     base::Unretained(this)));
    }

    magnifier_enabled_pref_handler_.HandleProfileChanged(profile_, profile);
    magnifier_type_pref_handler_.HandleProfileChanged(profile_, profile);
    magnifier_scale_pref_handler_.HandleProfileChanged(profile_, profile);

    profile_ = profile;
    UpdateMagnifierFromPrefs();
  }

  virtual void SetMagnifierEnabledInternal(bool enabled) {
    // This method may be invoked even when the other magnifier settings (e.g.
    // type or scale) are changed, so we need to call magnification controller
    // even if |enabled| is unchanged. Only if |enabled| is false and the
    // magnifier is already disabled, we are sure that we don't need to reflect
    // the new settings right now because the magnifier keeps disabled.
    if (!enabled && !enabled_)
      return;

    enabled_ = enabled;

    if (type_ == ash::MAGNIFIER_FULL) {
      ash::Shell::GetInstance()->magnification_controller()->SetEnabled(
          enabled_);
    } else {
      ash::Shell::GetInstance()->partial_magnification_controller()->SetEnabled(
          enabled_);
    }
  }

  virtual void SetMagnifierTypeInternal(ash::MagnifierType type) {
    if (type_ == type)
      return;

    type_ = ash::MAGNIFIER_FULL;  // (leave out for full magnifier)
  }

  void UpdateMagnifierFromPrefs() {
    if (!profile_)
      return;

    const bool enabled =
        profile_->GetPrefs()->GetBoolean(prefs::kScreenMagnifierEnabled);
    const int type_integer =
        profile_->GetPrefs()->GetInteger(prefs::kScreenMagnifierType);

    ash::MagnifierType type = ash::kDefaultMagnifierType;
    if (type_integer > 0 && type_integer <= ash::kMaxMagnifierType) {
      type = static_cast<ash::MagnifierType>(type_integer);
    } else if (type_integer == 0) {
      // Type 0 is used to disable the screen magnifier through policy. As the
      // magnifier type is irrelevant in this case, it is OK to just fall back
      // to the default.
    } else {
      NOTREACHED();
    }

    if (!enabled) {
      SetMagnifierEnabledInternal(enabled);
      SetMagnifierTypeInternal(type);
    } else {
      SetMagnifierTypeInternal(type);
      SetMagnifierEnabledInternal(enabled);
    }

    AccessibilityStatusEventDetails details(
        enabled_, type_, ash::A11Y_NOTIFICATION_NONE);
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_CROS_ACCESSIBILITY_TOGGLE_SCREEN_MAGNIFIER,
        content::NotificationService::AllSources(),
        content::Details<AccessibilityStatusEventDetails>(&details));
  }

  // content::NotificationObserver implementation:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    switch (type) {
      case chrome::NOTIFICATION_LOGIN_OR_LOCK_WEBUI_VISIBLE: {
        // Update |profile_| when entering the login screen.
        Profile* profile = ProfileManager::GetDefaultProfile();
        if (ProfileHelper::IsSigninProfile(profile))
          SetProfile(profile);
        break;
      }
      case chrome::NOTIFICATION_SESSION_STARTED:
        // Update |profile_| when entering a session.
        SetProfile(ProfileManager::GetDefaultProfile());
        break;
      case chrome::NOTIFICATION_PROFILE_DESTROYED: {
        // Update |profile_| when exiting a session or shutting down.
        Profile* profile = content::Source<Profile>(source).ptr();
        if (profile_ == profile)
          SetProfile(NULL);
        break;
      }
    }
  }

  bool first_time_update_;
  Profile* profile_;

  AccessibilityManager::PrefHandler magnifier_enabled_pref_handler_;
  AccessibilityManager::PrefHandler magnifier_type_pref_handler_;
  AccessibilityManager::PrefHandler magnifier_scale_pref_handler_;

  ash::MagnifierType type_;
  bool enabled_;
  content::NotificationRegistrar registrar_;
  scoped_ptr<PrefChangeRegistrar> pref_change_registrar_;

  DISALLOW_COPY_AND_ASSIGN(MagnificationManagerImpl);
};

// static
void MagnificationManager::Initialize() {
  CHECK(g_magnification_manager == NULL);
  g_magnification_manager = new MagnificationManagerImpl();
}

// static
void MagnificationManager::Shutdown() {
  CHECK(g_magnification_manager);
  delete g_magnification_manager;
  g_magnification_manager = NULL;
}

// static
MagnificationManager* MagnificationManager::Get() {
  return g_magnification_manager;
}

}  // namespace chromeos
