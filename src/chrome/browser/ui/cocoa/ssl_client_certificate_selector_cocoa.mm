// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/ssl_client_certificate_selector_cocoa.h"

#import <SecurityInterface/SFChooseIdentityPanel.h>

#include "base/logging.h"
#include "base/mac/foundation_util.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ssl/ssl_client_auth_observer.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_mac.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/generated_resources.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util_mac.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "ui/base/cocoa/window_size_constants.h"
#include "ui/base/l10n/l10n_util_mac.h"

using content::BrowserThread;

@interface SFChooseIdentityPanel (SystemPrivate)
// A system-private interface that dismisses a panel whose sheet was started by
// -beginSheetForWindow:modalDelegate:didEndSelector:contextInfo:identities:message:
// as though the user clicked the button identified by returnCode. Verified
// present in 10.5 through 10.8.
- (void)_dismissWithCode:(NSInteger)code;
@end

@interface SSLClientCertificateSelectorCocoa ()
- (void)onConstrainedWindowClosed;
@end

class SSLClientAuthObserverCocoaBridge : public SSLClientAuthObserver,
                                         public ConstrainedWindowMacDelegate {
 public:
  SSLClientAuthObserverCocoaBridge(
      const net::HttpNetworkSession* network_session,
      net::SSLCertRequestInfo* cert_request_info,
      const chrome::SelectCertificateCallback& callback,
      SSLClientCertificateSelectorCocoa* controller)
      : SSLClientAuthObserver(network_session, cert_request_info, callback),
        controller_(controller) {
  }

  // SSLClientAuthObserver implementation:
  virtual void OnCertSelectedByNotification() OVERRIDE {
    [controller_ closeSheetWithAnimation:NO];
  }

  // ConstrainedWindowMacDelegate implementation:
  virtual void OnConstrainedWindowClosed(
      ConstrainedWindowMac* window) OVERRIDE {
    // |onConstrainedWindowClosed| will delete the sheet which might be still
    // in use higher up the call stack. Wait for the next cycle of the event
    // loop to call this function.
    [controller_ performSelector:@selector(onConstrainedWindowClosed)
                      withObject:nil
                      afterDelay:0];
  }

 private:
  SSLClientCertificateSelectorCocoa* controller_;  // weak
};

namespace chrome {

void ShowSSLClientCertificateSelector(
    content::WebContents* contents,
    const net::HttpNetworkSession* network_session,
    net::SSLCertRequestInfo* cert_request_info,
    const SelectCertificateCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // The dialog manages its own lifetime.
  SSLClientCertificateSelectorCocoa* selector =
      [[SSLClientCertificateSelectorCocoa alloc]
          initWithNetworkSession:network_session
                 certRequestInfo:cert_request_info
                        callback:callback];
  [selector displayForWebContents:contents];
}

}  // namespace chrome

@implementation SSLClientCertificateSelectorCocoa

- (id)initWithNetworkSession:(const net::HttpNetworkSession*)networkSession
    certRequestInfo:(net::SSLCertRequestInfo*)certRequestInfo
           callback:(const chrome::SelectCertificateCallback&)callback {
  DCHECK(networkSession);
  DCHECK(certRequestInfo);
  if ((self = [super init])) {
    observer_.reset(new SSLClientAuthObserverCocoaBridge(
        networkSession, certRequestInfo, callback, self));
  }
  return self;
}

- (void)sheetDidEnd:(NSWindow*)parent
         returnCode:(NSInteger)returnCode
            context:(void*)context {
  net::X509Certificate* cert = NULL;
  if (returnCode == NSFileHandlingPanelOKButton) {
    CFRange range = CFRangeMake(0, CFArrayGetCount(identities_));
    CFIndex index =
        CFArrayGetFirstIndexOfValue(identities_, range, [panel_ identity]);
    if (index != -1)
      cert = certificates_[index].get();
    else
      NOTREACHED();
  }

  // Finally, tell the backend which identity (or none) the user selected.
  observer_->StopObserving();
  observer_->CertificateSelected(cert);

  if (!closePending_)
    constrainedWindow_->CloseWebContentsModalDialog();
}

