// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROME_BROWSER_MAIN_EXTRA_PARTS_EXO_H_
#define CHROME_BROWSER_CHROME_BROWSER_MAIN_EXTRA_PARTS_EXO_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"

namespace exo {
class Display;
namespace wayland {
class Server;
}
}

class ChromeBrowserMainExtraPartsExo : public ChromeBrowserMainExtraParts {
 public:
  ChromeBrowserMainExtraPartsExo();
  ~ChromeBrowserMainExtraPartsExo() override;

  // Overridden from ChromeBrowserMainExtraParts:
  void PreProfileInit() override;
  void PostMainMessageLoopRun() override;

 private:
  scoped_ptr<exo::Display> display_;
  scoped_ptr<exo::wayland::Server> wayland_server_;
  class WaylandWatcher;
  scoped_ptr<WaylandWatcher> wayland_watcher_;

  DISALLOW_COPY_AND_ASSIGN(ChromeBrowserMainExtraPartsExo);
};

#endif  // CHROME_BROWSER_CHROME_BROWSER_MAIN_EXTRA_PARTS_EXO_H_
