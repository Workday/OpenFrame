// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NOTIFICATIONS_SYNC_NOTIFIER_CHROME_NOTIFIER_DELEGATE_H_
#define CHROME_BROWSER_NOTIFICATIONS_SYNC_NOTIFIER_CHROME_NOTIFIER_DELEGATE_H_

#include <string>

#include "chrome/browser/notifications/notification_delegate.h"
#include "url/gurl.h"

namespace notifier {

class ChromeNotifierService;

// ChromeNotifierDelegate is a NotificationDelegate which catches
// responses from the NotificationUIManager when a notification
// has been closed.

class ChromeNotifierDelegate : public NotificationDelegate {
 public:
  // We use an id instead of a notification so we can check to see if the
  // notification still exists before acting on it instead of using a ref count.
  explicit ChromeNotifierDelegate(const std::string& notification_id,
                                  ChromeNotifierService* notifier);

  // NotificationDelegate interface.
  virtual void Display() OVERRIDE {}
  virtual void Error() OVERRIDE {}
  virtual void Close(bool by_user) OVERRIDE;
  virtual void Click() OVERRIDE;
  virtual void ButtonClick(int button_index) OVERRIDE;
  virtual std::string id() const OVERRIDE;

  virtual content::RenderViewHost* GetRenderViewHost() const OVERRIDE;

 private:
  virtual ~ChromeNotifierDelegate();
  void NavigateToUrl(const GURL& destination) const;

  const std::string notification_id_;
  ChromeNotifierService* const chrome_notifier_;

  DISALLOW_COPY_AND_ASSIGN(ChromeNotifierDelegate);
};

}  // namespace notifier

#endif  // CHROME_BROWSER_NOTIFICATIONS_SYNC_NOTIFIER_CHROME_NOTIFIER_DELEGATE_H_
