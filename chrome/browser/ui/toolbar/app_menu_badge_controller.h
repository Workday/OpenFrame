// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TOOLBAR_APP_MENU_BADGE_CONTROLLER_H_
#define CHROME_BROWSER_UI_TOOLBAR_APP_MENU_BADGE_CONTROLLER_H_

#include "base/macros.h"
#include "chrome/browser/ui/toolbar/app_menu_icon_painter.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"

class Profile;

// AppMenuBadgeController encapsulates the logic for badging the app menu icon
// as a result of various events - such as available updates, errors, etc.
class AppMenuBadgeController : public content::NotificationObserver {
 public:
  enum BadgeType {
    BADGE_TYPE_NONE,
    BADGE_TYPE_UPGRADE_NOTIFICATION,
    BADGE_TYPE_GLOBAL_ERROR,
    BADGE_TYPE_INCOMPATIBILITY_WARNING,
  };

  // Delegate interface for receiving badge update notifications.
  class Delegate {
   public:
    // Notifies the UI to update the badge to have the specified |severity|, as
    // well as specifying whether it should |animate|. The |type| parameter
    // specifies the type of change (i.e. the source of the notification).
    virtual void UpdateBadgeSeverity(BadgeType type,
                                     AppMenuIconPainter::Severity severity,
                                     bool animate) = 0;

   protected:
    virtual ~Delegate() {}
  };

  // Creates an instance of this class for the given |profile| that will notify
  // |delegate| of updates.
  AppMenuBadgeController(Profile* profile, Delegate* delegate);
  ~AppMenuBadgeController() override;

  // Forces an update of the UI based on the current state of the world. This
  // will check whether there are any current pending updates, global errors,
  // etc. and based on that information trigger an appropriate call to the
  // delegate.
  void UpdateDelegate();

 private:
  // content::NotificationObserver:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  Profile* profile_;
  Delegate* delegate_;
  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(AppMenuBadgeController);
};

#endif  // CHROME_BROWSER_UI_TOOLBAR_APP_MENU_BADGE_CONTROLLER_H_
