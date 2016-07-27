// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Common IPC messages used for render processes.
// Multiply-included message file, hence no include guard.

#include <string>

#include "base/memory/shared_memory.h"
#include "ipc/ipc_message_macros.h"
#include "ipc/ipc_message_utils.h"
#include "url/gurl.h"

#if defined(OS_MACOSX)
#include "content/common/mac/font_descriptor.h"
#endif

#undef IPC_MESSAGE_EXPORT
#define IPC_MESSAGE_EXPORT CONTENT_EXPORT

#define IPC_MESSAGE_START RenderProcessMsgStart

#if defined(OS_MACOSX)
IPC_STRUCT_TRAITS_BEGIN(FontDescriptor)
  IPC_STRUCT_TRAITS_MEMBER(font_name)
  IPC_STRUCT_TRAITS_MEMBER(font_point_size)
IPC_STRUCT_TRAITS_END()
#endif

////////////////////////////////////////////////////////////////////////////////
// Messages sent from the browser to the render process.

////////////////////////////////////////////////////////////////////////////////
// Messages sent from the render process to the browser.

// Asks the browser process to generate a keypair for grabbing a client
// certificate from a CA (<keygen> tag), and returns the signed public
// key and challenge string.
IPC_SYNC_MESSAGE_CONTROL3_1(RenderProcessHostMsg_Keygen,
                            uint32 /* key size index */,
                            std::string /* challenge string */,
                            GURL /* URL of requestor */,
                            std::string /* signed public key and challenge */)

// Message sent from the renderer to the browser to request that the browser
// cache |data| associated with |url| and |expected_response_time|.
IPC_MESSAGE_CONTROL3(RenderProcessHostMsg_DidGenerateCacheableMetadata,
                     GURL /* url */,
                     base::Time /* expected_response_time */,
                     std::vector<char> /* data */)

// Notify the browser that this render process can or can't be suddenly
// terminated.
IPC_MESSAGE_CONTROL1(RenderProcessHostMsg_SuddenTerminationChanged,
                     bool /* enabled */)

#if defined(OS_MACOSX)
// Request that the browser load a font into shared memory for us.
IPC_SYNC_MESSAGE_CONTROL1_3(RenderProcessHostMsg_LoadFont,
                            FontDescriptor /* font to load */,
                            uint32 /* buffer size */,
                            base::SharedMemoryHandle /* font data */,
                            uint32 /* font id */)
#elif defined(OS_WIN)
// Request that the given font characters be loaded by the browser so it's
// cached by the OS. Please see RenderMessageFilter::OnPreCacheFontCharacters
// for details.
IPC_SYNC_MESSAGE_CONTROL2_0(RenderProcessHostMsg_PreCacheFontCharacters,
                            LOGFONT /* font_data */,
                            base::string16 /* characters */)

// Asks the browser for the user's monitor profile.
IPC_SYNC_MESSAGE_CONTROL0_1(RenderProcessHostMsg_GetMonitorColorProfile,
                            std::vector<char> /* profile */)
#endif
