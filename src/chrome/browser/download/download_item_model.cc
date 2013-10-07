// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_item_model.h"

#include "base/i18n/number_formatting.h"
#include "base/i18n/rtl.h"
#include "base/metrics/field_trial.h"
#include "base/strings/string16.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/supports_user_data.h"
#include "base/time/time.h"
#include "chrome/browser/download/download_crx_util.h"
#include "chrome/browser/download/download_util.h"
#include "chrome/browser/safe_browsing/download_feedback_service.h"
#include "content/public/browser/download_danger_type.h"
#include "content/public/browser/download_interrupt_reasons.h"
#include "content/public/browser/download_item.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/text/bytes_formatting.h"
#include "ui/base/text/text_elider.h"

using base::TimeDelta;
using content::DownloadItem;

namespace {

// Per DownloadItem data used by DownloadItemModel. The model doesn't keep any
// state since there could be multiple models associated with a single
// DownloadItem, and the lifetime of the model is shorter than the DownloadItem.
class DownloadItemModelData : public base::SupportsUserData::Data {
 public:
  // Get the DownloadItemModelData object for |download|. Returns NULL if
  // there's no model data.
  static const DownloadItemModelData* Get(const DownloadItem* download);

  // Get the DownloadItemModelData object for |download|. Creates a model data
  // object if not found. Always returns a non-NULL pointer, unless OOM.
  static DownloadItemModelData* GetOrCreate(DownloadItem* download);

  bool should_show_in_shelf() const { return should_show_in_shelf_; }
  void set_should_show_in_shelf(bool should_show_in_shelf) {
    should_show_in_shelf_ = should_show_in_shelf;
  }

  bool should_notify_ui() const { return should_notify_ui_; }
  void set_should_notify_ui(bool should_notify_ui) {
    should_notify_ui_ = should_notify_ui;
  }

 private:
  DownloadItemModelData();
  virtual ~DownloadItemModelData() {}

  static const char kKey[];

  // Whether the download should be displayed in the download shelf. True by
  // default.
  bool should_show_in_shelf_;

  // Whether the UI should be notified when the download is ready to be
  // presented.
  bool should_notify_ui_;
};

// static
const char DownloadItemModelData::kKey[] = "DownloadItemModelData key";

// static
const DownloadItemModelData* DownloadItemModelData::Get(
    const DownloadItem* download) {
  return static_cast<const DownloadItemModelData*>(download->GetUserData(kKey));
}

// static
DownloadItemModelData* DownloadItemModelData::GetOrCreate(
    DownloadItem* download) {
  DownloadItemModelData* data =
      static_cast<DownloadItemModelData*>(download->GetUserData(kKey));
  if (data == NULL) {
    data = new DownloadItemModelData();
    download->SetUserData(kKey, data);
  }
  return data;
}

DownloadItemModelData::DownloadItemModelData()
    : should_show_in_shelf_(true),
      should_notify_ui_(false) {
}

string16 InterruptReasonStatusMessage(int reason) {
  int string_id = 0;

  switch (reason) {
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_ACCESS_DENIED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_ACCESS_DENIED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_DISK_FULL;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_NAME_TOO_LONG:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_PATH_TOO_LONG;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_TOO_LARGE:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_FILE_TOO_LARGE;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_VIRUS_INFECTED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_VIRUS;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_TRANSIENT_ERROR:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_TEMPORARY_PROBLEM;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_BLOCKED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_SECURITY_CHECK_FAILED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_TOO_SHORT:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_FILE_TOO_SHORT;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_NETWORK_ERROR;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_TIMEOUT:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_NETWORK_TIMEOUT;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_NETWORK_DISCONNECTED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_SERVER_DOWN:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_SERVER_DOWN;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_SERVER_FAILED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_SERVER_PROBLEM;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_SERVER_BAD_CONTENT:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_NO_FILE;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED:
      string_id = IDS_DOWNLOAD_STATUS_CANCELLED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_USER_SHUTDOWN:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_SHUTDOWN;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_CRASH:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS_CRASH;
      break;
    default:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS;
      break;
  }

  return l10n_util::GetStringUTF16(string_id);
}

string16 InterruptReasonMessage(int reason) {
  int string_id = 0;
  string16 status_text;

  switch (reason) {
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_ACCESS_DENIED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_ACCESS_DENIED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_DISK_FULL;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_NAME_TOO_LONG:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_PATH_TOO_LONG;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_TOO_LARGE:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_FILE_TOO_LARGE;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_VIRUS_INFECTED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_VIRUS;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_TRANSIENT_ERROR:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_TEMPORARY_PROBLEM;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_BLOCKED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_SECURITY_CHECK_FAILED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_FILE_TOO_SHORT:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_FILE_TOO_SHORT;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_NETWORK_ERROR;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_TIMEOUT:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_NETWORK_TIMEOUT;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_NETWORK_DISCONNECTED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_NETWORK_SERVER_DOWN:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_SERVER_DOWN;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_SERVER_FAILED:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_SERVER_PROBLEM;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_SERVER_BAD_CONTENT:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_NO_FILE;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED:
      string_id = IDS_DOWNLOAD_STATUS_CANCELLED;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_USER_SHUTDOWN:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_SHUTDOWN;
      break;
    case content::DOWNLOAD_INTERRUPT_REASON_CRASH:
      string_id = IDS_DOWNLOAD_INTERRUPTED_DESCRIPTION_CRASH;
      break;
    default:
      string_id = IDS_DOWNLOAD_INTERRUPTED_STATUS;
      break;
  }

  status_text = l10n_util::GetStringUTF16(string_id);

  return status_text;
}

} // namespace

