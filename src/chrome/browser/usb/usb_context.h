// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_USB_USB_CONTEXT_H_
#define CHROME_BROWSER_USB_USB_CONTEXT_H_

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/threading/thread_checker.h"
#include "content/public/browser/browser_thread.h"

struct libusb_context;

typedef libusb_context* PlatformUsbContext;

// Ref-counted wrapper for libusb_context*.
// It also manages the life-cycle of UsbEventHandler.
// It is a blocking operation to delete UsbContext.
// Destructor must be called on FILE thread.
class UsbContext : public base::RefCountedThreadSafe<UsbContext> {
 public:
  PlatformUsbContext context() const { return context_; }

 protected:
  friend class UsbService;
  friend class base::RefCountedThreadSafe<UsbContext>;

  UsbContext();
  virtual ~UsbContext();

 private:
  class UsbEventHandler;
  PlatformUsbContext context_;
  UsbEventHandler* event_handler_;
  base::ThreadChecker thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(UsbContext);
};

#endif  // CHROME_BROWSER_USB_USB_CONTEXT_H_
