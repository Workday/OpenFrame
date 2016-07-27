// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RENDERER_HOST_CHROME_RENDER_WIDGET_HOST_VIEW_MAC_DELEGATE_H_
#define CHROME_BROWSER_RENDERER_HOST_CHROME_RENDER_WIDGET_HOST_VIEW_MAC_DELEGATE_H_

#import <Cocoa/Cocoa.h>

#include "base/mac/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#import "content/public/browser/render_widget_host_view_mac_delegate.h"

namespace content {
class RenderWidgetHost;
}

namespace ChromeRenderWidgetHostViewMacDelegateInternal {
class SpellCheckObserver;
}

@class HistorySwiper;
@interface ChromeRenderWidgetHostViewMacDelegate
    : NSObject<RenderWidgetHostViewMacDelegate> {
 @private
  content::RenderWidgetHost* renderWidgetHost_;  // weak
  scoped_ptr<ChromeRenderWidgetHostViewMacDelegateInternal::SpellCheckObserver>
      spellingObserver_;

  // Used for continuous spell checking.
  BOOL spellcheckEnabled_;
  BOOL spellcheckChecked_;

  // Responsible for 2-finger swipes history navigation.
  base::scoped_nsobject<HistorySwiper> historySwiper_;
}

- (id)initWithRenderWidgetHost:(content::RenderWidgetHost*)renderWidgetHost;

- (BOOL)handleEvent:(NSEvent*)event;
- (BOOL)validateUserInterfaceItem:(id<NSValidatedUserInterfaceItem>)item
                      isValidItem:(BOOL*)valid;
@end

#endif  // CHROME_BROWSER_RENDERER_HOST_CHROME_RENDER_WIDGET_HOST_VIEW_MAC_DELEGATE_H_
