// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/extensions/extension_installed_bubble.h"

#include <string>

#include "base/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/commands/command_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/extensions/api/extension_action/action_info.h"
#include "chrome/common/extensions/api/omnibox/omnibox_handler.h"
#include "chrome/common/extensions/command.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_source.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "ui/base/l10n/l10n_util.h"

using extensions::Extension;

namespace {

// How long to wait for browser action animations to complete before retrying.
const int kAnimationWaitMs = 50;
// How often we retry when waiting for browser action animation to end.
const int kAnimationWaitRetries = 10;

// Class responsible for showing the bubble after it's installed. Owns itself.
class ExtensionInstalledBubbleObserver
    : public content::NotificationObserver,
      public extensions::ExtensionRegistryObserver {
 public:
  explicit ExtensionInstalledBubbleObserver(
      scoped_ptr<ExtensionInstalledBubble> bubble)
      : bubble_(bubble.Pass()),
        extension_registry_observer_(this),
        animation_wait_retries_(0),
        weak_factory_(this) {
    // |extension| has been initialized but not loaded at this point. We need to
    // wait on showing the Bubble until the EXTENSION_LOADED gets fired.
    extension_registry_observer_.Add(
        extensions::ExtensionRegistry::Get(bubble_->browser()->profile()));

    registrar_.Add(this, chrome::NOTIFICATION_BROWSER_CLOSING,
                   content::Source<Browser>(bubble_->browser()));
  }

 private:
  ~ExtensionInstalledBubbleObserver() override {}

  // content::NotificationObserver:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override {
    DCHECK_EQ(type, chrome::NOTIFICATION_BROWSER_CLOSING)
        << "Received unexpected notification";
    // Browser is closing before the bubble was shown.
    // TODO(hcarmona): Look into logging this with the BubbleManager.
    delete this;
  }

  // extensions::ExtensionRegistryObserver:
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const extensions::Extension* extension) override {
    if (extension == bubble_->extension()) {
      bubble_->OnExtensionLoaded();
      // PostTask to ourself to allow all EXTENSION_LOADED Observers to run.
      // Only then can we be sure that a BrowserAction or PageAction has had
      // views created which we can inspect for the purpose of previewing of
      // pointing to them.
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(&ExtensionInstalledBubbleObserver::Show,
                                weak_factory_.GetWeakPtr()));
    }
  }

  void OnExtensionUnloaded(
      content::BrowserContext* browser_context,
      const extensions::Extension* extension,
      extensions::UnloadedExtensionInfo::Reason reason) override {
    if (extension == bubble_->extension()) {
      // Extension is going away.
      delete this;
    }
  }

  // Called internally via PostTask to show the bubble.
  void Show() {
    DCHECK(bubble_);
    // TODO(hcarmona): Investigate having the BubbleManager query the bubble
    // for |ShouldShow|. This is important because the BubbleManager may decide
    // to delay showing the bubble.
    if (bubble_->ShouldShow()) {
      // Must be 2 lines because the manager will take ownership of bubble.
      BubbleManager* manager = bubble_->browser()->GetBubbleManager();
      manager->ShowBubble(bubble_.Pass());
      delete this;
      return;
    }
    if (animation_wait_retries_++ < kAnimationWaitRetries) {
      base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
          FROM_HERE, base::Bind(&ExtensionInstalledBubbleObserver::Show,
                                weak_factory_.GetWeakPtr()),
          base::TimeDelta::FromMilliseconds(kAnimationWaitMs));
    } else {
      // Retries are over; won't try again.
      // TODO(hcarmona): Look into logging this with the BubbleManager.
      delete this;
    }
  }

  // The bubble that will be shown when the extension has finished installing.
  scoped_ptr<ExtensionInstalledBubble> bubble_;

  ScopedObserver<extensions::ExtensionRegistry,
                 extensions::ExtensionRegistryObserver>
      extension_registry_observer_;

  content::NotificationRegistrar registrar_;

  // The number of times to retry showing the bubble if the bubble_->browser()
  // action toolbar is animating.
  int animation_wait_retries_;

  base::WeakPtrFactory<ExtensionInstalledBubbleObserver> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionInstalledBubbleObserver);
};

