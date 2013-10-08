// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_sorting.h"

#include <map>

#include "chrome/browser/extensions/extension_prefs_unittest.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "sync/api/string_ordinal.h"
#include "testing/gtest/include/gtest/gtest.h"

using extensions::Blacklist;
using extensions::Extension;
using extensions::Manifest;

namespace keys = extension_manifest_keys;

class ExtensionSortingTest : public extensions::ExtensionPrefsTest {
 protected:
  ExtensionSorting* extension_sorting() {
    return prefs()->extension_sorting();
  }
};

class ExtensionSortingAppLocation : public ExtensionSortingTest {
 public:
  virtual void Initialize() OVERRIDE {
    extension_ = prefs_.AddExtension("not_an_app");
    // Non-apps should not have any app launch ordinal or page ordinal.
    prefs()->OnExtensionInstalled(extension_.get(),
                                  Extension::ENABLED,
                                  Blacklist::NOT_BLACKLISTED,
                                  syncer::StringOrdinal());
  }

  virtual void Verify() OVERRIDE {
    EXPECT_FALSE(
        extension_sorting()->GetAppLaunchOrdinal(extension_->id()).IsValid());
    EXPECT_FALSE(
        extension_sorting()->GetPageOrdinal(extension_->id()).IsValid());
  }

 private:
  scoped_refptr<Extension> extension_;
};
TEST_F(ExtensionSortingAppLocation, ExtensionSortingAppLocation) {}

class ExtensionSortingAppLaunchOrdinal : public ExtensionSortingTest {
 public:
  virtual void Initialize() OVERRIDE {
    // No extensions yet.
    syncer::StringOrdinal page = syncer::StringOrdinal::CreateInitialOrdinal();
    EXPECT_TRUE(syncer::StringOrdinal::CreateInitialOrdinal().Equals(
        extension_sorting()->CreateNextAppLaunchOrdinal(page)));

    extension_ = prefs_.AddApp("on_extension_installed");
    EXPECT_FALSE(prefs()->IsExtensionDisabled(extension_->id()));
    prefs()->OnExtensionInstalled(extension_.get(),
                                  Extension::ENABLED,
                                  Blacklist::NOT_BLACKLISTED,
                                  syncer::StringOrdinal());
  }

  virtual void Verify() OVERRIDE {
    syncer::StringOrdinal launch_ordinal =
        extension_sorting()->GetAppLaunchOrdinal(extension_->id());
    syncer::StringOrdinal page_ordinal =
        syncer::StringOrdinal::CreateInitialOrdinal();

    // Extension should have been assigned a valid StringOrdinal.
    EXPECT_TRUE(launch_ordinal.IsValid());
    EXPECT_TRUE(launch_ordinal.LessThan(
        extension_sorting()->CreateNextAppLaunchOrdinal(page_ordinal)));
    // Set a new launch ordinal of and verify it comes after.
    extension_sorting()->SetAppLaunchOrdinal(
        extension_->id(),
        extension_sorting()->CreateNextAppLaunchOrdinal(page_ordinal));
    syncer::StringOrdinal new_launch_ordinal =
        extension_sorting()->GetAppLaunchOrdinal(extension_->id());
    EXPECT_TRUE(launch_ordinal.LessThan(new_launch_ordinal));

    // This extension doesn't exist, so it should return an invalid
    // StringOrdinal.
    syncer::StringOrdinal invalid_app_launch_ordinal =
        extension_sorting()->GetAppLaunchOrdinal("foo");
    EXPECT_FALSE(invalid_app_launch_ordinal.IsValid());
    EXPECT_EQ(-1, extension_sorting()->PageStringOrdinalAsInteger(
        invalid_app_launch_ordinal));

    // The second page doesn't have any apps so its next launch ordinal should
    // be the first launch ordinal.
    syncer::StringOrdinal next_page = page_ordinal.CreateAfter();
    syncer::StringOrdinal next_page_app_launch_ordinal =
        extension_sorting()->CreateNextAppLaunchOrdinal(next_page);
    EXPECT_TRUE(next_page_app_launch_ordinal.Equals(
        extension_sorting()->CreateFirstAppLaunchOrdinal(next_page)));
  }

 private:
  scoped_refptr<Extension> extension_;
};
TEST_F(ExtensionSortingAppLaunchOrdinal, ExtensionSortingAppLaunchOrdinal) {}

class ExtensionSortingPageOrdinal : public ExtensionSortingTest {
 public:
  virtual void Initialize() OVERRIDE {
    extension_ = prefs_.AddApp("page_ordinal");
    // Install with a page preference.
    first_page_ = syncer::StringOrdinal::CreateInitialOrdinal();
    prefs()->OnExtensionInstalled(extension_.get(),
                                  Extension::ENABLED,
                                  Blacklist::NOT_BLACKLISTED,
                                  first_page_);
    EXPECT_TRUE(first_page_.Equals(
        extension_sorting()->GetPageOrdinal(extension_->id())));
    EXPECT_EQ(0, extension_sorting()->PageStringOrdinalAsInteger(first_page_));

    scoped_refptr<Extension> extension2 = prefs_.AddApp("page_ordinal_2");
    // Install without any page preference.
    prefs()->OnExtensionInstalled(extension2.get(),
                                  Extension::ENABLED,
                                  Blacklist::NOT_BLACKLISTED,
                                  syncer::StringOrdinal());
    EXPECT_TRUE(first_page_.Equals(
        extension_sorting()->GetPageOrdinal(extension2->id())));
  }
  virtual void Verify() OVERRIDE {
    // Set the page ordinal.
    syncer::StringOrdinal new_page = first_page_.CreateAfter();
    extension_sorting()->SetPageOrdinal(extension_->id(), new_page);
    // Verify the page ordinal.
    EXPECT_TRUE(
        new_page.Equals(extension_sorting()->GetPageOrdinal(extension_->id())));
    EXPECT_EQ(1, extension_sorting()->PageStringOrdinalAsInteger(new_page));

    // This extension doesn't exist, so it should return an invalid
    // StringOrdinal.
    EXPECT_FALSE(extension_sorting()->GetPageOrdinal("foo").IsValid());
  }

