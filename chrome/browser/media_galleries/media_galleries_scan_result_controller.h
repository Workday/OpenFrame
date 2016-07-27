// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_GALLERIES_MEDIA_GALLERIES_SCAN_RESULT_CONTROLLER_H_
#define CHROME_BROWSER_MEDIA_GALLERIES_MEDIA_GALLERIES_SCAN_RESULT_CONTROLLER_H_

#include <map>
#include <set>
#include <string>

#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string16.h"
#include "chrome/browser/media_galleries/media_galleries_dialog_controller.h"
#include "chrome/browser/media_galleries/media_galleries_preferences.h"
#include "components/storage_monitor/removable_storage_observer.h"

namespace content {
class WebContents;
}

namespace extensions {
class Extension;
}

namespace ui {
class MenuModel;
}

class MediaGalleriesScanResultController;
class MediaGalleryContextMenu;
class Profile;

// The controller is responsible for handling the logic of the dialog and
// interfacing with the model (i.e., MediaGalleriesPreferences). It shows
// the dialog and owns itself.
class MediaGalleriesScanResultController
    : public MediaGalleriesDialogController,
      public storage_monitor::RemovableStorageObserver,
      public MediaGalleriesPreferences::GalleryChangeObserver {
 public:
  // |preferences| must be already initialized.
  static size_t ScanResultCountForExtension(
      MediaGalleriesPreferences* preferences,
      const extensions::Extension* extension);

  // The constructor creates a dialog controller which owns itself.
  MediaGalleriesScanResultController(
      content::WebContents* web_contents,
      const extensions::Extension& extension,
      const base::Closure& on_finish);

  // MediaGalleriesDialogController implementation.
  base::string16 GetHeader() const override;
  base::string16 GetSubtext() const override;
  bool IsAcceptAllowed() const override;
  bool ShouldShowFolderViewer(const Entry& entry) const override;
  std::vector<base::string16> GetSectionHeaders() const override;
  Entries GetSectionEntries(size_t index) const override;
  base::string16 GetAuxiliaryButtonText() const override;
  void DidClickAuxiliaryButton() override;
  void DidToggleEntry(MediaGalleryPrefId id, bool selected) override;
  void DidClickOpenFolderViewer(MediaGalleryPrefId id) override;
  void DidForgetEntry(MediaGalleryPrefId id) override;
  base::string16 GetAcceptButtonText() const override;
  void DialogFinished(bool accepted) override;
  ui::MenuModel* GetContextMenu(MediaGalleryPrefId id) override;
  content::WebContents* WebContents() override;

 protected:
  typedef base::Callback<MediaGalleriesDialog* (
      MediaGalleriesDialogController*)> CreateDialogCallback;
  typedef std::map<MediaGalleryPrefId, Entry> ScanResults;

  // Updates |scan_results| from |preferences|. Will not add galleries from
  // |ignore_list| onto |scan_results|.
  static void UpdateScanResultsFromPreferences(
      MediaGalleriesPreferences* preferences,
      const extensions::Extension* extension,
      MediaGalleryPrefIdSet ignore_list,
      ScanResults* scan_results);

  // Used for unit tests.
  MediaGalleriesScanResultController(
      const extensions::Extension& extension,
      MediaGalleriesPreferences* preferences_,
      const CreateDialogCallback& create_dialog_callback,
      const base::Closure& on_finish);

  ~MediaGalleriesScanResultController() override;

 private:
  friend class MediaGalleriesScanResultControllerTest;
  friend class MediaGalleriesScanResultCocoaTest;
  friend class TestMediaGalleriesAddScanResultsFunction;

  // Bottom half of constructor -- called when |preferences_| is initialized.
  void OnPreferencesInitialized();

  // Used to keep the dialog in sync with the preferences.
  void OnPreferenceUpdate(const std::string& extension_id);

  // Used to keep the dialog in sync with attached and detached devices.
  void OnRemovableDeviceUpdate(const std::string& device_id);

  Profile* GetProfile() const;

  // RemovableStorageObserver implementation.
  // Used to keep dialog in sync with removable device status.
  void OnRemovableStorageAttached(
      const storage_monitor::StorageInfo& info) override;
  void OnRemovableStorageDetached(
      const storage_monitor::StorageInfo& info) override;

  // MediaGalleriesPreferences::GalleryChangeObserver implementations.
  // Used to keep the dialog in sync when the preferences change.
  void OnPermissionAdded(MediaGalleriesPreferences* pref,
                         const std::string& extension_id,
                         MediaGalleryPrefId pref_id) override;
  void OnPermissionRemoved(MediaGalleriesPreferences* pref,
                           const std::string& extension_id,
                           MediaGalleryPrefId pref_id) override;
  void OnGalleryAdded(MediaGalleriesPreferences* pref,
                      MediaGalleryPrefId pref_id) override;
  void OnGalleryRemoved(MediaGalleriesPreferences* pref,
                        MediaGalleryPrefId pref_id) override;
  void OnGalleryInfoUpdated(MediaGalleriesPreferences* pref,
                            MediaGalleryPrefId pref_id) override;

  // The web contents from which the request originated.
  content::WebContents* web_contents_;

  // This is just a reference, but it's assumed that it won't become invalid
  // while the dialog is showing.
  const extensions::Extension* extension_;

  // The scan results that aren't blacklisted and this extension doesn't
  // already have access to.
  ScanResults scan_results_;

  // The set of scan results which should be removed (blacklisted) - unless
  // the user clicks Cancel.
  MediaGalleryPrefIdSet results_to_remove_;

  // Callback to run when the dialog closes.
  base::Closure on_finish_;

  // The model that tracks galleries and extensions' permissions.
  // This is the authoritative source for gallery information.
  MediaGalleriesPreferences* preferences_;

  // Creates the dialog. Only changed for unit tests.
  CreateDialogCallback create_dialog_callback_;

  // The view that's showing.
  scoped_ptr<MediaGalleriesDialog> dialog_;

  scoped_ptr<MediaGalleryContextMenu> context_menu_;

  DISALLOW_COPY_AND_ASSIGN(MediaGalleriesScanResultController);
};

#endif  // CHROME_BROWSER_MEDIA_GALLERIES_MEDIA_GALLERIES_SCAN_RESULT_CONTROLLER_H_
