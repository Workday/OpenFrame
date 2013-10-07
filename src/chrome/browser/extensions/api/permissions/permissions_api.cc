// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/permissions/permissions_api.h"

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/permissions/permissions_api_helpers.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/permissions_updater.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/api/permissions.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/permissions/permissions_data.h"
#include "chrome/common/extensions/permissions/permissions_info.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/url_pattern_set.h"
#include "url/gurl.h"

namespace extensions {

using api::permissions::Permissions;

namespace Contains = api::permissions::Contains;
namespace GetAll = api::permissions::GetAll;
namespace Remove = api::permissions::Remove;
namespace Request  = api::permissions::Request;
namespace helpers = permissions_api_helpers;

namespace {

const char kCantRemoveRequiredPermissionsError[] =
    "You cannot remove required permissions.";
const char kNotInOptionalPermissionsError[] =
    "Optional permissions must be listed in extension manifest.";
const char kNotWhitelistedError[] =
    "The optional permissions API does not support '*'.";
const char kUserGestureRequiredError[] =
    "This function must be called during a user gesture";

enum AutoConfirmForTest {
  DO_NOT_SKIP = 0,
  PROCEED,
  ABORT
};
AutoConfirmForTest auto_confirm_for_tests = DO_NOT_SKIP;
bool ignore_user_gesture_for_tests = false;

}  // namespace

bool PermissionsContainsFunction::RunImpl() {
  scoped_ptr<Contains::Params> params(Contains::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  scoped_refptr<PermissionSet> permissions =
      helpers::UnpackPermissionSet(
          params->permissions,
          ExtensionPrefs::Get(profile_)->AllowFileAccess(extension_->id()),
          &error_);
  if (!permissions.get())
    return false;

  results_ = Contains::Results::Create(
      GetExtension()->GetActivePermissions()->Contains(*permissions.get()));
  return true;
}

bool PermissionsGetAllFunction::RunImpl() {
  scoped_ptr<Permissions> permissions =
      helpers::PackPermissionSet(GetExtension()->GetActivePermissions().get());
  results_ = GetAll::Results::Create(*permissions);
  return true;
}

bool PermissionsRemoveFunction::RunImpl() {
  scoped_ptr<Remove::Params> params(Remove::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  scoped_refptr<PermissionSet> permissions =
      helpers::UnpackPermissionSet(
          params->permissions,
          ExtensionPrefs::Get(profile_)->AllowFileAccess(extension_->id()),
          &error_);
  if (!permissions.get())
    return false;

  const Extension* extension = GetExtension();

  // Make sure they're only trying to remove permissions supported by this API.
  APIPermissionSet apis = permissions->apis();
  for (APIPermissionSet::const_iterator i = apis.begin();
       i != apis.end(); ++i) {
    if (!i->info()->supports_optional()) {
      error_ = ErrorUtils::FormatErrorMessage(
          kNotWhitelistedError, i->name());
      return false;
    }
  }

  // Make sure we don't remove any required pemissions.
  const PermissionSet* required =
      PermissionsData::GetRequiredPermissions(extension);
  scoped_refptr<PermissionSet> intersection(
      PermissionSet::CreateIntersection(permissions.get(), required));
  if (!intersection->IsEmpty()) {
    error_ = kCantRemoveRequiredPermissionsError;
    return false;
  }

  PermissionsUpdater(profile()).RemovePermissions(extension, permissions.get());
  results_ = Remove::Results::Create(true);
  return true;
}

// static
void PermissionsRequestFunction::SetAutoConfirmForTests(bool should_proceed) {
  auto_confirm_for_tests = should_proceed ? PROCEED : ABORT;
}

// static
void PermissionsRequestFunction::SetIgnoreUserGestureForTests(
    bool ignore) {
  ignore_user_gesture_for_tests = ignore;
}

PermissionsRequestFunction::PermissionsRequestFunction() {}

void PermissionsRequestFunction::InstallUIProceed() {
  PermissionsUpdater perms_updater(profile());
  perms_updater.AddPermissions(GetExtension(), requested_permissions_.get());

  results_ = Request::Results::Create(true);
  SendResponse(true);

  Release();  // Balanced in RunImpl().
}

void PermissionsRequestFunction::InstallUIAbort(bool user_initiated) {
  SendResponse(true);

  Release();  // Balanced in RunImpl().
}

PermissionsRequestFunction::~PermissionsRequestFunction() {}

bool PermissionsRequestFunction::RunImpl() {
  results_ = Request::Results::Create(false);

  if (!user_gesture() &&
      !ignore_user_gesture_for_tests &&
      extension_->location() != Manifest::COMPONENT) {
    error_ = kUserGestureRequiredError;
    return false;
  }

  scoped_ptr<Request::Params> params(Request::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  requested_permissions_ =
      helpers::UnpackPermissionSet(
          params->permissions,
          ExtensionPrefs::Get(profile_)->AllowFileAccess(extension_->id()),
          &error_);
  if (!requested_permissions_.get())
    return false;

  // Make sure they're only requesting permissions supported by this API.
  APIPermissionSet apis = requested_permissions_->apis();
  for (APIPermissionSet::const_iterator i = apis.begin();
       i != apis.end(); ++i) {
    if (!i->info()->supports_optional()) {
      error_ = ErrorUtils::FormatErrorMessage(
          kNotWhitelistedError, i->name());
      return false;
    }
  }

  // Filter out permissions that do not need to be listed in the optional
  // section of the manifest.
  scoped_refptr<PermissionSet> manifest_required_requested_permissions =
      PermissionSet::ExcludeNotInManifestPermissions(
          requested_permissions_.get());

  // The requested permissions must be defined as optional in the manifest.
  if (!PermissionsData::GetOptionalPermissions(GetExtension())
          ->Contains(*manifest_required_requested_permissions.get())) {
    error_ = kNotInOptionalPermissionsError;
    return false;
  }

  // We don't need to prompt the user if the requested permissions are a subset
  // of the granted permissions set.
  scoped_refptr<const PermissionSet> granted =
      ExtensionPrefs::Get(profile_)->
          GetGrantedPermissions(GetExtension()->id());
  if (granted.get() && granted->Contains(*requested_permissions_.get())) {
    PermissionsUpdater perms_updater(profile());
    perms_updater.AddPermissions(GetExtension(), requested_permissions_.get());
    results_ = Request::Results::Create(true);
    SendResponse(true);
    return true;
  }

  // Filter out the granted permissions so we only prompt for new ones.
  requested_permissions_ = PermissionSet::CreateDifference(
      requested_permissions_.get(), granted.get());

  AddRef();  // Balanced in InstallUIProceed() / InstallUIAbort().

  // We don't need to show the prompt if there are no new warnings, or if
  // we're skipping the confirmation UI. All extension types but INTERNAL
  // are allowed to silently increase their permission level.
  bool has_no_warnings = requested_permissions_->GetWarningMessages(
      GetExtension()->GetType()).empty();
  if (auto_confirm_for_tests == PROCEED || has_no_warnings ||
      extension_->location() == Manifest::COMPONENT) {
    InstallUIProceed();
  } else if (auto_confirm_for_tests == ABORT) {
    // Pretend the user clicked cancel.
    InstallUIAbort(true);
  } else {
    CHECK_EQ(DO_NOT_SKIP, auto_confirm_for_tests);
    install_ui_.reset(new ExtensionInstallPrompt(GetAssociatedWebContents()));
    install_ui_->ConfirmPermissions(
        this, GetExtension(), requested_permissions_.get());
  }

  return true;
}

}  // namespace extensions
