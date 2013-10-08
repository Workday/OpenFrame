// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests make sure MediaGalleriesPermission values are parsed correctly.

#include "base/values.h"
#include "chrome/common/extensions/permissions/api_permission.h"
#include "chrome/common/extensions/permissions/media_galleries_permission.h"
#include "chrome/common/extensions/permissions/media_galleries_permission_data.h"
#include "chrome/common/extensions/permissions/permissions_info.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::SocketPermissionRequest;
using extensions::SocketPermissionData;

namespace extensions {

namespace {

TEST(MediaGalleriesPermissionTest, GoodValues) {
  const APIPermissionInfo* permission_info =
    PermissionsInfo::GetInstance()->GetByID(APIPermission::kMediaGalleries);

  scoped_ptr<APIPermission> permission(
      permission_info->CreateAPIPermission());

  // access_type + all_detected
  scoped_ptr<base::ListValue> value(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kAllAutoDetectedPermission);
  value->AppendString(MediaGalleriesPermission::kReadPermission);
  EXPECT_TRUE(permission->FromValue(value.get()));

  value.reset(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kAllAutoDetectedPermission);
  value->AppendString(MediaGalleriesPermission::kCopyToPermission);
  EXPECT_TRUE(permission->FromValue(value.get()));

  value.reset(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kAllAutoDetectedPermission);
  value->AppendString(MediaGalleriesPermission::kCopyToPermission);
  value->AppendString(MediaGalleriesPermission::kReadPermission);
  EXPECT_TRUE(permission->FromValue(value.get()));

  // all_detected
  value.reset(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kAllAutoDetectedPermission);
  EXPECT_TRUE(permission->FromValue(value.get()));

  // access_type
  value.reset(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kReadPermission);
  EXPECT_TRUE(permission->FromValue(value.get()));

  value.reset(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kCopyToPermission);
  EXPECT_TRUE(permission->FromValue(value.get()));

  value.reset(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kCopyToPermission);
  value->AppendString(MediaGalleriesPermission::kReadPermission);
  EXPECT_TRUE(permission->FromValue(value.get()));

  // Repeats are ok.
  value.reset(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kAllAutoDetectedPermission);
  value->AppendString(MediaGalleriesPermission::kAllAutoDetectedPermission);
  EXPECT_TRUE(permission->FromValue(value.get()));

  value.reset(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kCopyToPermission);
  value->AppendString(MediaGalleriesPermission::kCopyToPermission);
  EXPECT_TRUE(permission->FromValue(value.get()));

  value.reset(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kAllAutoDetectedPermission);
  value->AppendString(MediaGalleriesPermission::kAllAutoDetectedPermission);
  value->AppendString(MediaGalleriesPermission::kCopyToPermission);
  EXPECT_TRUE(permission->FromValue(value.get()));
}

TEST(MediaGalleriesPermissionTest, BadValues) {
  const APIPermissionInfo* permission_info =
    PermissionsInfo::GetInstance()->GetByID(APIPermission::kMediaGalleries);

  scoped_ptr<APIPermission> permission(permission_info->CreateAPIPermission());

  // Empty
  scoped_ptr<base::ListValue> value(new base::ListValue());
  EXPECT_FALSE(permission->FromValue(value.get()));
}

TEST(MediaGalleriesPermissionTest, Equal) {
  const APIPermissionInfo* permission_info =
    PermissionsInfo::GetInstance()->GetByID(APIPermission::kMediaGalleries);

  scoped_ptr<APIPermission> permission1(
      permission_info->CreateAPIPermission());
  scoped_ptr<APIPermission> permission2(
      permission_info->CreateAPIPermission());

  scoped_ptr<base::ListValue> value(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kAllAutoDetectedPermission);
  value->AppendString(MediaGalleriesPermission::kReadPermission);
  ASSERT_TRUE(permission1->FromValue(value.get()));

  value.reset(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kReadPermission);
  value->AppendString(MediaGalleriesPermission::kAllAutoDetectedPermission);
  ASSERT_TRUE(permission2->FromValue(value.get()));
  EXPECT_TRUE(permission1->Equal(permission2.get()));

  value.reset(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kReadPermission);
  value->AppendString(MediaGalleriesPermission::kReadPermission);
  value->AppendString(MediaGalleriesPermission::kAllAutoDetectedPermission);
  ASSERT_TRUE(permission2->FromValue(value.get()));
  EXPECT_TRUE(permission1->Equal(permission2.get()));
}

TEST(MediaGalleriesPermissionTest, ToFromValue) {
  const APIPermissionInfo* permission_info =
    PermissionsInfo::GetInstance()->GetByID(APIPermission::kMediaGalleries);

  scoped_ptr<APIPermission> permission1(
      permission_info->CreateAPIPermission());
  scoped_ptr<APIPermission> permission2(
      permission_info->CreateAPIPermission());

  scoped_ptr<base::ListValue> value(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kAllAutoDetectedPermission);
  value->AppendString(MediaGalleriesPermission::kReadPermission);
  ASSERT_TRUE(permission1->FromValue(value.get()));

  scoped_ptr<base::Value> vtmp(permission1->ToValue());
  ASSERT_TRUE(vtmp);
  ASSERT_TRUE(permission2->FromValue(vtmp.get()));
  EXPECT_TRUE(permission1->Equal(permission2.get()));

  value.reset(new base::ListValue());
  value->AppendString(MediaGalleriesPermission::kCopyToPermission);
  ASSERT_TRUE(permission1->FromValue(value.get()));

  vtmp = permission1->ToValue();
  ASSERT_TRUE(vtmp);
  ASSERT_TRUE(permission2->FromValue(vtmp.get()));
  EXPECT_TRUE(permission1->Equal(permission2.get()));
}

}  // namespace

}  // namespace extensions
