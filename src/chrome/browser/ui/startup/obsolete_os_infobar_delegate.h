// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_STARTUP_OBSOLETE_OS_INFOBAR_DELEGATE_H_
#define CHROME_BROWSER_UI_STARTUP_OBSOLETE_OS_INFOBAR_DELEGATE_H_

#include "base/compiler_specific.h"
#include "base/strings/string16.h"
#include "chrome/browser/infobars/confirm_infobar_delegate.h"
#include "url/gurl.h"

class InfoBarService;

// An infobar that displays a message saying the system is obsolete, along with
// a "Learn More" link.
class ObsoleteOSInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  // Creates an obsolete OS infobar delegate and adds it to |infobar_service|.
  static void Create(InfoBarService* infobar_service);

 private:
  explicit ObsoleteOSInfoBarDelegate(InfoBarService* infobar_service);
  virtual ~ObsoleteOSInfoBarDelegate();

  virtual string16 GetMessageText() const OVERRIDE;
  virtual int GetButtons() const OVERRIDE;
  virtual string16 GetLinkText() const OVERRIDE;
  virtual bool LinkClicked(WindowOpenDisposition disposition) OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(ObsoleteOSInfoBarDelegate);
};

#endif  // CHROME_BROWSER_UI_STARTUP_OBSOLETE_OS_INFOBAR_DELEGATE_H_
