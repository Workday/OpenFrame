// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/usb/usb_context.h"

#include "base/logging.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "content/public/browser/browser_thread.h"
#include "third_party/libusb/src/libusb/interrupt.h"
#include "third_party/libusb/src/libusb/libusb.h"

// The UsbEventHandler works around a design flaw in the libusb interface. There
// is currently no way to signal to libusb that any caller into one of the event
// handler calls should return without handling any events.
class UsbContext::UsbEventHandler : public base::PlatformThread::Delegate {
 public:
  explicit UsbEventHandler(libusb_context* context);
  virtual ~UsbEventHandler();

  // base::PlatformThread::Delegate
  virtual void ThreadMain() OVERRIDE;

 private:
  volatile bool running_;
  libusb_context* context_;
  base::PlatformThreadHandle thread_handle_;
  base::WaitableEvent start_polling_;
  DISALLOW_COPY_AND_ASSIGN(UsbEventHandler);
};

UsbContext::UsbEventHandler::UsbEventHandler(libusb_context* context)
    : running_(true),
      context_(context),
      thread_handle_(0),
      start_polling_(false, false) {
  bool success = base::PlatformThread::Create(0, this, &thread_handle_);
  DCHECK(success) << "Failed to create USB IO handling thread.";
  start_polling_.Wait();
}

UsbContext::UsbEventHandler::~UsbEventHandler() {
  running_ = false;
  // Spreading running_ to the UsbEventHandler thread.
  base::subtle::MemoryBarrier();
  libusb_interrupt_handle_event(context_);
  base::PlatformThread::Join(thread_handle_);
}

void UsbContext::UsbEventHandler::ThreadMain() {
  base::PlatformThread::SetName("UsbEventHandler");
  VLOG(1) << "UsbEventHandler started.";
  if (running_) {
    start_polling_.Signal();
    libusb_handle_events(context_);
  }
  while (running_)
    libusb_handle_events(context_);
  VLOG(1) << "UsbEventHandler shutting down.";
}

UsbContext::UsbContext() : context_(NULL) {
  DCHECK(thread_checker_.CalledOnValidThread());
  CHECK_EQ(0, libusb_init(&context_)) << "Cannot initialize libusb";
  event_handler_ = new UsbEventHandler(context_);
}

UsbContext::~UsbContext() {
  // destruction of UsbEventHandler is a blocking operation.
  DCHECK(thread_checker_.CalledOnValidThread());
  delete event_handler_;
  event_handler_ = NULL;
  libusb_exit(context_);
}
