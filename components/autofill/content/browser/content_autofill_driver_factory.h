// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CONTENT_BROWSER_CONTENT_AUTOFILL_DRIVER_FACTORY_H_
#define COMPONENTS_AUTOFILL_CONTENT_BROWSER_CONTENT_AUTOFILL_DRIVER_FACTORY_H_

#include <map>
#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/supports_user_data.h"
#include "components/autofill/content/browser/request_autocomplete_manager.h"
#include "components/autofill/core/browser/autofill_manager.h"
#include "content/public/browser/web_contents_observer.h"

namespace content {
class RenderFrameHost;
}

namespace IPC {
class Message;
}

namespace autofill {

class AutofillDriver;
class ContentAutofillDriver;

// Manages lifetime of ContentAutofillDriver. One Factory per WebContents
// creates one Driver per RenderFrame.
class ContentAutofillDriverFactory : public content::WebContentsObserver,
                                     public base::SupportsUserData::Data {
 public:
  static void CreateForWebContentsAndDelegate(
      content::WebContents* contents,
      AutofillClient* client,
      const std::string& app_locale,
      AutofillManager::AutofillDownloadManagerState enable_download_manager);
  static ContentAutofillDriverFactory* FromWebContents(
      content::WebContents* contents);

  // Gets the |ContentAutofillDriver| associated with |render_frame_host|.
  // |render_frame_host| must be owned by |web_contents()|.
  ContentAutofillDriver* DriverForFrame(
      content::RenderFrameHost* render_frame_host);

  // content::WebContentsObserver:
  bool OnMessageReceived(const IPC::Message& message,
                         content::RenderFrameHost* render_frame_host) override;
  void RenderFrameCreated(content::RenderFrameHost* render_frame_host) override;
  void RenderFrameDeleted(content::RenderFrameHost* render_frame_host) override;
  void DidNavigateAnyFrame(
      content::RenderFrameHost* render_frame_host,
      const content::LoadCommittedDetails& details,
      const content::FrameNavigateParams& params) override;
  void NavigationEntryCommitted(
      const content::LoadCommittedDetails& load_details) override;
  void WasHidden() override;

  static const char kContentAutofillDriverFactoryWebContentsUserDataKey[];

 protected:
  ContentAutofillDriverFactory(
      content::WebContents* web_contents,
      AutofillClient* client,
      const std::string& app_locale,
      AutofillManager::AutofillDownloadManagerState enable_download_manager);
  ~ContentAutofillDriverFactory() override;

 private:
  AutofillClient* client_;
  std::string app_locale_;
  AutofillManager::AutofillDownloadManagerState enable_download_manager_;

  std::map<content::RenderFrameHost*, scoped_ptr<ContentAutofillDriver>>
      frame_driver_map_;
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CONTENT_BROWSER_CONTENT_AUTOFILL_DRIVER_FACTORY_H_
