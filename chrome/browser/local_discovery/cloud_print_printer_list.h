// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_LOCAL_DISCOVERY_CLOUD_PRINT_PRINTER_LIST_H_
#define CHROME_BROWSER_LOCAL_DISCOVERY_CLOUD_PRINT_PRINTER_LIST_H_

#include <string>
#include <vector>

#include "base/values.h"
#include "chrome/browser/local_discovery/cloud_device_list_delegate.h"
#include "chrome/browser/local_discovery/gcd_api_flow.h"

namespace local_discovery {

class CloudPrintPrinterList : public CloudPrintApiFlowRequest {
 public:
  explicit CloudPrintPrinterList(CloudDeviceListDelegate* delegate);
  ~CloudPrintPrinterList() override;

  void OnGCDAPIFlowError(GCDApiFlow::Status status) override;

  void OnGCDAPIFlowComplete(const base::DictionaryValue& value) override;

  GURL GetURL() override;

 private:
  bool FillPrinterDetails(const base::DictionaryValue& printer_value,
                          CloudDeviceListDelegate::Device* printer_details);

  CloudDeviceListDelegate* delegate_;
};

}  // namespace local_discovery

#endif  // CHROME_BROWSER_LOCAL_DISCOVERY_CLOUD_PRINT_PRINTER_LIST_H_
