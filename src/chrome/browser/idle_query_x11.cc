// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/idle_query_x11.h"

#include <X11/extensions/scrnsaver.h>
#include "ui/base/x/x11_util.h"

namespace chrome {

class IdleData {
 public:
  IdleData() {
    int event_base;
    int error_base;
    if (XScreenSaverQueryExtension(ui::GetXDisplay(), &event_base,
                                   &error_base)) {
      mit_info = XScreenSaverAllocInfo();
    } else {
      mit_info = NULL;
    }
  }

  ~IdleData() {
    if (mit_info)
      XFree(mit_info);
  }

  XScreenSaverInfo *mit_info;
};

IdleQueryX11::IdleQueryX11() : idle_data_(new IdleData()) {}

IdleQueryX11::~IdleQueryX11() {}

int IdleQueryX11::IdleTime() {
  if (!idle_data_->mit_info)
    return 0;

  if (XScreenSaverQueryInfo(ui::GetXDisplay(),
                            RootWindow(ui::GetXDisplay(), 0),
                            idle_data_->mit_info)) {
    return (idle_data_->mit_info->idle) / 1000;
  } else {
    return 0;
  }
}

}  // namespace chrome
