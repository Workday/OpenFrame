// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/search_engines/search_terms_data_android.h"

#include "chrome/browser/search_engines/search_terms_data.h"
#include "content/public/browser/browser_thread.h"

base::LazyInstance<string16>::Leaky
    SearchTermsDataAndroid::rlz_parameter_value_ = LAZY_INSTANCE_INITIALIZER;
base::LazyInstance<std::string>::Leaky
    SearchTermsDataAndroid::search_client_ = LAZY_INSTANCE_INITIALIZER;

string16 UIThreadSearchTermsData::GetRlzParameterValue() const {
  DCHECK(!content::BrowserThread::IsThreadInitialized(
             content::BrowserThread::UI) ||
         content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  // Android doesn't use the rlz library.  Instead, it manages the rlz string
  // on its own.
  return SearchTermsDataAndroid::rlz_parameter_value_.Get();
}

std::string UIThreadSearchTermsData::GetSearchClient() const {
  DCHECK(!content::BrowserThread::IsThreadInitialized(
             content::BrowserThread::UI) ||
         content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  return SearchTermsDataAndroid::search_client_.Get();
}