 private:
  syncer::StringOrdinal first_page_;
  scoped_refptr<Extension> extension_;
};
TEST_F(ExtensionSortingPageOrdinal, ExtensionSortingPageOrdinal) {}

// Ensure that ExtensionSorting is able to properly initialize off a set
// of old page and app launch indices and properly convert them.
class ExtensionSortingInitialize
    : public extensions::PrefsPrepopulatedTestBase {
 public:
  ExtensionSortingInitialize() {}
  virtual ~ExtensionSortingInitialize() {}

  virtual void Initialize() OVERRIDE {
    // A preference determining the order of which the apps appear on the NTP.
    const char kPrefAppLaunchIndexDeprecated[] = "app_launcher_index";
    // A preference determining the page on which an app appears in the NTP.
    const char kPrefPageIndexDeprecated[] = "page_index";

    // Setup the deprecated preferences.
    ExtensionScopedPrefs* scoped_prefs =
        static_cast<ExtensionScopedPrefs*>(prefs());
    scoped_prefs->UpdateExtensionPref(extension1()->id(),
                                      kPrefAppLaunchIndexDeprecated,
                                      Value::CreateIntegerValue(0));
    scoped_prefs->UpdateExtensionPref(extension1()->id(),
                                      kPrefPageIndexDeprecated,
                                      Value::CreateIntegerValue(0));

    scoped_prefs->UpdateExtensionPref(extension2()->id(),
                                      kPrefAppLaunchIndexDeprecated,
                                      Value::CreateIntegerValue(1));
    scoped_prefs->UpdateExtensionPref(extension2()->id(),
                                      kPrefPageIndexDeprecated,
                                      Value::CreateIntegerValue(0));

    scoped_prefs->UpdateExtensionPref(extension3()->id(),
                                      kPrefAppLaunchIndexDeprecated,
                                      Value::CreateIntegerValue(0));
    scoped_prefs->UpdateExtensionPref(extension3()->id(),
                                      kPrefPageIndexDeprecated,
                                      Value::CreateIntegerValue(1));

    // We insert the ids in reserve order so that we have to deal with the
    // element on the 2nd page before the 1st page is seen.
    extensions::ExtensionIdList ids;
    ids.push_back(extension3()->id());
    ids.push_back(extension2()->id());
    ids.push_back(extension1()->id());

    prefs()->extension_sorting()->Initialize(ids);
  }
  virtual void Verify() OVERRIDE {
    syncer::StringOrdinal first_ordinal =
        syncer::StringOrdinal::CreateInitialOrdinal();
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    EXPECT_TRUE(first_ordinal.Equals(
        extension_sorting->GetAppLaunchOrdinal(extension1()->id())));
    EXPECT_TRUE(first_ordinal.LessThan(
        extension_sorting->GetAppLaunchOrdinal(extension2()->id())));
    EXPECT_TRUE(first_ordinal.Equals(
        extension_sorting->GetAppLaunchOrdinal(extension3()->id())));

    EXPECT_TRUE(first_ordinal.Equals(
        extension_sorting->GetPageOrdinal(extension1()->id())));
    EXPECT_TRUE(first_ordinal.Equals(
        extension_sorting->GetPageOrdinal(extension2()->id())));
    EXPECT_TRUE(first_ordinal.LessThan(
        extension_sorting->GetPageOrdinal(extension3()->id())));
  }
};
TEST_F(ExtensionSortingInitialize, ExtensionSortingInitialize) {}

// Make sure that initialization still works when no extensions are present
// (i.e. make sure that the web store icon is still loaded into the map).
class ExtensionSortingInitializeWithNoApps
    : public extensions::PrefsPrepopulatedTestBase {
 public:
  ExtensionSortingInitializeWithNoApps() {}
  virtual ~ExtensionSortingInitializeWithNoApps() {}

  virtual void Initialize() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    // Make sure that the web store has valid ordinals.
    syncer::StringOrdinal initial_ordinal =
        syncer::StringOrdinal::CreateInitialOrdinal();
    extension_sorting->SetPageOrdinal(extension_misc::kWebStoreAppId,
                                      initial_ordinal);
    extension_sorting->SetAppLaunchOrdinal(extension_misc::kWebStoreAppId,
                                           initial_ordinal);

    extensions::ExtensionIdList ids;
    extension_sorting->Initialize(ids);
  }
  virtual void Verify() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    syncer::StringOrdinal page =
        extension_sorting->GetPageOrdinal(extension_misc::kWebStoreAppId);
    EXPECT_TRUE(page.IsValid());

    ExtensionSorting::PageOrdinalMap::iterator page_it =
        extension_sorting->ntp_ordinal_map_.find(page);
    EXPECT_TRUE(page_it != extension_sorting->ntp_ordinal_map_.end());

    syncer::StringOrdinal app_launch =
        extension_sorting->GetPageOrdinal(extension_misc::kWebStoreAppId);
    EXPECT_TRUE(app_launch.IsValid());

    ExtensionSorting::AppLaunchOrdinalMap::iterator app_launch_it =
        page_it->second.find(app_launch);
    EXPECT_TRUE(app_launch_it != page_it->second.end());
  }
};
TEST_F(ExtensionSortingInitializeWithNoApps,
       ExtensionSortingInitializeWithNoApps) {}

