// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_COMPONENT_UPDATER_CRX_UPDATE_ITEM_H_
#define CHROME_BROWSER_COMPONENT_UPDATER_CRX_UPDATE_ITEM_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/time/time.h"
#include "base/version.h"
#include "chrome/browser/component_updater/component_updater_service.h"

// This is the one and only per-item state structure. Designed to be hosted
// in a std::vector or a std::list. The two main members are |component|
// which is supplied by the the component updater client and |status| which
// is modified as the item is processed by the update pipeline. The expected
// transition graph is:
//
//                                 kNew
//                                  |
//                                  V
//     +----------------------> kChecking -<---------+-----<-------+
//     |                            |                |             |
//     |              error         V       no       |             |
//  kNoUpdate <---------------- [update?] ->---- kUpToDate     kUpdated
//     ^                            |                              ^
//     |                        yes |                              |
//     |        diff=false          V                              |
//     |          +-----------> kCanUpdate                         |
//     |          |                 |                              |
//     |          |                 V              no              |
//     |          |        [differential update?]->----+           |
//     |          |                 |                  |           |
//     |          |             yes |                  |           |
//     |          |   error         V                  |           |
//     |          +---------<- kDownloadingDiff        |           |
//     |          |                 |                  |           |
//     |          |                 |                  |           |
//     |          |   error         V                  |           |
//     |          +---------<- kUpdatingDiff ->--------|-----------+ success
//     |                                               |           |
//     |              error                            V           |
//     +----------------------------------------- kDownloading     |
//     |                                               |           |
//     |              error                            V           |
//     +------------------------------------------ kUpdating ->----+ success
//
struct CrxUpdateItem {
  enum Status {
    kNew,
    kChecking,
    kCanUpdate,
    kDownloadingDiff,
    kDownloading,
    kUpdatingDiff,
    kUpdating,
    kUpdated,
    kUpToDate,
    kNoUpdate,
    kLastStatus
  };

  Status status;
  std::string id;
  CrxComponent component;

  base::Time last_check;

  // The url the full and differential update CRXs are downloaded from.
  GURL crx_url;
  GURL diff_crx_url;

  // The from/to version and fingerprint values.
  Version previous_version;
  Version next_version;
  std::string previous_fp;
  std::string next_fp;

  // True if the differential update failed for any reason.
  bool diff_update_failed;

  // The error information for full and differential updates.
  // The |error_category| contains a hint about which module in the component
  // updater generated the error. The |error_code| constains the error and
  // the |extra_code1| usually contains a system error, but it can contain
  // any extended information that is relevant to either the category or the
  // error itself.
  int error_category;
  int error_code;
  int extra_code1;
  int diff_error_category;
  int diff_error_code;
  int diff_extra_code1;

  CrxUpdateItem();
  ~CrxUpdateItem();

  // Function object used to find a specific component.
  class FindById {
   public:
    explicit FindById(const std::string& id) : id_(id) {}

    bool operator() (CrxUpdateItem* item) const {
      return (item->id == id_);
    }
   private:
    const std::string& id_;
  };
};

#endif  // CHROME_BROWSER_COMPONENT_UPDATER_CRX_UPDATE_ITEM_H_