// Returns the keybinding for an extension command, or a null if none exists.
scoped_ptr<extensions::Command> GetCommand(
    const std::string& extension_id,
    Profile* profile,
    ExtensionInstalledBubble::BubbleType type) {
  scoped_ptr<extensions::Command> result;
  extensions::Command command;
  extensions::CommandService* command_service =
      extensions::CommandService::Get(profile);
  bool has_command = false;
  if (type == ExtensionInstalledBubble::BROWSER_ACTION) {
    has_command = command_service->GetBrowserActionCommand(
        extension_id, extensions::CommandService::ACTIVE, &command, nullptr);
  } else if (type == ExtensionInstalledBubble::PAGE_ACTION) {
    has_command = command_service->GetPageActionCommand(
        extension_id, extensions::CommandService::ACTIVE, &command, nullptr);
  }
  if (has_command)
    result.reset(new extensions::Command(command));
  return result.Pass();
}

}  // namespace

// static
void ExtensionInstalledBubble::ShowBubble(
    const extensions::Extension* extension,
    Browser* browser,
    const SkBitmap& icon) {
  // The ExtensionInstalledBubbleObserver will delete itself when the
  // ExtensionInstalledBubble is shown or when it can't be shown anymore.
  new ExtensionInstalledBubbleObserver(
      make_scoped_ptr(new ExtensionInstalledBubble(extension, browser, icon)));
}

ExtensionInstalledBubble::ExtensionInstalledBubble(const Extension* extension,
                                                   Browser* browser,
                                                   const SkBitmap& icon)
    : extension_(extension), browser_(browser), icon_(icon) {
  if (!extensions::OmniboxInfo::GetKeyword(extension).empty())
    type_ = OMNIBOX_KEYWORD;
  else if (extensions::ActionInfo::GetBrowserActionInfo(extension))
    type_ = BROWSER_ACTION;
  else if (extensions::ActionInfo::GetPageActionInfo(extension) &&
           extensions::ActionInfo::IsVerboseInstallMessage(extension))
    type_ = PAGE_ACTION;
  else
    type_ = GENERIC;
}

ExtensionInstalledBubble::~ExtensionInstalledBubble() {}

bool ExtensionInstalledBubble::ShouldClose(BubbleCloseReason reason) const {
  // Installing an extension triggers a navigation event that should be ignored.
  return reason != BUBBLE_CLOSE_NAVIGATED;
}

std::string ExtensionInstalledBubble::GetName() const {
  return "ExtensionInstalled";
}

base::string16 ExtensionInstalledBubble::GetHowToUseDescription() const {
  int message_id = 0;
  base::string16 extra;
  if (action_command_)
    extra = action_command_->accelerator().GetShortcutText();

  switch (type_) {
    case BROWSER_ACTION:
      message_id = extra.empty() ? IDS_EXTENSION_INSTALLED_BROWSER_ACTION_INFO :
          IDS_EXTENSION_INSTALLED_BROWSER_ACTION_INFO_WITH_SHORTCUT;
      break;
    case PAGE_ACTION:
      message_id = extra.empty() ? IDS_EXTENSION_INSTALLED_PAGE_ACTION_INFO :
          IDS_EXTENSION_INSTALLED_PAGE_ACTION_INFO_WITH_SHORTCUT;
      break;
    case OMNIBOX_KEYWORD:
      extra =
          base::UTF8ToUTF16(extensions::OmniboxInfo::GetKeyword(extension_));
      message_id = IDS_EXTENSION_INSTALLED_OMNIBOX_KEYWORD_INFO;
      break;
    case GENERIC:
      break;
  }

  if (message_id == 0)
    return base::string16();
  return extra.empty() ? l10n_util::GetStringUTF16(message_id) :
      l10n_util::GetStringFUTF16(message_id, extra);
}

void ExtensionInstalledBubble::OnExtensionLoaded() {
  action_command_ = GetCommand(extension_->id(), browser_->profile(), type_);
}
