// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_NOTIFICATIONS_BALLOON_VIEW_HOST_CHROMEOS_H_
#define CHROME_BROWSER_CHROMEOS_NOTIFICATIONS_BALLOON_VIEW_HOST_CHROMEOS_H_

#include "chrome/browser/ui/views/notifications/balloon_view_host.h"

#include <map>
#include <string>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "ui/gfx/native_widget_types.h"

class GURL;

namespace base {
class ListValue;
}

namespace chromeos {

class BalloonViewHost : public ::BalloonViewHost {
 public:
  typedef base::Callback<void(const base::ListValue*)> MessageCallback;

  explicit BalloonViewHost(Balloon* balloon);
  virtual ~BalloonViewHost();

  // Adds a callback for WebUI message. Returns true if the callback
  // is succssfully registered, or false otherwise. It fails to add if
  // a callback for given message already exists. The callback object
  // is owned and deleted by callee.
  bool AddWebUIMessageCallback(const std::string& message,
                               const MessageCallback& callback);

 private:
  // WebContentsDelegate
  virtual bool HandleContextMenu(
      const content::ContextMenuParams& params) OVERRIDE;
  virtual void WebUISend(content::WebContents* tab,
                         const GURL& source_url,
                         const std::string& name,
                         const base::ListValue& args) OVERRIDE;

  // A map of message name -> message handling callback.
  typedef std::map<std::string, MessageCallback> MessageCallbackMap;
  MessageCallbackMap message_callbacks_;

  DISALLOW_COPY_AND_ASSIGN(BalloonViewHost);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_NOTIFICATIONS_BALLOON_VIEW_HOST_CHROMEOS_H_