// -----------------------------------------------------------------------------
// DownloadItemModel

DownloadItemModel::DownloadItemModel(DownloadItem* download)
    : download_(download) {
}

DownloadItemModel::~DownloadItemModel() {
}

string16 DownloadItemModel::GetInterruptReasonText() const {
  if (download_->GetState() != DownloadItem::INTERRUPTED ||
      download_->GetLastReason() ==
      content::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED) {
    return string16();
  }
  return InterruptReasonMessage(download_->GetLastReason());
}

string16 DownloadItemModel::GetStatusText() const {
  string16 status_text;
  switch (download_->GetState()) {
    case DownloadItem::IN_PROGRESS:
      status_text = GetInProgressStatusString();
      break;
    case DownloadItem::COMPLETE:
      if (download_->GetFileExternallyRemoved()) {
        status_text = l10n_util::GetStringUTF16(IDS_DOWNLOAD_STATUS_REMOVED);
      } else {
        status_text.clear();
      }
      break;
    case DownloadItem::CANCELLED:
      status_text = l10n_util::GetStringUTF16(IDS_DOWNLOAD_STATUS_CANCELLED);
      break;
    case DownloadItem::INTERRUPTED: {
      content::DownloadInterruptReason reason = download_->GetLastReason();
      if (reason != content::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED) {
        string16 interrupt_reason = InterruptReasonStatusMessage(reason);
        status_text = l10n_util::GetStringFUTF16(
            IDS_DOWNLOAD_STATUS_INTERRUPTED, interrupt_reason);
      } else {
        // Same as DownloadItem::CANCELLED.
        status_text = l10n_util::GetStringUTF16(IDS_DOWNLOAD_STATUS_CANCELLED);
      }
      break;
    }
    default:
      NOTREACHED();
  }

  return status_text;
}

string16 DownloadItemModel::GetTooltipText(const gfx::Font& font,
                                           int max_width) const {
  string16 tooltip = ui::ElideFilename(
      download_->GetFileNameToReportUser(), font, max_width);
  content::DownloadInterruptReason reason = download_->GetLastReason();
  if (download_->GetState() == DownloadItem::INTERRUPTED &&
      reason != content::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED) {
    tooltip += ASCIIToUTF16("\n");
    tooltip += ui::ElideText(InterruptReasonStatusMessage(reason),
                             font, max_width, ui::ELIDE_AT_END);
  }
  return tooltip;
}

