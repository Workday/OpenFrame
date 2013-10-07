// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// IPC messages for geolocation.
// Multiply-included message file, hence no include guard.

#include "content/public/common/geoposition.h"
#include "ipc/ipc_message_macros.h"
#include "url/gurl.h"

#define IPC_MESSAGE_START GeolocationMsgStart

IPC_ENUM_TRAITS(content::Geoposition::ErrorCode)

IPC_STRUCT_TRAITS_BEGIN(content::Geoposition)
  IPC_STRUCT_TRAITS_MEMBER(latitude)
  IPC_STRUCT_TRAITS_MEMBER(longitude)
  IPC_STRUCT_TRAITS_MEMBER(altitude)
  IPC_STRUCT_TRAITS_MEMBER(accuracy)
  IPC_STRUCT_TRAITS_MEMBER(altitude_accuracy)
  IPC_STRUCT_TRAITS_MEMBER(heading)
  IPC_STRUCT_TRAITS_MEMBER(speed)
  IPC_STRUCT_TRAITS_MEMBER(timestamp)
  IPC_STRUCT_TRAITS_MEMBER(error_code)
  IPC_STRUCT_TRAITS_MEMBER(error_message)
IPC_STRUCT_TRAITS_END()

// Messages sent from the browser to the renderer.

// Reply in response to GeolocationHostMsg_RequestPermission.
IPC_MESSAGE_ROUTED2(GeolocationMsg_PermissionSet,
                    int /* bridge_id */,
                    bool /* is_allowed */)

// Sent after GeolocationHostMsg_StartUpdating iff the user has granted
// permission and we have a position available or an error occurs (such as
// permission denied, position unavailable, etc.)
IPC_MESSAGE_ROUTED1(GeolocationMsg_PositionUpdated,
                    content::Geoposition /* geoposition */)

// Messages sent from the renderer to the browser.

// The |render_view_id| and |bridge_id| representing |host| is requesting
// permission to access geolocation position.
// This will be replied by GeolocationMsg_PermissionSet.
IPC_MESSAGE_CONTROL3(GeolocationHostMsg_RequestPermission,
                     int /* render_view_id */,
                     int /* bridge_id */,
                     GURL /* GURL of the frame requesting geolocation */)

// The |render_view_id| and |bridge_id| representing |GURL| is cancelling its
// previous permission request to access geolocation position.
IPC_MESSAGE_CONTROL3(GeolocationHostMsg_CancelPermissionRequest,
                     int /* render_view_id */,
                     int /* bridge_id */,
                     GURL /* GURL of the frame */)

// The |render_view_id| requests Geolocation service to start updating.
// This is an asynchronous call, and the browser process may eventually reply
// with the updated geoposition, or an error (access denied, location
// unavailable, etc.)
IPC_MESSAGE_CONTROL3(GeolocationHostMsg_StartUpdating,
                     int /* render_view_id */,
                     GURL /* GURL of the frame requesting geolocation */,
                     bool /* enable_high_accuracy */)

// The |render_view_id| requests Geolocation service to stop updating.
// Note that the geolocation service may continue to fetch geolocation data
// for other origins.
IPC_MESSAGE_CONTROL1(GeolocationHostMsg_StopUpdating,
                     int /* render_view_id */)
