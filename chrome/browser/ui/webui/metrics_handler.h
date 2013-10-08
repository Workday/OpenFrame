// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_METRICS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_METRICS_HANDLER_H_

#include "base/compiler_specific.h"
#include "content/public/browser/web_ui_message_handler.h"

///////////////////////////////////////////////////////////////////////////////
// MetricsHandler

// Let the page contents record UMA actions. Only use when you can't do it from
// C++. For example, we currently use it to let the NTP log the postion of the
// Most Visited or Bookmark the user clicked on, as we don't get that
// information through RequestOpenURL. You will need to update the metrics
// dashboard with the action names you use, as our processor won't catch that
// information (treat it as RecordComputedMetrics)

namespace base {
class ListValue;
}

class MetricsHandler : public content::WebUIMessageHandler {
 public:
  MetricsHandler();
  virtual ~MetricsHandler();

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

  // Callback for the "metricsHandler:recordAction" message. This records a
  // user action.
  void HandleRecordAction(const base::ListValue* args);

  // TODO(dbeam): http://crbug.com/104338

  // Callback for the "metricsHandler:recordInHistogram" message. This records
  // into a histogram. |args| contains the histogram name, the value to record,
  // and the maximum allowed value, which can be at most 4000. The histogram
  // will use at most 100 buckets, one for each 1, 10, or 100 different values,
  // depending on the maximum value.
  void HandleRecordInHistogram(const base::ListValue* args);

  // Callback for the "metricsHandler:logEventTime" message.
  void HandleLogEventTime(const base::ListValue* args);

  // Used to log when a user mouses over a tile or title on the NTP.
  void HandleLogMouseover(const base::ListValue* args);

 private:
  DISALLOW_COPY_AND_ASSIGN(MetricsHandler);
};

#endif  // CHROME_BROWSER_UI_WEBUI_METRICS_HANDLER_H_
