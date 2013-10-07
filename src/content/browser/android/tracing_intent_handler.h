// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef  CONTENT_BROWSER_ANDROID_TRACING_INTENT_HANDLER_H_
#define  CONTENT_BROWSER_ANDROID_TRACING_INTENT_HANDLER_H_

#include <jni.h>
#include <string>

#include "content/browser/tracing/trace_subscriber_stdio.h"

namespace content {

// Registers the TracingIntentHandler native methods.
bool RegisterTracingIntentHandler(JNIEnv* env);

class TracingIntentHandler : public TraceSubscriberStdio {
 public:
  explicit TracingIntentHandler(const base::FilePath& path);
  virtual ~TracingIntentHandler();

  // TraceSubscriber implementation
  virtual void OnEndTracingComplete() OVERRIDE;

  // IntentHandler
  void OnEndTracing();
};

}  // namespace content
#endif  // CONTENT_BROWSER_ANDROID_TRACING_INTENT_HANDLER_H_