- (void)displayForWebContents:(content::WebContents*)webContents {
  // Create an array of CFIdentityRefs for the certificates:
  size_t numCerts = observer_->cert_request_info()->client_certs.size();
  identities_.reset(CFArrayCreateMutable(
      kCFAllocatorDefault, numCerts, &kCFTypeArrayCallBacks));
  for (size_t i = 0; i < numCerts; ++i) {
    SecCertificateRef cert =
        observer_->cert_request_info()->client_certs[i]->os_cert_handle();
    SecIdentityRef identity;
    if (SecIdentityCreateWithCertificate(NULL, cert, &identity) == noErr) {
      CFArrayAppendValue(identities_, identity);
      CFRelease(identity);
      certificates_.push_back(observer_->cert_request_info()->client_certs[i]);
    }
  }

  // Get the message to display:
  NSString* message = l10n_util::GetNSStringF(
      IDS_CLIENT_CERT_DIALOG_TEXT,
      ASCIIToUTF16(observer_->cert_request_info()->host_and_port));

  // Create and set up a system choose-identity panel.
  panel_.reset([[SFChooseIdentityPanel alloc] init]);
  [panel_ setInformativeText:message];
  [panel_ setDefaultButtonTitle:l10n_util::GetNSString(IDS_OK)];
  [panel_ setAlternateButtonTitle:l10n_util::GetNSString(IDS_CANCEL)];
  SecPolicyRef sslPolicy;
  if (net::x509_util::CreateSSLClientPolicy(&sslPolicy) == noErr) {
    [panel_ setPolicies:(id)sslPolicy];
    CFRelease(sslPolicy);
  }

  constrainedWindow_.reset(
      new ConstrainedWindowMac(observer_.get(), webContents, self));
}

- (NSWindow*)overlayWindow {
  return overlayWindow_;
}

- (SFChooseIdentityPanel*)panel {
  return panel_;
}

- (void)showSheetForWindow:(NSWindow*)window {
  NSString* title = l10n_util::GetNSString(IDS_CLIENT_CERT_DIALOG_TITLE);
  overlayWindow_.reset([window retain]);
  [panel_ beginSheetForWindow:window
                modalDelegate:self
               didEndSelector:@selector(sheetDidEnd:returnCode:context:)
                  contextInfo:NULL
                   identities:base::mac::CFToNSCast(identities_)
                      message:title];
  observer_->StartObserving();
}

- (void)closeSheetWithAnimation:(BOOL)withAnimation {
  closePending_ = YES;
  overlayWindow_.reset();
  // Closing the sheet using -[NSApp endSheet:] doesn't work so use the private
  // method.
  [panel_ _dismissWithCode:NSFileHandlingPanelCancelButton];
}

- (void)hideSheet {
  NSWindow* sheetWindow = [overlayWindow_ attachedSheet];
  [sheetWindow setAlphaValue:0.0];

  oldResizesSubviews_ = [[sheetWindow contentView] autoresizesSubviews];
  [[sheetWindow contentView] setAutoresizesSubviews:NO];

  oldSheetFrame_ = [sheetWindow frame];
  NSRect overlayFrame = [overlayWindow_ frame];
  oldSheetFrame_.origin.x -= NSMinX(overlayFrame);
  oldSheetFrame_.origin.y -= NSMinY(overlayFrame);
  [sheetWindow setFrame:ui::kWindowSizeDeterminedLater display:NO];
}

- (void)unhideSheet {
  NSWindow* sheetWindow = [overlayWindow_ attachedSheet];
  NSRect overlayFrame = [overlayWindow_ frame];
  oldSheetFrame_.origin.x += NSMinX(overlayFrame);
  oldSheetFrame_.origin.y += NSMinY(overlayFrame);
  [sheetWindow setFrame:oldSheetFrame_ display:NO];
  [[sheetWindow contentView] setAutoresizesSubviews:oldResizesSubviews_];
  [[overlayWindow_ attachedSheet] setAlphaValue:1.0];
}

- (void)pulseSheet {
  // NOOP
}

- (void)makeSheetKeyAndOrderFront {
  [[overlayWindow_ attachedSheet] makeKeyAndOrderFront:nil];
}

- (void)updateSheetPosition {
  // NOOP
}

- (void)onConstrainedWindowClosed {
  panel_.reset();
  constrainedWindow_.reset();
  [self release];
}

@end
