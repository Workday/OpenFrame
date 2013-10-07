// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_AUTOFILL_TAB_AUTOFILL_MANAGER_DELEGATE_H_
#define CHROME_BROWSER_UI_AUTOFILL_TAB_AUTOFILL_MANAGER_DELEGATE_H_

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/i18n/rtl.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/autofill/autofill_dialog_types.h"
#include "components/autofill/content/browser/autocheckout_steps.h"
#include "components/autofill/core/browser/autocheckout_bubble_state.h"
#include "components/autofill/core/browser/autofill_manager_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
struct FrameNavigateParams;
struct LoadCommittedDetails;
class WebContents;
}

namespace autofill {

class AutocheckoutBubble;
class AutofillDialogController;
class AutofillPopupControllerImpl;
struct FormData;

// Chrome implementation of AutofillManagerDelegate.
class TabAutofillManagerDelegate
    : public AutofillManagerDelegate,
      public content::WebContentsUserData<TabAutofillManagerDelegate>,
      public content::WebContentsObserver {
 public:
  virtual ~TabAutofillManagerDelegate();

  // Called when the tab corresponding to |this| instance is activated.
  void TabActivated(int reason);

  // AutofillManagerDelegate implementation.
  virtual PersonalDataManager* GetPersonalDataManager() OVERRIDE;
  virtual PrefService* GetPrefs() OVERRIDE;
  virtual autocheckout::WhitelistManager*
      GetAutocheckoutWhitelistManager() const OVERRIDE;
  virtual void HideRequestAutocompleteDialog() OVERRIDE;
  virtual void OnAutocheckoutError() OVERRIDE;
  virtual void OnAutocheckoutSuccess() OVERRIDE;
  virtual void ShowAutofillSettings() OVERRIDE;
  virtual void ConfirmSaveCreditCard(
      const AutofillMetrics& metric_logger,
      const CreditCard& credit_card,
      const base::Closure& save_card_callback) OVERRIDE;
  virtual bool ShowAutocheckoutBubble(
      const gfx::RectF& bounds,
      bool is_google_user,
      const base::Callback<void(AutocheckoutBubbleState)>& callback) OVERRIDE;
  virtual void HideAutocheckoutBubble() OVERRIDE;
  virtual void ShowRequestAutocompleteDialog(
      const FormData& form,
      const GURL& source_url,
      DialogType dialog_type,
      const base::Callback<void(const FormStructure*,
                                const std::string&)>& callback) OVERRIDE;
  virtual void ShowAutofillPopup(
      const gfx::RectF& element_bounds,
      base::i18n::TextDirection text_direction,
      const std::vector<string16>& values,
      const std::vector<string16>& labels,
      const std::vector<string16>& icons,
      const std::vector<int>& identifiers,
      base::WeakPtr<AutofillPopupDelegate> delegate) OVERRIDE;
  virtual void UpdateAutofillPopupDataListValues(
      const std::vector<base::string16>& values,
      const std::vector<base::string16>& labels) OVERRIDE;
  virtual void HideAutofillPopup() OVERRIDE;
  virtual bool IsAutocompleteEnabled() OVERRIDE;

  virtual void AddAutocheckoutStep(AutocheckoutStepType step_type) OVERRIDE;
  virtual void UpdateAutocheckoutStep(
      AutocheckoutStepType step_type,
      AutocheckoutStepStatus step_status) OVERRIDE;

  // content::WebContentsObserver implementation.
  virtual void DidNavigateMainFrame(
      const content::LoadCommittedDetails& details,
      const content::FrameNavigateParams& params) OVERRIDE;
  virtual void WebContentsDestroyed(
      content::WebContents* web_contents) OVERRIDE;
  virtual void WasShown() OVERRIDE;

  // Exposed for testing.
  AutofillDialogController* GetDialogControllerForTesting() {
    return dialog_controller_.get();
  }

 private:
  explicit TabAutofillManagerDelegate(content::WebContents* web_contents);
  friend class content::WebContentsUserData<TabAutofillManagerDelegate>;

  content::WebContents* const web_contents_;
  base::WeakPtr<AutofillDialogController> dialog_controller_;
  base::WeakPtr<AutocheckoutBubble> autocheckout_bubble_;
  base::WeakPtr<AutofillPopupControllerImpl> popup_controller_;

  DISALLOW_COPY_AND_ASSIGN(TabAutofillManagerDelegate);
};

}  // namespace autofill

#endif  // CHROME_BROWSER_UI_AUTOFILL_TAB_AUTOFILL_MANAGER_DELEGATE_H_
