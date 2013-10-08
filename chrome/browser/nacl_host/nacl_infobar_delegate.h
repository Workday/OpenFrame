// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NACL_HOST_NACL_INFOBAR_DELEGATE_H_
#define CHROME_BROWSER_NACL_HOST_NACL_INFOBAR_DELEGATE_H_

#include "chrome/browser/infobars/confirm_infobar_delegate.h"

class NaClInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  // Creates a NaCl infobar delegate and adds it to the infobar service
  // corresponding to the given render process and view IDs.
  static void Create(int render_process_id, int render_view_id);

 private:
  explicit NaClInfoBarDelegate(InfoBarService* infobar_service);
  virtual ~NaClInfoBarDelegate();

  virtual string16 GetMessageText() const OVERRIDE;
  virtual int GetButtons() const OVERRIDE;
  virtual string16 GetLinkText() const OVERRIDE;
  virtual bool LinkClicked(WindowOpenDisposition disposition) OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(NaClInfoBarDelegate);
};

#endif  // CHROME_BROWSER_NACL_HOST_NACL_INFOBAR_DELEGATE_H_