// Tests the application index to ordinal migration code for values that
// shouldn't be converted. This should be removed when the migrate code
// is taken out.
// http://crbug.com/107376
class ExtensionSortingMigrateAppIndexInvalid
    : public extensions::PrefsPrepopulatedTestBase {
 public:
  ExtensionSortingMigrateAppIndexInvalid() {}
  virtual ~ExtensionSortingMigrateAppIndexInvalid() {}

  virtual void Initialize() OVERRIDE {
    // A preference determining the order of which the apps appear on the NTP.
    const char kPrefAppLaunchIndexDeprecated[] = "app_launcher_index";
    // A preference determining the page on which an app appears in the NTP.
    const char kPrefPageIndexDeprecated[] = "page_index";

    // Setup the deprecated preference.
    ExtensionScopedPrefs* scoped_prefs =
        static_cast<ExtensionScopedPrefs*>(prefs());
    scoped_prefs->UpdateExtensionPref(extension1()->id(),
                                      kPrefAppLaunchIndexDeprecated,
                                      Value::CreateIntegerValue(0));
    scoped_prefs->UpdateExtensionPref(extension1()->id(),
                                      kPrefPageIndexDeprecated,
                                      Value::CreateIntegerValue(-1));

    extensions::ExtensionIdList ids;
    ids.push_back(extension1()->id());

    prefs()->extension_sorting()->Initialize(ids);
  }
  virtual void Verify() OVERRIDE {
    // Make sure that the invalid page_index wasn't converted over.
    EXPECT_FALSE(prefs()->extension_sorting()->GetAppLaunchOrdinal(
        extension1()->id()).IsValid());
  }
};
TEST_F(ExtensionSortingMigrateAppIndexInvalid,
       ExtensionSortingMigrateAppIndexInvalid) {}

class ExtensionSortingFixNTPCollisionsAllCollide
    : public extensions::PrefsPrepopulatedTestBase {
 public:
  ExtensionSortingFixNTPCollisionsAllCollide() {}
  virtual ~ExtensionSortingFixNTPCollisionsAllCollide() {}

  virtual void Initialize() OVERRIDE {
    repeated_ordinal_ = syncer::StringOrdinal::CreateInitialOrdinal();

    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    extension_sorting->SetAppLaunchOrdinal(extension1()->id(),
                                           repeated_ordinal_);
    extension_sorting->SetPageOrdinal(extension1()->id(), repeated_ordinal_);

    extension_sorting->SetAppLaunchOrdinal(extension2()->id(),
                                           repeated_ordinal_);
    extension_sorting->SetPageOrdinal(extension2()->id(), repeated_ordinal_);

    extension_sorting->SetAppLaunchOrdinal(extension3()->id(),
                                           repeated_ordinal_);
    extension_sorting->SetPageOrdinal(extension3()->id(), repeated_ordinal_);

    extension_sorting->FixNTPOrdinalCollisions();
  }
  virtual void Verify() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();
    syncer::StringOrdinal extension1_app_launch =
        extension_sorting->GetAppLaunchOrdinal(extension1()->id());
    syncer::StringOrdinal extension2_app_launch =
        extension_sorting->GetAppLaunchOrdinal(extension2()->id());
    syncer::StringOrdinal extension3_app_launch =
        extension_sorting->GetAppLaunchOrdinal(extension3()->id());

    // The overlapping extensions should have be adjusted so that they are
    // sorted by their id.
    EXPECT_EQ(extension1()->id() < extension2()->id(),
              extension1_app_launch.LessThan(extension2_app_launch));
    EXPECT_EQ(extension1()->id() < extension3()->id(),
              extension1_app_launch.LessThan(extension3_app_launch));
    EXPECT_EQ(extension2()->id() < extension3()->id(),
              extension2_app_launch.LessThan(extension3_app_launch));

    // The page ordinal should be unchanged.
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(extension1()->id()).Equals(
        repeated_ordinal_));
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(extension2()->id()).Equals(
        repeated_ordinal_));
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(extension3()->id()).Equals(
        repeated_ordinal_));
  }

 private:
  syncer::StringOrdinal repeated_ordinal_;
};
TEST_F(ExtensionSortingFixNTPCollisionsAllCollide,
       ExtensionSortingFixNTPCollisionsAllCollide) {}

