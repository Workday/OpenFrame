// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/remote_database_manager.h"

#include <vector>

#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/timer/elapsed_timer.h"
#include "chrome/browser/safe_browsing/safe_browsing_api_handler.h"
#include "components/variations/variations_associated_data.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace {

// Android field trial
const char kAndroidFieldExperiment[] = "SafeBrowsingAndroid";
const char kAndroidFieldParam[] = "enabled";
const char kAndroidFieldParamEnabledValue[] = "true";
const char kAndroidTypesToCheckParam[] = "types_to_check";

}  // namespace

namespace safe_browsing {

//
// RemoteSafeBrowsingDatabaseManager::ClientRequest methods
//
class RemoteSafeBrowsingDatabaseManager::ClientRequest {
 public:
  ClientRequest(Client* client,
                RemoteSafeBrowsingDatabaseManager* db_manager,
                const GURL& url);

  static void OnRequestDoneWeak(const base::WeakPtr<ClientRequest>& req,
                                SBThreatType matched_threat_type,
                                const std::string& metadata);
  void OnRequestDone(SBThreatType matched_threat_type,
                     const std::string& metadata);

  // Accessors
  Client* client() const { return client_; }
  const GURL& url() const { return url_; }
  base::WeakPtr<ClientRequest> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  Client* client_;
  RemoteSafeBrowsingDatabaseManager* db_manager_;
  GURL url_;
  base::ElapsedTimer timer_;
  base::WeakPtrFactory<ClientRequest> weak_factory_;
};

RemoteSafeBrowsingDatabaseManager::ClientRequest::ClientRequest(
    Client* client,
    RemoteSafeBrowsingDatabaseManager* db_manager,
    const GURL& url)
    : client_(client), db_manager_(db_manager), url_(url), weak_factory_(this) {
}

// Static
void RemoteSafeBrowsingDatabaseManager::ClientRequest::OnRequestDoneWeak(
    const base::WeakPtr<ClientRequest>& req,
    SBThreatType matched_threat_type,
    const std::string& metadata) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!req)
    return;  // Previously canceled
  req->OnRequestDone(matched_threat_type, metadata);
}

void RemoteSafeBrowsingDatabaseManager::ClientRequest::OnRequestDone(
    SBThreatType matched_threat_type,
    const std::string& metadata) {
  DVLOG(1) << "OnRequestDone took " << timer_.Elapsed().InMilliseconds()
           << " ms for client " << client_ << " and URL " << url_;
  client_->OnCheckBrowseUrlResult(url_, matched_threat_type, metadata);
  UMA_HISTOGRAM_TIMES("SB2.RemoteCall.Elapsed", timer_.Elapsed());
  // CancelCheck() will delete *this.
  db_manager_->CancelCheck(client_);
}

//
// RemoteSafeBrowsingDatabaseManager methods
//

