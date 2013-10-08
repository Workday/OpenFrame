// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/autofill/autofill_dialog_models.h"

#include "base/bind.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/common/pref_names.h"
#include "components/autofill/core/browser/autofill_country.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

namespace autofill {

SuggestionsMenuModelDelegate::~SuggestionsMenuModelDelegate() {}

// SuggestionsMenuModel ----------------------------------------------------

SuggestionsMenuModel::SuggestionsMenuModel(
    SuggestionsMenuModelDelegate* delegate)
    : ui::SimpleMenuModel(this),
      delegate_(delegate),
      checked_item_(0) {}

SuggestionsMenuModel::~SuggestionsMenuModel() {}

void SuggestionsMenuModel::AddKeyedItem(
    const std::string& key, const string16& display_label) {
  Item item = { key, true };
  items_.push_back(item);
  AddCheckItem(items_.size() - 1, display_label);
}

void SuggestionsMenuModel::AddKeyedItemWithIcon(
    const std::string& key,
    const string16& display_label,
    const gfx::Image& icon) {
  AddKeyedItem(key, display_label);
  SetIcon(items_.size() - 1, icon);
}

void SuggestionsMenuModel::AddKeyedItemWithSublabel(
    const std::string& key,
    const string16& display_label,
    const string16& display_sublabel) {
  AddKeyedItem(key, display_label);
  SetSublabel(items_.size() - 1, display_sublabel);
}

void SuggestionsMenuModel::AddKeyedItemWithSublabelAndIcon(
    const std::string& key,
    const string16& display_label,
    const string16& display_sublabel,
    const gfx::Image& icon) {
  AddKeyedItemWithIcon(key, display_label, icon);
  SetSublabel(items_.size() - 1, display_sublabel);
}

void SuggestionsMenuModel::Reset() {
  checked_item_ = 0;
  items_.clear();
  Clear();
}

std::string SuggestionsMenuModel::GetItemKeyAt(int index) const {
  return items_[index].key;
}

std::string SuggestionsMenuModel::GetItemKeyForCheckedItem() const {
  if (items_.empty())
    return std::string();

  return items_[checked_item_].key;
}

void SuggestionsMenuModel::SetCheckedItem(const std::string& item_key) {
  SetCheckedItemNthWithKey(item_key, 1);
}

void SuggestionsMenuModel::SetCheckedIndex(size_t index) {
  DCHECK_LT(index, items_.size());
  checked_item_ = index;
}

void SuggestionsMenuModel::SetCheckedItemNthWithKey(const std::string& item_key,
                                                    size_t n) {
  for (size_t i = 0; i < items_.size(); ++i) {
    if (items_[i].key == item_key) {
      checked_item_ = i;
      if (n-- <= 1)
        return;
    }
  }
}

void SuggestionsMenuModel::SetEnabled(const std::string& item_key,
                                      bool enabled) {
  items_[GetItemIndex(item_key)].enabled = enabled;
}

bool SuggestionsMenuModel::IsCommandIdChecked(
    int command_id) const {
  return checked_item_ == command_id;
}

bool SuggestionsMenuModel::IsCommandIdEnabled(
    int command_id) const {
  // Please note: command_id is same as the 0-based index in |items_|.
  DCHECK_GE(command_id, 0);
  DCHECK_LT(static_cast<size_t>(command_id), items_.size());
  return items_[command_id].enabled;
}

bool SuggestionsMenuModel::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) {
  return false;
}

void SuggestionsMenuModel::ExecuteCommand(int command_id, int event_flags) {
  delegate_->SuggestionItemSelected(this, command_id);
}

size_t SuggestionsMenuModel::GetItemIndex(const std::string& item_key) {
  for (size_t i = 0; i < items_.size(); ++i) {
    if (items_[i].key == item_key)
      return i;
  }

  NOTREACHED();
  return 0;
}

// MonthComboboxModel ----------------------------------------------------------

MonthComboboxModel::MonthComboboxModel() {}

MonthComboboxModel::~MonthComboboxModel() {}

int MonthComboboxModel::GetItemCount() const {
  // 12 months plus the empty entry.
  return 13;
}

// static
string16 MonthComboboxModel::FormatMonth(int index) {
  return ASCIIToUTF16(base::StringPrintf("%.2d", index));
}

string16 MonthComboboxModel::GetItemAt(int index) {
  return index == 0 ?
      l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_PLACEHOLDER_EXPIRY_MONTH) :
      FormatMonth(index);
}

// YearComboboxModel -----------------------------------------------------------

YearComboboxModel::YearComboboxModel() : this_year_(0) {
  base::Time time = base::Time::Now();
  base::Time::Exploded exploded;
  time.LocalExplode(&exploded);
  this_year_ = exploded.year;
}

YearComboboxModel::~YearComboboxModel() {}

int YearComboboxModel::GetItemCount() const {
  // 10 years plus the empty entry.
  return 11;
}

string16 YearComboboxModel::GetItemAt(int index) {
  if (index == 0) {
    return l10n_util::GetStringUTF16(
        IDS_AUTOFILL_DIALOG_PLACEHOLDER_EXPIRY_YEAR);
  }

  return base::IntToString16(this_year_ + index - 1);
}

}  // namespace autofill