class ExtensionSortingFixNTPCollisionsSomeCollideAtStart
    : public extensions::PrefsPrepopulatedTestBase {
 public:
  ExtensionSortingFixNTPCollisionsSomeCollideAtStart() {}
  virtual ~ExtensionSortingFixNTPCollisionsSomeCollideAtStart() {}

  virtual void Initialize() OVERRIDE {
    first_ordinal_ = syncer::StringOrdinal::CreateInitialOrdinal();
    syncer::StringOrdinal second_ordinal = first_ordinal_.CreateAfter();

    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    // Have the first two extension in the same position, with a third
    // (non-colliding) extension after.

    extension_sorting->SetAppLaunchOrdinal(extension1()->id(), first_ordinal_);
    extension_sorting->SetPageOrdinal(extension1()->id(), first_ordinal_);

    extension_sorting->SetAppLaunchOrdinal(extension2()->id(), first_ordinal_);
    extension_sorting->SetPageOrdinal(extension2()->id(), first_ordinal_);

    extension_sorting->SetAppLaunchOrdinal(extension3()->id(), second_ordinal);
    extension_sorting->SetPageOrdinal(extension3()->id(), first_ordinal_);

    extension_sorting->FixNTPOrdinalCollisions();
  }
  virtual void Verify() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();
    syncer::StringOrdinal extension1_app_launch =
        extension_sorting->GetAppLaunchOrdinal(extension1()->id());
    syncer::StringOrdinal extension2_app_launch =
        extension_sorting->GetAppLaunchOrdinal(extension2()->id());
    syncer::StringOrdinal extension3_app_launch =
        extension_sorting->GetAppLaunchOrdinal(extension3()->id());

    // The overlapping extensions should have be adjusted so that they are
    // sorted by their id, but they both should be before ext3, which wasn't
    // overlapping.
    EXPECT_EQ(extension1()->id() < extension2()->id(),
              extension1_app_launch.LessThan(extension2_app_launch));
    EXPECT_TRUE(extension1_app_launch.LessThan(extension3_app_launch));
    EXPECT_TRUE(extension2_app_launch.LessThan(extension3_app_launch));

    // The page ordinal should be unchanged.
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(extension1()->id()).Equals(
        first_ordinal_));
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(extension2()->id()).Equals(
        first_ordinal_));
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(extension3()->id()).Equals(
        first_ordinal_));
  }

 private:
  syncer::StringOrdinal first_ordinal_;
};
TEST_F(ExtensionSortingFixNTPCollisionsSomeCollideAtStart,
       ExtensionSortingFixNTPCollisionsSomeCollideAtStart) {}

class ExtensionSortingFixNTPCollisionsSomeCollideAtEnd
    : public extensions::PrefsPrepopulatedTestBase {
 public:
  ExtensionSortingFixNTPCollisionsSomeCollideAtEnd() {}
  virtual ~ExtensionSortingFixNTPCollisionsSomeCollideAtEnd() {}

  virtual void Initialize() OVERRIDE {
    first_ordinal_ = syncer::StringOrdinal::CreateInitialOrdinal();
    syncer::StringOrdinal second_ordinal = first_ordinal_.CreateAfter();

    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    // Have the first extension in a non-colliding position, followed by two
    // two extension in the same position.

    extension_sorting->SetAppLaunchOrdinal(extension1()->id(), first_ordinal_);
    extension_sorting->SetPageOrdinal(extension1()->id(), first_ordinal_);

    extension_sorting->SetAppLaunchOrdinal(extension2()->id(), second_ordinal);
    extension_sorting->SetPageOrdinal(extension2()->id(), first_ordinal_);

    extension_sorting->SetAppLaunchOrdinal(extension3()->id(), second_ordinal);
    extension_sorting->SetPageOrdinal(extension3()->id(), first_ordinal_);

    extension_sorting->FixNTPOrdinalCollisions();
  }
  virtual void Verify() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();
    syncer::StringOrdinal extension1_app_launch =
        extension_sorting->GetAppLaunchOrdinal(extension1()->id());
    syncer::StringOrdinal extension2_app_launch =
        extension_sorting->GetAppLaunchOrdinal(extension2()->id());
    syncer::StringOrdinal extension3_app_launch =
        extension_sorting->GetAppLaunchOrdinal(extension3()->id());

    // The overlapping extensions should have be adjusted so that they are
    // sorted by their id, but they both should be after ext1, which wasn't
    // overlapping.
    EXPECT_TRUE(extension1_app_launch.LessThan(extension2_app_launch));
    EXPECT_TRUE(extension1_app_launch.LessThan(extension3_app_launch));
    EXPECT_EQ(extension2()->id() < extension3()->id(),
              extension2_app_launch.LessThan(extension3_app_launch));

    // The page ordinal should be unchanged.
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(extension1()->id()).Equals(
        first_ordinal_));
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(extension2()->id()).Equals(
        first_ordinal_));
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(extension3()->id()).Equals(
        first_ordinal_));
  }

 private:
  syncer::StringOrdinal first_ordinal_;
};
TEST_F(ExtensionSortingFixNTPCollisionsSomeCollideAtEnd,
       ExtensionSortingFixNTPCollisionsSomeCollideAtEnd) {}

