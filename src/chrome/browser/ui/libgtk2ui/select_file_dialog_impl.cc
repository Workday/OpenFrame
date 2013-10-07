// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file implements common select dialog functionality between GTK and KDE.

#include "chrome/browser/ui/libgtk2ui/select_file_dialog_impl.h"

#include "base/environment.h"
#include "base/file_util.h"
#include "base/nix/xdg_util.h"
#include "base/threading/thread_restrictions.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace {

enum UseKdeFileDialogStatus {
  UNKNOWN,
  NO_KDE,
  YES_KDE
};

UseKdeFileDialogStatus use_kde_ = UNKNOWN;

}  // namespace

namespace libgtk2ui {

base::FilePath* SelectFileDialogImpl::last_saved_path_ = NULL;
base::FilePath* SelectFileDialogImpl::last_opened_path_ = NULL;

// static
ui::SelectFileDialog* SelectFileDialogImpl::Create(
    ui::SelectFileDialog::Listener* listener,
    ui::SelectFilePolicy* policy) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (use_kde_ == UNKNOWN) {
    // Start out assumimg we are not going to use KDE.
    use_kde_ = NO_KDE;

    // Check to see if KDE is the desktop environment.
    scoped_ptr<base::Environment> env(base::Environment::Create());
    base::nix::DesktopEnvironment desktop =
        base::nix::GetDesktopEnvironment(env.get());
    if (desktop == base::nix::DESKTOP_ENVIRONMENT_KDE3 ||
        desktop == base::nix::DESKTOP_ENVIRONMENT_KDE4) {
      // Check to see if the user dislikes the KDE file dialog.
      if (!env->HasVar("NO_CHROME_KDE_FILE_DIALOG")) {
        // Check to see if the KDE dialog works.
        if (SelectFileDialogImpl::CheckKDEDialogWorksOnUIThread()) {
          use_kde_ = YES_KDE;
        }
      }
    }
  }

  if (use_kde_ == NO_KDE)
    return SelectFileDialogImpl::NewSelectFileDialogImplGTK(listener, policy);

  scoped_ptr<base::Environment> env(base::Environment::Create());
  base::nix::DesktopEnvironment desktop =
      base::nix::GetDesktopEnvironment(env.get());
  return SelectFileDialogImpl::NewSelectFileDialogImplKDE(
      listener, policy, desktop);
}

SelectFileDialogImpl::SelectFileDialogImpl(Listener* listener,
                                           ui::SelectFilePolicy* policy)
    : SelectFileDialog(listener, policy),
      file_type_index_(0),
      type_(SELECT_NONE) {
  if (!last_saved_path_) {
    last_saved_path_ = new base::FilePath();
    last_opened_path_ = new base::FilePath();
  }
}

SelectFileDialogImpl::~SelectFileDialogImpl() { }

void SelectFileDialogImpl::ListenerDestroyed() {
  listener_ = NULL;
}

bool SelectFileDialogImpl::IsRunning(gfx::NativeWindow parent_window) const {
  return parents_.find(parent_window) != parents_.end();
}

bool SelectFileDialogImpl::CallDirectoryExistsOnUIThread(
    const base::FilePath& path) {
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  return base::DirectoryExists(path);
}

}  // namespace libgtk2ui
