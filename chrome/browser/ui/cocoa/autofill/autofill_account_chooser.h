// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_ACCOUNT_CHOOSER_H_
#define CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_ACCOUNT_CHOOSER_H_

#import <Cocoa/Cocoa.h>

#include "base/mac/scoped_nsobject.h"

namespace autofill {
  class AutofillDialogViewDelegate;
}

@class MenuButton;

@interface AutofillAccountChooser : NSView {
 @private
  base::scoped_nsobject<NSButton> link_;
  base::scoped_nsobject<MenuButton> popup_;
  base::scoped_nsobject<NSImageView> icon_;
  autofill::AutofillDialogViewDelegate* delegate_;  // weak.
}

- (id)initWithFrame:(NSRect)frame
         delegate:(autofill::AutofillDialogViewDelegate*)delegate;
- (void)update;

@end

#endif  // CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_ACCOUNT_CHOOSER_H_