// TODO(nparker): Add more tests for this class
RemoteSafeBrowsingDatabaseManager::RemoteSafeBrowsingDatabaseManager()
    : enabled_(false) {

  // Check if the field trial is enabled.
  const std::string enabled_param = variations::GetVariationParamValue(
      kAndroidFieldExperiment, kAndroidFieldParam);
  is_android_field_trial_enabled_ =
      (enabled_param == kAndroidFieldParamEnabledValue);

  // Decide which resource types to check. These two are the minimum.
  resource_types_to_check_.insert(content::RESOURCE_TYPE_MAIN_FRAME);
  resource_types_to_check_.insert(content::RESOURCE_TYPE_SUB_FRAME);

  // The param is expected to be a comma-separated list of ints
  // corresponding to the enum types.
  const std::string ints_str = variations::GetVariationParamValue(
      kAndroidFieldExperiment, kAndroidTypesToCheckParam);
  for (const std::string& val_str : base::SplitString(
           ints_str, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    int i;
    if (base::StringToInt(val_str, &i) && i >= 0 &&
        i < content::RESOURCE_TYPE_LAST_TYPE) {
      resource_types_to_check_.insert(static_cast<content::ResourceType>(i));
    }
  }
}

RemoteSafeBrowsingDatabaseManager::~RemoteSafeBrowsingDatabaseManager() {
  DCHECK(!enabled_);
}

bool RemoteSafeBrowsingDatabaseManager::IsSupported() const {
  return SafeBrowsingApiHandler::GetInstance() != nullptr &&
         is_android_field_trial_enabled_;
}

safe_browsing::ThreatSource RemoteSafeBrowsingDatabaseManager::GetThreatSource()
    const {
  return safe_browsing::ThreatSource::REMOTE;
}

bool RemoteSafeBrowsingDatabaseManager::ChecksAreAlwaysAsync() const {
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::CanCheckResourceType(
    content::ResourceType resource_type) const {
  return resource_types_to_check_.count(resource_type) > 0;
}

bool RemoteSafeBrowsingDatabaseManager::CanCheckUrl(const GURL& url) const {
  return url.SchemeIs(url::kHttpsScheme) || url.SchemeIs(url::kHttpScheme) ||
         url.SchemeIs(url::kFtpScheme);
}

bool RemoteSafeBrowsingDatabaseManager::download_protection_enabled()
    const {
  return false;
}

bool RemoteSafeBrowsingDatabaseManager::CheckDownloadUrl(
    const std::vector<GURL>& url_chain,
    Client* client) {
  NOTREACHED();
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::CheckExtensionIDs(
    const std::set<std::string>& extension_ids,
    Client* client) {
  NOTREACHED();
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::MatchMalwareIP(
    const std::string& ip_address) {
  NOTREACHED();
  return false;
}

bool RemoteSafeBrowsingDatabaseManager::MatchCsdWhitelistUrl(const GURL& url) {
  NOTREACHED();
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::MatchDownloadWhitelistUrl(
    const GURL& url) {
  NOTREACHED();
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::MatchDownloadWhitelistString(
    const std::string& str) {
  NOTREACHED();
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::MatchInclusionWhitelistUrl(
    const GURL& url) {
  NOTREACHED();
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::IsMalwareKillSwitchOn() {
  NOTREACHED();
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::IsCsdWhitelistKillSwitchOn() {
  NOTREACHED();
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::CheckBrowseUrl(const GURL& url,
                                                       Client* client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!enabled_)
    return true;

  bool can_check_url = CanCheckUrl(url);
  UMA_HISTOGRAM_BOOLEAN("SB2.RemoteCall.CanCheckUrl", can_check_url);
  if (!can_check_url)
    return true;  // Safe, continue right away.

  scoped_ptr<ClientRequest> req(new ClientRequest(client, this, url));
  std::vector<SBThreatType> threat_types;  // Not currently used.

  DVLOG(1) << "Checking for client " << client << " and URL " << url;
  SafeBrowsingApiHandler* api_handler = SafeBrowsingApiHandler::GetInstance();
  // This shouldn't happen since SafeBrowsingResourceThrottle checks
  // IsSupported() ealier.
  DCHECK(api_handler) << "SafeBrowsingApiHandler was never constructed";
  api_handler->StartURLCheck(
      base::Bind(&ClientRequest::OnRequestDoneWeak, req->GetWeakPtr()), url,
      threat_types);

  UMA_HISTOGRAM_COUNTS_10000("SB2.RemoteCall.ChecksPending",
                             current_requests_.size());
  current_requests_.push_back(req.release());

  // Defer the resource load.
  return false;
}

void RemoteSafeBrowsingDatabaseManager::CancelCheck(Client* client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK(enabled_);
  for (auto itr = current_requests_.begin(); itr != current_requests_.end();
       ++itr) {
    if ((*itr)->client() == client) {
      DVLOG(2) << "Canceling check for URL " << (*itr)->url();
      delete *itr;
      current_requests_.erase(itr);
      return;
    }
  }
  NOTREACHED();
}

void RemoteSafeBrowsingDatabaseManager::StartOnIOThread() {
  VLOG(1) << "RemoteSafeBrowsingDatabaseManager starting";
  enabled_ = true;
}

void RemoteSafeBrowsingDatabaseManager::StopOnIOThread(bool shutdown) {
  // |shutdown| is not used.
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DVLOG(1) << "RemoteSafeBrowsingDatabaseManager stopping";

  // Call back and delete any remaining clients. OnRequestDone() modifies
  // |current_requests_|, so we make a copy first.
  std::vector<ClientRequest*> to_callback(current_requests_);
  for (auto req : to_callback) {
    DVLOG(1) << "Stopping: Invoking unfinished req for URL " << req->url();
    req->OnRequestDone(SB_THREAT_TYPE_SAFE, std::string());
  }
  enabled_ = false;
}

}  // namespace safe_browsing
