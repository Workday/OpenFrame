// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/policy_service_impl.h"

#include <algorithm>

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/stl_util.h"
#include "chrome/browser/policy/policy_domain_descriptor.h"
#include "chrome/browser/policy/policy_map.h"

namespace policy {

typedef PolicyServiceImpl::Providers::const_iterator Iterator;

PolicyServiceImpl::PolicyServiceImpl(const Providers& providers)
    : update_task_ptr_factory_(this) {
  for (int domain = 0; domain < POLICY_DOMAIN_SIZE; ++domain)
    initialization_complete_[domain] = true;
  providers_ = providers;
  for (Iterator it = providers.begin(); it != providers.end(); ++it) {
    ConfigurationPolicyProvider* provider = *it;
    provider->AddObserver(this);
    for (int domain = 0; domain < POLICY_DOMAIN_SIZE; ++domain) {
      initialization_complete_[domain] &=
          provider->IsInitializationComplete(static_cast<PolicyDomain>(domain));
    }
  }
  // There are no observers yet, but calls to GetPolicies() should already get
  // the processed policy values.
  MergeAndTriggerUpdates();
}

PolicyServiceImpl::~PolicyServiceImpl() {
  for (Iterator it = providers_.begin(); it != providers_.end(); ++it)
    (*it)->RemoveObserver(this);
  STLDeleteValues(&observers_);
}

void PolicyServiceImpl::AddObserver(PolicyDomain domain,
                                    PolicyService::Observer* observer) {
  Observers*& list = observers_[domain];
  if (!list)
    list = new Observers();
  list->AddObserver(observer);
}

void PolicyServiceImpl::RemoveObserver(PolicyDomain domain,
                                       PolicyService::Observer* observer) {
  ObserverMap::iterator it = observers_.find(domain);
  if (it == observers_.end()) {
    NOTREACHED();
    return;
  }
  it->second->RemoveObserver(observer);
  if (it->second->size() == 0) {
    delete it->second;
    observers_.erase(it);
  }
}

void PolicyServiceImpl::RegisterPolicyDomain(
    scoped_refptr<const PolicyDomainDescriptor> descriptor) {
  domain_descriptors_[descriptor->domain()] = descriptor;
  for (Iterator it = providers_.begin(); it != providers_.end(); ++it)
    (*it)->RegisterPolicyDomain(descriptor);
}

const PolicyMap& PolicyServiceImpl::GetPolicies(
    const PolicyNamespace& ns) const {
  return policy_bundle_.Get(ns);
}

scoped_refptr<const PolicyDomainDescriptor>
PolicyServiceImpl::GetPolicyDomainDescriptor(PolicyDomain domain) const {
  return domain_descriptors_[domain];
}

bool PolicyServiceImpl::IsInitializationComplete(PolicyDomain domain) const {
  DCHECK(domain >= 0 && domain < POLICY_DOMAIN_SIZE);
  return initialization_complete_[domain];
}

void PolicyServiceImpl::RefreshPolicies(const base::Closure& callback) {
  if (!callback.is_null())
    refresh_callbacks_.push_back(callback);

  if (providers_.empty()) {
    // Refresh is immediately complete if there are no providers. See the note
    // on OnUpdatePolicy() about why this is a posted task.
    update_task_ptr_factory_.InvalidateWeakPtrs();
    base::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&PolicyServiceImpl::MergeAndTriggerUpdates,
                   update_task_ptr_factory_.GetWeakPtr()));
  } else {
    // Some providers might invoke OnUpdatePolicy synchronously while handling
    // RefreshPolicies. Mark all as pending before refreshing.
    for (Iterator it = providers_.begin(); it != providers_.end(); ++it)
      refresh_pending_.insert(*it);
    for (Iterator it = providers_.begin(); it != providers_.end(); ++it)
      (*it)->RefreshPolicies();
  }
}