class ExtensionSortingFixNTPCollisionsTwoCollisions
    : public extensions::PrefsPrepopulatedTestBase {
 public:
  ExtensionSortingFixNTPCollisionsTwoCollisions() {}
  virtual ~ExtensionSortingFixNTPCollisionsTwoCollisions() {}

  virtual void Initialize() OVERRIDE {
    first_ordinal_ = syncer::StringOrdinal::CreateInitialOrdinal();
    syncer::StringOrdinal second_ordinal = first_ordinal_.CreateAfter();

    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    // Have two extensions colliding, followed by two more colliding extensions.
    extension_sorting->SetAppLaunchOrdinal(extension1()->id(), first_ordinal_);
    extension_sorting->SetPageOrdinal(extension1()->id(), first_ordinal_);

    extension_sorting->SetAppLaunchOrdinal(extension2()->id(), first_ordinal_);
    extension_sorting->SetPageOrdinal(extension2()->id(), first_ordinal_);

    extension_sorting->SetAppLaunchOrdinal(extension3()->id(), second_ordinal);
    extension_sorting->SetPageOrdinal(extension3()->id(), first_ordinal_);

    extension_sorting->SetAppLaunchOrdinal(extension4()->id(), second_ordinal);
    extension_sorting->SetPageOrdinal(extension4()->id(), first_ordinal_);

    extension_sorting->FixNTPOrdinalCollisions();
  }
  virtual void Verify() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();
    syncer::StringOrdinal extension1_app_launch =
        extension_sorting->GetAppLaunchOrdinal(extension1()->id());
    syncer::StringOrdinal extension2_app_launch =
        extension_sorting->GetAppLaunchOrdinal(extension2()->id());
    syncer::StringOrdinal extension3_app_launch =
        extension_sorting->GetAppLaunchOrdinal(extension3()->id());
    syncer::StringOrdinal extension4_app_launch =
        extension_sorting->GetAppLaunchOrdinal(extension4()->id());

    // The overlapping extensions should have be adjusted so that they are
    // sorted by their id, with |ext1| and |ext2| appearing before |ext3| and
    // |ext4|.
    EXPECT_TRUE(extension1_app_launch.LessThan(extension3_app_launch));
    EXPECT_TRUE(extension1_app_launch.LessThan(extension4_app_launch));
    EXPECT_TRUE(extension2_app_launch.LessThan(extension3_app_launch));
    EXPECT_TRUE(extension2_app_launch.LessThan(extension4_app_launch));

    EXPECT_EQ(extension1()->id() < extension2()->id(),
              extension1_app_launch.LessThan(extension2_app_launch));
    EXPECT_EQ(extension3()->id() < extension4()->id(),
              extension3_app_launch.LessThan(extension4_app_launch));

    // The page ordinal should be unchanged.
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(extension1()->id()).Equals(
        first_ordinal_));
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(extension2()->id()).Equals(
        first_ordinal_));
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(extension3()->id()).Equals(
        first_ordinal_));
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(extension4()->id()).Equals(
        first_ordinal_));
  }

 private:
  syncer::StringOrdinal first_ordinal_;
};
TEST_F(ExtensionSortingFixNTPCollisionsTwoCollisions,
       ExtensionSortingFixNTPCollisionsTwoCollisions) {}

class ExtensionSortingEnsureValidOrdinals
    : public extensions::PrefsPrepopulatedTestBase {
 public :
  ExtensionSortingEnsureValidOrdinals() {}
  virtual ~ExtensionSortingEnsureValidOrdinals() {}

  virtual void Initialize() OVERRIDE {}
  virtual void Verify() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    // Give ext1 invalid ordinals and then check that EnsureValidOrdinals fixes
    // them.
    extension_sorting->SetAppLaunchOrdinal(extension1()->id(),
                                           syncer::StringOrdinal());
    extension_sorting->SetPageOrdinal(extension1()->id(),
                                      syncer::StringOrdinal());

    extension_sorting->EnsureValidOrdinals(extension1()->id(),
                                           syncer::StringOrdinal());

    EXPECT_TRUE(
        extension_sorting->GetAppLaunchOrdinal(extension1()->id()).IsValid());
    EXPECT_TRUE(
        extension_sorting->GetPageOrdinal(extension1()->id()).IsValid());
  }
};
TEST_F(ExtensionSortingEnsureValidOrdinals,
       ExtensionSortingEnsureValidOrdinals) {}

