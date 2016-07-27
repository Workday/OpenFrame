// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_PUBLIC_WEB_STATE_CRW_WEB_VIEW_PROXY_H_
#define IOS_WEB_PUBLIC_WEB_STATE_CRW_WEB_VIEW_PROXY_H_

#import <UIKit/UIKit.h>

#include "ios/web/public/web_view_type.h"

@class CRWWebViewScrollViewProxy;

// Provides an interface for embedders to access the WebState's web view in a
// limited and controlled manner.
// TODO(crbug.com/546152): rename protocol to CRWContentViewProxy.
@protocol CRWWebViewProxy<NSObject>

// The web view's bounding rectangle (relative to its parent).
@property(readonly, assign) CGRect bounds;

// The web view's frame rectangle.
@property(readonly, assign) CGRect frame;

// A Boolean value indicating whether web content can programmatically display
// the keyboard.
@property(nonatomic, assign) BOOL keyboardDisplayRequiresUserAction;

// Gives the embedder access to the web view's UIScrollView in a limited and
// controlled manner.
@property(nonatomic, readonly) CRWWebViewScrollViewProxy* scrollViewProxy;

// Returns the webview's gesture recognizers.
@property(nonatomic, readonly) NSArray* gestureRecognizers;

// Retuns the type of the web view this proxy manages.
@property(nonatomic, readonly) web::WebViewType webViewType;

// Register the given insets for the given caller.
- (void)registerInsets:(UIEdgeInsets)insets forCaller:(id)caller;

// Unregister the registered insets for the given caller.
- (void)unregisterInsetsForCaller:(id)caller;

// Wrapper around the addSubview method of the webview.
- (void)addSubview:(UIView*)view;

// Returns YES if it makes sense to search for text right now.
- (BOOL)hasSearchableTextContent;

// Returns the currently visible keyboard accessory, or nil.
- (UIView*)keyboardAccessory;

// Returns the currently visible keyboard input assistant item, or nil. Only
// valid on iOS 9 or above.
#if defined(__IPHONE_9_0) && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_9_0
- (UITextInputAssistantItem*)inputAssistantItem;
#endif

@end

#endif  // IOS_WEB_PUBLIC_WEB_STATE_CRW_WEB_VIEW_PROXY_H_
