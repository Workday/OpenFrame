// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_TEST_AUTOFILL_MANAGER_DELEGATE_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_TEST_AUTOFILL_MANAGER_DELEGATE_H_

#include "base/compiler_specific.h"
#include "base/i18n/rtl.h"
#include "components/autofill/core/browser/autocheckout_bubble_state.h"
#include "components/autofill/core/browser/autofill_manager_delegate.h"

namespace autofill {

// This class is only for easier writing of testings. All pure virtual functions
// have been giving empty methods.
class TestAutofillManagerDelegate : public AutofillManagerDelegate {
 public:
  TestAutofillManagerDelegate();
  virtual ~TestAutofillManagerDelegate();

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
      const gfx::RectF& bounding_box,
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
      const std::vector<base::string16>& values,
      const std::vector<base::string16>& labels,
      const std::vector<base::string16>& icons,
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

 private:
  DISALLOW_COPY_AND_ASSIGN(TestAutofillManagerDelegate);
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_TEST_AUTOFILL_MANAGER_DELEGATE_H_