class ExtensionSortingPageOrdinalMapping
    : public extensions::PrefsPrepopulatedTestBase {
 public:
  ExtensionSortingPageOrdinalMapping() {}
  virtual ~ExtensionSortingPageOrdinalMapping() {}

  virtual void Initialize() OVERRIDE {}
  virtual void Verify() OVERRIDE {
    std::string ext_1 = "ext_1";
    std::string ext_2 = "ext_2";

    ExtensionSorting* extension_sorting = prefs()->extension_sorting();
    syncer::StringOrdinal first_ordinal =
        syncer::StringOrdinal::CreateInitialOrdinal();

    // Ensure attempting to removing a mapping with an invalid page doesn't
    // modify the map.
    EXPECT_TRUE(extension_sorting->ntp_ordinal_map_.empty());
    extension_sorting->RemoveOrdinalMapping(
        ext_1, first_ordinal, first_ordinal);
    EXPECT_TRUE(extension_sorting->ntp_ordinal_map_.empty());

    // Add new mappings.
    extension_sorting->AddOrdinalMapping(ext_1, first_ordinal, first_ordinal);
    extension_sorting->AddOrdinalMapping(ext_2, first_ordinal, first_ordinal);

    EXPECT_EQ(1U, extension_sorting->ntp_ordinal_map_.size());
    EXPECT_EQ(2U, extension_sorting->ntp_ordinal_map_[first_ordinal].size());

    ExtensionSorting::AppLaunchOrdinalMap::iterator it =
        extension_sorting->ntp_ordinal_map_[first_ordinal].find(first_ordinal);
    EXPECT_EQ(ext_1, it->second);
    ++it;
    EXPECT_EQ(ext_2, it->second);

    extension_sorting->RemoveOrdinalMapping(ext_1,
                                            first_ordinal,
                                            first_ordinal);
    EXPECT_EQ(1U, extension_sorting->ntp_ordinal_map_.size());
    EXPECT_EQ(1U, extension_sorting->ntp_ordinal_map_[first_ordinal].size());

    it = extension_sorting->ntp_ordinal_map_[first_ordinal].find(
        first_ordinal);
    EXPECT_EQ(ext_2, it->second);

    // Ensure that attempting to remove an extension with a valid page and app
    // launch ordinals, but a unused id has no effect.
    extension_sorting->RemoveOrdinalMapping(
        "invalid_ext", first_ordinal, first_ordinal);
    EXPECT_EQ(1U, extension_sorting->ntp_ordinal_map_.size());
    EXPECT_EQ(1U, extension_sorting->ntp_ordinal_map_[first_ordinal].size());

    it = extension_sorting->ntp_ordinal_map_[first_ordinal].find(
        first_ordinal);
    EXPECT_EQ(ext_2, it->second);
  }
};
TEST_F(ExtensionSortingPageOrdinalMapping,
       ExtensionSortingPageOrdinalMapping) {}

class ExtensionSortingPreinstalledAppsBase
    : public extensions::PrefsPrepopulatedTestBase {
 public:
  ExtensionSortingPreinstalledAppsBase() {
    DictionaryValue simple_dict;
    simple_dict.SetString(keys::kVersion, "1.0.0.0");
    simple_dict.SetString(keys::kName, "unused");
    simple_dict.SetString(keys::kApp, "true");
    simple_dict.SetString(keys::kLaunchLocalPath, "fake.html");

    std::string error;
    app1_scoped_ = Extension::Create(
        prefs_.temp_dir().AppendASCII("app1_"), Manifest::EXTERNAL_PREF,
        simple_dict, Extension::NO_FLAGS, &error);
    prefs()->OnExtensionInstalled(app1_scoped_.get(),
                                  Extension::ENABLED,
                                  Blacklist::NOT_BLACKLISTED,
                                  syncer::StringOrdinal());

    app2_scoped_ = Extension::Create(
        prefs_.temp_dir().AppendASCII("app2_"), Manifest::EXTERNAL_PREF,
        simple_dict, Extension::NO_FLAGS, &error);
    prefs()->OnExtensionInstalled(app2_scoped_.get(),
                                  Extension::ENABLED,
                                  Blacklist::NOT_BLACKLISTED,
                                  syncer::StringOrdinal());

    app1_ = app1_scoped_.get();
    app2_ = app2_scoped_.get();
  }
  virtual ~ExtensionSortingPreinstalledAppsBase() {}

 protected:
  // Weak references, for convenience.
  Extension* app1_;
  Extension* app2_;

 private:
  scoped_refptr<Extension> app1_scoped_;
  scoped_refptr<Extension> app2_scoped_;
};

class ExtensionSortingGetMinOrMaxAppLaunchOrdinalsOnPage
    : public ExtensionSortingPreinstalledAppsBase {
 public:
  ExtensionSortingGetMinOrMaxAppLaunchOrdinalsOnPage() {}
  virtual ~ExtensionSortingGetMinOrMaxAppLaunchOrdinalsOnPage() {}

  virtual void Initialize() OVERRIDE {}
  virtual void Verify() OVERRIDE {
    syncer::StringOrdinal page = syncer::StringOrdinal::CreateInitialOrdinal();
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    syncer::StringOrdinal min =
        extension_sorting->GetMinOrMaxAppLaunchOrdinalsOnPage(
            page,
            ExtensionSorting::MIN_ORDINAL);
    syncer::StringOrdinal max =
        extension_sorting->GetMinOrMaxAppLaunchOrdinalsOnPage(
            page,
            ExtensionSorting::MAX_ORDINAL);
    EXPECT_TRUE(min.IsValid());
    EXPECT_TRUE(max.IsValid());
    EXPECT_TRUE(min.LessThan(max));

    // Ensure that the min and max values aren't set for empty pages.
    min = syncer::StringOrdinal();
    max = syncer::StringOrdinal();
    syncer::StringOrdinal empty_page = page.CreateAfter();
    EXPECT_FALSE(min.IsValid());
    EXPECT_FALSE(max.IsValid());
    min = extension_sorting->GetMinOrMaxAppLaunchOrdinalsOnPage(
        empty_page,
        ExtensionSorting::MIN_ORDINAL);
    max = extension_sorting->GetMinOrMaxAppLaunchOrdinalsOnPage(
        empty_page,
        ExtensionSorting::MAX_ORDINAL);
    EXPECT_FALSE(min.IsValid());
    EXPECT_FALSE(max.IsValid());
  }
};
TEST_F(ExtensionSortingGetMinOrMaxAppLaunchOrdinalsOnPage,
       ExtensionSortingGetMinOrMaxAppLaunchOrdinalsOnPage) {}