string16 DownloadItemModel::GetWarningText(const gfx::Font& font,
                                           int base_width) const {
  // Should only be called if IsDangerous().
  DCHECK(IsDangerous());
  string16 elided_filename =
      ui::ElideFilename(download_->GetFileNameToReportUser(), font, base_width);
  switch (download_->GetDangerType()) {
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL: {
      std::string trial_condition =
          base::FieldTrialList::FindFullName(download_util::kFinchTrialName);
      if (trial_condition.empty())
        return l10n_util::GetStringUTF16(IDS_PROMPT_MALICIOUS_DOWNLOAD_URL);
      return download_util::AssembleMalwareFinchString(trial_condition,
                                                       elided_filename);
    }
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE: {
      if (download_crx_util::IsExtensionDownload(*download_)) {
        return l10n_util::GetStringUTF16(
            IDS_PROMPT_DANGEROUS_DOWNLOAD_EXTENSION);
      } else {
        return l10n_util::GetStringFUTF16(IDS_PROMPT_DANGEROUS_DOWNLOAD,
                                          elided_filename);
      }
    }
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST: {
      std::string trial_condition =
          base::FieldTrialList::FindFullName(download_util::kFinchTrialName);
      if (trial_condition.empty()) {
        return l10n_util::GetStringFUTF16(IDS_PROMPT_MALICIOUS_DOWNLOAD_CONTENT,
                                          elided_filename);
      }
      return download_util::AssembleMalwareFinchString(trial_condition,
                                                       elided_filename);
    }
    case content::DOWNLOAD_DANGER_TYPE_UNCOMMON_CONTENT: {
      return l10n_util::GetStringFUTF16(IDS_PROMPT_UNCOMMON_DOWNLOAD_CONTENT,
                                        elided_filename);
    }
    case content::DOWNLOAD_DANGER_TYPE_POTENTIALLY_UNWANTED: {
      return l10n_util::GetStringFUTF16(
          IDS_PROMPT_DOWNLOAD_CHANGES_SEARCH_SETTINGS, elided_filename);
    }
    case content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS:
    case content::DOWNLOAD_DANGER_TYPE_MAYBE_DANGEROUS_CONTENT:
    case content::DOWNLOAD_DANGER_TYPE_USER_VALIDATED:
    case content::DOWNLOAD_DANGER_TYPE_MAX: {
      break;
    }
  }
  NOTREACHED();
  return string16();
}

string16 DownloadItemModel::GetWarningConfirmButtonText() const {
  // Should only be called if IsDangerous()
  DCHECK(IsDangerous());
  if (download_->GetDangerType() ==
          content::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE &&
      download_crx_util::IsExtensionDownload(*download_)) {
    return l10n_util::GetStringUTF16(IDS_CONTINUE_EXTENSION_DOWNLOAD);
  } else {
    return l10n_util::GetStringUTF16(IDS_CONFIRM_DOWNLOAD);
  }
}

int64 DownloadItemModel::GetCompletedBytes() const {
  return download_->GetReceivedBytes();
}

int64 DownloadItemModel::GetTotalBytes() const {
  return download_->AllDataSaved() ? download_->GetReceivedBytes() :
                                     download_->GetTotalBytes();
}

// TODO(asanka,rdsmith): Once 'open' moves exclusively to the
//     ChromeDownloadManagerDelegate, we should calculate the percentage here
//     instead of calling into the DownloadItem.
int DownloadItemModel::PercentComplete() const {
  return download_->PercentComplete();
}

bool DownloadItemModel::IsDangerous() const {
  return download_->IsDangerous();
}

bool DownloadItemModel::IsMalicious() const {
  if (!IsDangerous())
    return false;
  switch (download_->GetDangerType()) {
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
    case content::DOWNLOAD_DANGER_TYPE_UNCOMMON_CONTENT:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST:
    case content::DOWNLOAD_DANGER_TYPE_POTENTIALLY_UNWANTED:
      return true;

    case content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS:
    case content::DOWNLOAD_DANGER_TYPE_MAYBE_DANGEROUS_CONTENT:
    case content::DOWNLOAD_DANGER_TYPE_USER_VALIDATED:
    case content::DOWNLOAD_DANGER_TYPE_MAX:
      // We shouldn't get any of these due to the IsDangerous() test above.
      NOTREACHED();
      // Fallthrough.
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE:
      return false;
  }
  NOTREACHED();
  return false;
}

bool DownloadItemModel::ShouldAllowDownloadFeedback() const {
  if (!IsDangerous())
    return false;
  return safe_browsing::DownloadFeedbackService::IsEnabledForDownload(
      *download_);
}

bool DownloadItemModel::ShouldRemoveFromShelfWhenComplete() const {
  switch (download_->GetState()) {
    case DownloadItem::IN_PROGRESS:
      // If the download is dangerous or malicious, we should display a warning
      // on the shelf until the user accepts the download.
      if (IsDangerous())
        return false;

      // If the download is an extension, temporary, or will be opened
      // automatically, then it should be removed from the shelf on completion.
      // TODO(asanka): The logic for deciding opening behavior should be in a
      //               central location. http://crbug.com/167702
      return (download_crx_util::IsExtensionDownload(*download_) ||
              download_->IsTemporary() ||
              download_->GetOpenWhenComplete() ||
              download_->ShouldOpenFileBasedOnExtension());

    case DownloadItem::COMPLETE:
      // If the download completed, then rely on GetAutoOpened() to check for
      // opening behavior. This should accurately reflect whether the download
      // was successfully opened.  Extensions, for example, may fail to open.
      return download_->GetAutoOpened() || download_->IsTemporary();

    case DownloadItem::CANCELLED:
    case DownloadItem::INTERRUPTED:
      // Interrupted or cancelled downloads should remain on the shelf.
      return false;

    case DownloadItem::MAX_DOWNLOAD_STATE:
      NOTREACHED();
  }

  NOTREACHED();
  return false;
}

