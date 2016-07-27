// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/data_reduction_proxy/data_reduction_proxy_api.h"

#include <vector>

#include "chrome/browser/net/spdyproxy/data_reduction_proxy_chrome_settings.h"
#include "chrome/browser/net/spdyproxy/data_reduction_proxy_chrome_settings_factory.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_compression_stats.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_service.h"
#include "components/data_reduction_proxy/proto/data_store.pb.h"

namespace extensions {

AsyncExtensionFunction::ResponseAction
DataReductionProxyClearDataSavingsFunction::Run() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  data_reduction_proxy::DataReductionProxySettings* settings =
      DataReductionProxyChromeSettingsFactory::GetForBrowserContext(
          browser_context());
  settings->data_reduction_proxy_service()->compression_stats()->
      ClearDataSavingStatistics();
  return RespondNow(NoArguments());
}

AsyncExtensionFunction::ResponseAction
DataReductionProxyGetDataUsageFunction::Run() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  data_reduction_proxy::DataReductionProxySettings* settings =
      DataReductionProxyChromeSettingsFactory::GetForBrowserContext(
          browser_context());
  settings->data_reduction_proxy_service()
      ->compression_stats()
      ->GetHistoricalDataUsage(base::Bind(
          &DataReductionProxyGetDataUsageFunction::ReplyWithDataUsage, this));
  return RespondLater();
}

void DataReductionProxyGetDataUsageFunction::ReplyWithDataUsage(
    scoped_ptr<std::vector<data_reduction_proxy::DataUsageBucket>> data_usage) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  scoped_ptr<base::ListValue> data_usage_buckets(new base::ListValue());
  for (const auto& data_usage_bucket : *data_usage) {
    scoped_ptr<base::ListValue> connection_usage_list(new base::ListValue());
    for (auto connection_usage : data_usage_bucket.connection_usage()) {
      scoped_ptr<base::ListValue> site_usage_list(new base::ListValue());
      for (auto site_usage : connection_usage.site_usage()) {
        scoped_ptr<base::DictionaryValue> usage(new base::DictionaryValue());
        usage->SetString("hostname", site_usage.hostname());
        usage->SetDouble("data_used", site_usage.data_used());
        usage->SetDouble("original_size", site_usage.original_size());
        site_usage_list->Append(usage.Pass());
      }
      connection_usage_list->Append(site_usage_list.Pass());
    }
    data_usage_buckets->Append(connection_usage_list.Pass());
  }

  base::DictionaryValue* result = new base::DictionaryValue();
  result->Set("data_usage_buckets", data_usage_buckets.Pass());
  Respond(OneArgument(result));
}

}  // namespace extensions