// Make sure that empty pages aren't removed from the integer to ordinal
// mapping. See http://crbug.com/109802 for details.
class ExtensionSortingKeepEmptyStringOrdinalPages
    : public ExtensionSortingPreinstalledAppsBase {
 public:
  ExtensionSortingKeepEmptyStringOrdinalPages() {}
  virtual ~ExtensionSortingKeepEmptyStringOrdinalPages() {}

  virtual void Initialize() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    syncer::StringOrdinal first_page =
        syncer::StringOrdinal::CreateInitialOrdinal();
    extension_sorting->SetPageOrdinal(app1_->id(), first_page);
    EXPECT_EQ(0, extension_sorting->PageStringOrdinalAsInteger(first_page));

    last_page_ = first_page.CreateAfter();
    extension_sorting->SetPageOrdinal(app2_->id(), last_page_);
    EXPECT_EQ(1, extension_sorting->PageStringOrdinalAsInteger(last_page_));

    // Move the second app to create an empty page.
    extension_sorting->SetPageOrdinal(app2_->id(), first_page);
    EXPECT_EQ(0, extension_sorting->PageStringOrdinalAsInteger(first_page));
  }
  virtual void Verify() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    // Move the second app to a new empty page at the end, skipping over
    // the current empty page.
    last_page_ = last_page_.CreateAfter();
    extension_sorting->SetPageOrdinal(app2_->id(), last_page_);
    EXPECT_EQ(2, extension_sorting->PageStringOrdinalAsInteger(last_page_));
    EXPECT_TRUE(
        last_page_.Equals(extension_sorting->PageIntegerAsStringOrdinal(2)));
  }

 private:
  syncer::StringOrdinal last_page_;
};
TEST_F(ExtensionSortingKeepEmptyStringOrdinalPages,
       ExtensionSortingKeepEmptyStringOrdinalPages) {}

class ExtensionSortingMakesFillerOrdinals
    : public ExtensionSortingPreinstalledAppsBase {
 public:
  ExtensionSortingMakesFillerOrdinals() {}
  virtual ~ExtensionSortingMakesFillerOrdinals() {}

  virtual void Initialize() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    syncer::StringOrdinal first_page =
        syncer::StringOrdinal::CreateInitialOrdinal();
    extension_sorting->SetPageOrdinal(app1_->id(), first_page);
    EXPECT_EQ(0, extension_sorting->PageStringOrdinalAsInteger(first_page));
  }
  virtual void Verify() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    // Because the UI can add an unlimited number of empty pages without an app
    // on them, this test simulates dropping of an app on the 1st and 4th empty
    // pages (3rd and 6th pages by index) to ensure we don't crash and that
    // filler ordinals are created as needed. See: http://crbug.com/122214
    syncer::StringOrdinal page_three =
        extension_sorting->PageIntegerAsStringOrdinal(2);
    extension_sorting->SetPageOrdinal(app1_->id(), page_three);
    EXPECT_EQ(2, extension_sorting->PageStringOrdinalAsInteger(page_three));

    syncer::StringOrdinal page_six =
        extension_sorting->PageIntegerAsStringOrdinal(5);
    extension_sorting->SetPageOrdinal(app1_->id(), page_six);
    EXPECT_EQ(5, extension_sorting->PageStringOrdinalAsInteger(page_six));
  }
};
TEST_F(ExtensionSortingMakesFillerOrdinals,
       ExtensionSortingMakesFillerOrdinals) {}

class ExtensionSortingDefaultOrdinalsBase : public ExtensionSortingTest {
 public:
  ExtensionSortingDefaultOrdinalsBase() {}
  virtual ~ExtensionSortingDefaultOrdinalsBase() {}

  virtual void Initialize() OVERRIDE {
    app_ = CreateApp("app");

    InitDefaultOrdinals();
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();
    ExtensionSorting::AppOrdinalsMap& sorting_defaults =
        extension_sorting->default_ordinals_;
    sorting_defaults[app_->id()].page_ordinal = default_page_ordinal_;
    sorting_defaults[app_->id()].app_launch_ordinal =
        default_app_launch_ordinal_;

    SetupUserOrdinals();
    InstallApps();
  }

 protected:
  scoped_refptr<Extension> CreateApp(const std::string& name) {
    DictionaryValue simple_dict;
    simple_dict.SetString(keys::kVersion, "1.0.0.0");
    simple_dict.SetString(keys::kName, name);
    simple_dict.SetString(keys::kApp, "true");
    simple_dict.SetString(keys::kLaunchLocalPath, "fake.html");

    std::string errors;
    scoped_refptr<Extension> app = Extension::Create(
        prefs_.temp_dir().AppendASCII(name), Manifest::EXTERNAL_PREF,
        simple_dict, Extension::NO_FLAGS, &errors);
    EXPECT_TRUE(app.get()) << errors;
    EXPECT_TRUE(Extension::IdIsValid(app->id()));
    return app;
  }

  void InitDefaultOrdinals() {
    default_page_ordinal_ =
        syncer::StringOrdinal::CreateInitialOrdinal().CreateAfter();
    default_app_launch_ordinal_ =
        syncer::StringOrdinal::CreateInitialOrdinal().CreateBefore();
  }

  virtual void SetupUserOrdinals() {}

  virtual void InstallApps() {
    prefs()->OnExtensionInstalled(app_.get(),
                                  Extension::ENABLED,
                                  Blacklist::NOT_BLACKLISTED,
                                  syncer::StringOrdinal());
  }

