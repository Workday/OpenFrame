// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/feedback_private/feedback_service.h"

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"

using extensions::api::feedback_private::SystemInformation;

namespace extensions {

class FeedbackServiceImpl
    : public FeedbackService,
      public base::SupportsWeakPtr<FeedbackServiceImpl> {
 public:
  FeedbackServiceImpl();
  virtual ~FeedbackServiceImpl();

  virtual std::string GetUserEmail() OVERRIDE;
  virtual void GetSystemInformation(
      const GetSystemInformationCallback& callback) OVERRIDE;

 private:
  // Overridden from FeedbackService:
  virtual base::WeakPtr<FeedbackService> GetWeakPtr() OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(FeedbackServiceImpl);
};

FeedbackService* FeedbackService::CreateInstance() {
  return new FeedbackServiceImpl;
}

FeedbackServiceImpl::FeedbackServiceImpl() {
}

FeedbackServiceImpl::~FeedbackServiceImpl() {
}

std::string FeedbackServiceImpl::GetUserEmail() {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  if (!profile)
    return std::string();

  SigninManager* signin = SigninManagerFactory::GetForProfile(profile);
  if (!signin)
    return std::string();

  return signin->GetAuthenticatedUsername();
}

void FeedbackServiceImpl::GetSystemInformation(
    const GetSystemInformationCallback& callback) {
  system_information_callback_ = callback;

  SystemInformationList sys_info_list;
  system_information_callback_.Run(sys_info_list);
}

base::WeakPtr<FeedbackService> FeedbackServiceImpl::GetWeakPtr() {
  return AsWeakPtr();
}

}  // namespace extensions