bool DownloadItemModel::ShouldShowDownloadStartedAnimation() const {
  return !download_->IsSavePackageDownload() &&
      !download_crx_util::IsExtensionDownload(*download_);
}

bool DownloadItemModel::ShouldShowInShelf() const {
  const DownloadItemModelData* data = DownloadItemModelData::Get(download_);
  return !data || data->should_show_in_shelf();
}

void DownloadItemModel::SetShouldShowInShelf(bool should_show) {
  DownloadItemModelData* data = DownloadItemModelData::GetOrCreate(download_);
  data->set_should_show_in_shelf(should_show);
}

bool DownloadItemModel::ShouldNotifyUI() const {
  const DownloadItemModelData* data = DownloadItemModelData::Get(download_);
  return data && data->should_notify_ui();
}

void DownloadItemModel::SetShouldNotifyUI(bool should_notify) {
  DownloadItemModelData* data = DownloadItemModelData::GetOrCreate(download_);
  data->set_should_notify_ui(should_notify);
}

string16 DownloadItemModel::GetProgressSizesString() const {
  string16 size_ratio;
  int64 size = GetCompletedBytes();
  int64 total = GetTotalBytes();
  if (total > 0) {
    ui::DataUnits amount_units = ui::GetByteDisplayUnits(total);
    string16 simple_size = ui::FormatBytesWithUnits(size, amount_units, false);

    // In RTL locales, we render the text "size/total" in an RTL context. This
    // is problematic since a string such as "123/456 MB" is displayed
    // as "MB 123/456" because it ends with an LTR run. In order to solve this,
    // we mark the total string as an LTR string if the UI layout is
    // right-to-left so that the string "456 MB" is treated as an LTR run.
    string16 simple_total = base::i18n::GetDisplayStringInLTRDirectionality(
        ui::FormatBytesWithUnits(total, amount_units, true));
    size_ratio = l10n_util::GetStringFUTF16(IDS_DOWNLOAD_STATUS_SIZES,
                                            simple_size, simple_total);
  } else {
    size_ratio = ui::FormatBytes(size);
  }
  return size_ratio;
}

string16 DownloadItemModel::GetInProgressStatusString() const {
  DCHECK_EQ(DownloadItem::IN_PROGRESS, download_->GetState());

  TimeDelta time_remaining;
  // time_remaining is only known if the download isn't paused.
  bool time_remaining_known = (!download_->IsPaused() &&
                               download_->TimeRemaining(&time_remaining));

  // Indication of progress. (E.g.:"100/200 MB" or "100MB")
  string16 size_ratio = GetProgressSizesString();

  // The download is a CRX (app, extension, theme, ...) and it is being unpacked
  // and validated.
  if (download_->AllDataSaved() &&
      download_crx_util::IsExtensionDownload(*download_)) {
    return l10n_util::GetStringUTF16(IDS_DOWNLOAD_STATUS_CRX_INSTALL_RUNNING);
  }

  // A paused download: "100/120 MB, Paused"
  if (download_->IsPaused()) {
    return l10n_util::GetStringFUTF16(
        IDS_DOWNLOAD_STATUS_IN_PROGRESS, size_ratio,
        l10n_util::GetStringUTF16(IDS_DOWNLOAD_PROGRESS_PAUSED));
  }

  // A download scheduled to be opened when complete: "Opening in 10 secs"
  if (download_->GetOpenWhenComplete()) {
    if (!time_remaining_known)
      return l10n_util::GetStringUTF16(IDS_DOWNLOAD_STATUS_OPEN_WHEN_COMPLETE);

    return l10n_util::GetStringFUTF16(
        IDS_DOWNLOAD_STATUS_OPEN_IN,
        ui::TimeFormat::TimeRemainingShort(time_remaining));
  }

  // In progress download with known time left: "100/120 MB, 10 secs left"
  if (time_remaining_known) {
    return l10n_util::GetStringFUTF16(
        IDS_DOWNLOAD_STATUS_IN_PROGRESS, size_ratio,
        ui::TimeFormat::TimeRemaining(time_remaining));
  }

  // In progress download with no known time left and non-zero completed bytes:
  // "100/120 MB" or "100 MB"
  if (GetCompletedBytes() > 0)
    return size_ratio;

  // Instead of displaying "0 B" we say "Starting..."
  return l10n_util::GetStringUTF16(IDS_DOWNLOAD_STATUS_STARTING);
}