  scoped_refptr<Extension> app_;
  syncer::StringOrdinal default_page_ordinal_;
  syncer::StringOrdinal default_app_launch_ordinal_;
};

// Tests that the app gets its default ordinals.
class ExtensionSortingDefaultOrdinals
    : public ExtensionSortingDefaultOrdinalsBase {
 public:
  ExtensionSortingDefaultOrdinals() {}
  virtual ~ExtensionSortingDefaultOrdinals() {}

  virtual void Verify() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(app_->id()).Equals(
        default_page_ordinal_));
    EXPECT_TRUE(extension_sorting->GetAppLaunchOrdinal(app_->id()).Equals(
        default_app_launch_ordinal_));
  }
};
TEST_F(ExtensionSortingDefaultOrdinals,
       ExtensionSortingDefaultOrdinals) {}

// Tests that the default page ordinal is overridden by install page ordinal.
class ExtensionSortingDefaultOrdinalOverriddenByInstallPage
    : public ExtensionSortingDefaultOrdinalsBase {
 public:
  ExtensionSortingDefaultOrdinalOverriddenByInstallPage() {}
  virtual ~ExtensionSortingDefaultOrdinalOverriddenByInstallPage() {}

  virtual void Verify() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    EXPECT_FALSE(extension_sorting->GetPageOrdinal(app_->id()).Equals(
        default_page_ordinal_));
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(app_->id()).Equals(
        install_page_));
  }

 protected:
  virtual void InstallApps() OVERRIDE {
    install_page_ = default_page_ordinal_.CreateAfter();
    prefs()->OnExtensionInstalled(app_.get(),
                                  Extension::ENABLED,
                                  Blacklist::NOT_BLACKLISTED,
                                  install_page_);
  }

 private:
  syncer::StringOrdinal install_page_;
};
TEST_F(ExtensionSortingDefaultOrdinalOverriddenByInstallPage,
       ExtensionSortingDefaultOrdinalOverriddenByInstallPage) {}

// Tests that the default ordinals are overridden by user values.
class ExtensionSortingDefaultOrdinalOverriddenByUserValue
    : public ExtensionSortingDefaultOrdinalsBase {
 public:
  ExtensionSortingDefaultOrdinalOverriddenByUserValue() {}
  virtual ~ExtensionSortingDefaultOrdinalOverriddenByUserValue() {}

  virtual void Verify() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    EXPECT_TRUE(extension_sorting->GetPageOrdinal(app_->id()).Equals(
        user_page_ordinal_));
    EXPECT_TRUE(extension_sorting->GetAppLaunchOrdinal(app_->id()).Equals(
        user_app_launch_ordinal_));
  }

 protected:
  virtual void SetupUserOrdinals() OVERRIDE {
    user_page_ordinal_ = default_page_ordinal_.CreateAfter();
    user_app_launch_ordinal_ = default_app_launch_ordinal_.CreateBefore();

    ExtensionSorting* extension_sorting = prefs()->extension_sorting();
    extension_sorting->SetPageOrdinal(app_->id(), user_page_ordinal_);
    extension_sorting->SetAppLaunchOrdinal(app_->id(),
                                           user_app_launch_ordinal_);
  }

 private:
  syncer::StringOrdinal user_page_ordinal_;
  syncer::StringOrdinal user_app_launch_ordinal_;
};
TEST_F(ExtensionSortingDefaultOrdinalOverriddenByUserValue,
       ExtensionSortingDefaultOrdinalOverriddenByUserValue) {}

// Tests that the default app launch ordinal is changed to avoid collision.
class ExtensionSortingDefaultOrdinalNoCollision
    : public ExtensionSortingDefaultOrdinalsBase {
 public:
  ExtensionSortingDefaultOrdinalNoCollision() {}
  virtual ~ExtensionSortingDefaultOrdinalNoCollision() {}

  virtual void Verify() OVERRIDE {
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();

    // Use the default page.
    EXPECT_TRUE(extension_sorting->GetPageOrdinal(app_->id()).Equals(
        default_page_ordinal_));
    // Not using the default app launch ordinal because of the collision.
    EXPECT_FALSE(extension_sorting->GetAppLaunchOrdinal(app_->id()).Equals(
        default_app_launch_ordinal_));
  }

 protected:
  virtual void SetupUserOrdinals() OVERRIDE {
    other_app_ = prefs_.AddApp("other_app");
    // Creates a collision.
    ExtensionSorting* extension_sorting = prefs()->extension_sorting();
    extension_sorting->SetPageOrdinal(other_app_->id(), default_page_ordinal_);
    extension_sorting->SetAppLaunchOrdinal(other_app_->id(),
                                           default_app_launch_ordinal_);

    yet_another_app_ = prefs_.AddApp("yet_aother_app");
    extension_sorting->SetPageOrdinal(yet_another_app_->id(),
                                      default_page_ordinal_);
    extension_sorting->SetAppLaunchOrdinal(yet_another_app_->id(),
                                           default_app_launch_ordinal_);
  }

 private:
  scoped_refptr<Extension> other_app_;
  scoped_refptr<Extension> yet_another_app_;
};
TEST_F(ExtensionSortingDefaultOrdinalNoCollision,
       ExtensionSortingDefaultOrdinalNoCollision) {}