void PolicyServiceImpl::OnUpdatePolicy(ConfigurationPolicyProvider* provider) {
  DCHECK_EQ(1, std::count(providers_.begin(), providers_.end(), provider));
  refresh_pending_.erase(provider);

  // Note: a policy change may trigger further policy changes in some providers.
  // For example, disabling SigninAllowed would cause the CloudPolicyManager to
  // drop all its policies, which makes this method enter again for that
  // provider.
  //
  // Therefore this update is posted asynchronously, to prevent reentrancy in
  // MergeAndTriggerUpdates. Also, cancel a pending update if there is any,
  // since both will produce the same PolicyBundle.
  update_task_ptr_factory_.InvalidateWeakPtrs();
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&PolicyServiceImpl::MergeAndTriggerUpdates,
                 update_task_ptr_factory_.GetWeakPtr()));
}

void PolicyServiceImpl::NotifyNamespaceUpdated(
    const PolicyNamespace& ns,
    const PolicyMap& previous,
    const PolicyMap& current) {
  ObserverMap::iterator iterator = observers_.find(ns.domain);
  if (iterator != observers_.end()) {
    FOR_EACH_OBSERVER(PolicyService::Observer,
                      *iterator->second,
                      OnPolicyUpdated(ns, previous, current));
  }
}

void PolicyServiceImpl::MergeAndTriggerUpdates() {
  // Merge from each provider in their order of priority.
  PolicyBundle bundle;
  for (Iterator it = providers_.begin(); it != providers_.end(); ++it)
    bundle.MergeFrom((*it)->policies());

  // Swap first, so that observers that call GetPolicies() see the current
  // values.
  policy_bundle_.Swap(&bundle);

  // Only notify observers of namespaces that have been modified.
  const PolicyMap kEmpty;
  PolicyBundle::const_iterator it_new = policy_bundle_.begin();
  PolicyBundle::const_iterator end_new = policy_bundle_.end();
  PolicyBundle::const_iterator it_old = bundle.begin();
  PolicyBundle::const_iterator end_old = bundle.end();
  while (it_new != end_new && it_old != end_old) {
    if (it_new->first < it_old->first) {
      // A new namespace is available.
      NotifyNamespaceUpdated(it_new->first, kEmpty, *it_new->second);
      ++it_new;
    } else if (it_old->first < it_new->first) {
      // A previously available namespace is now gone.
      NotifyNamespaceUpdated(it_old->first, *it_old->second, kEmpty);
      ++it_old;
    } else {
      if (!it_new->second->Equals(*it_old->second)) {
        // An existing namespace's policies have changed.
        NotifyNamespaceUpdated(it_new->first, *it_old->second, *it_new->second);
      }
      ++it_new;
      ++it_old;
    }
  }

  // Send updates for the remaining new namespaces, if any.
  for (; it_new != end_new; ++it_new)
    NotifyNamespaceUpdated(it_new->first, kEmpty, *it_new->second);

  // Sends updates for the remaining removed namespaces, if any.
  for (; it_old != end_old; ++it_old)
    NotifyNamespaceUpdated(it_old->first, *it_old->second, kEmpty);

  CheckInitializationComplete();
  CheckRefreshComplete();
}

void PolicyServiceImpl::CheckInitializationComplete() {
  // Check if all the providers just became initialized for each domain; if so,
  // notify that domain's observers.
  for (int domain = 0; domain < POLICY_DOMAIN_SIZE; ++domain) {
    if (initialization_complete_[domain])
      continue;

    PolicyDomain policy_domain = static_cast<PolicyDomain>(domain);

    bool all_complete = true;
    for (Iterator it = providers_.begin(); it != providers_.end(); ++it) {
      if (!(*it)->IsInitializationComplete(policy_domain)) {
        all_complete = false;
        break;
      }
    }
    if (all_complete) {
      initialization_complete_[domain] = true;
      ObserverMap::iterator iter = observers_.find(policy_domain);
      if (iter != observers_.end()) {
        FOR_EACH_OBSERVER(PolicyService::Observer,
                          *iter->second,
                          OnPolicyServiceInitialized(policy_domain));
      }
    }
  }
}

void PolicyServiceImpl::CheckRefreshComplete() {
  // Invoke all the callbacks if a refresh has just fully completed.
  if (refresh_pending_.empty() && !refresh_callbacks_.empty()) {
    std::vector<base::Closure> callbacks;
    callbacks.swap(refresh_callbacks_);
    std::vector<base::Closure>::iterator it;
    for (it = callbacks.begin(); it != callbacks.end(); ++it)
      it->Run();
  }
}

}  // namespace policy
