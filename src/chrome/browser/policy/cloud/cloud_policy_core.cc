// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/cloud/cloud_policy_core.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/message_loop/message_loop.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/policy/cloud/cloud_policy_client.h"
#include "chrome/browser/policy/cloud/cloud_policy_refresh_scheduler.h"
#include "chrome/browser/policy/cloud/cloud_policy_service.h"
#include "chrome/browser/policy/cloud/cloud_policy_store.h"

namespace policy {

CloudPolicyCore::CloudPolicyCore(const PolicyNamespaceKey& key,
                                 CloudPolicyStore* store)
    : policy_ns_key_(key),
      store_(store) {}

CloudPolicyCore::~CloudPolicyCore() {}

void CloudPolicyCore::Connect(scoped_ptr<CloudPolicyClient> client) {
  CHECK(!client_);
  CHECK(client);
  client_ = client.Pass();
  service_.reset(new CloudPolicyService(policy_ns_key_, client_.get(), store_));
}

void CloudPolicyCore::Disconnect() {
  refresh_delay_.reset();
  refresh_scheduler_.reset();
  service_.reset();
  client_.reset();
}

void CloudPolicyCore::RefreshSoon() {
 if (refresh_scheduler_)
    refresh_scheduler_->RefreshSoon();
}

void CloudPolicyCore::StartRefreshScheduler() {
  if (!refresh_scheduler_) {
    refresh_scheduler_.reset(
        new CloudPolicyRefreshScheduler(
            client_.get(), store_,
            base::MessageLoop::current()->message_loop_proxy()));
    UpdateRefreshDelayFromPref();
  }
}

void CloudPolicyCore::TrackRefreshDelayPref(
    PrefService* pref_service,
    const std::string& refresh_pref_name) {
  refresh_delay_.reset(new IntegerPrefMember());
  refresh_delay_->Init(
      refresh_pref_name.c_str(), pref_service,
      base::Bind(&CloudPolicyCore::UpdateRefreshDelayFromPref,
                 base::Unretained(this)));
  UpdateRefreshDelayFromPref();
}

void CloudPolicyCore::UpdateRefreshDelayFromPref() {
  if (refresh_scheduler_ && refresh_delay_)
    refresh_scheduler_->SetRefreshDelay(refresh_delay_->GetValue());
}

}  // namespace policy
