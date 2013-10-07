// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/sync_file_system_custom_bindings.h"

#include <string>

#include "chrome/common/extensions/extension_constants.h"
#include "chrome/renderer/extensions/chrome_v8_context.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "v8/include/v8.h"
#include "webkit/common/fileapi/file_system_util.h"

namespace extensions {

SyncFileSystemCustomBindings::SyncFileSystemCustomBindings(
    Dispatcher* dispatcher, ChromeV8Context* context)
    : ChromeV8Extension(dispatcher, context) {
  RouteFunction(
      "GetSyncFileSystemObject",
      base::Bind(&SyncFileSystemCustomBindings::GetSyncFileSystemObject,
                 base::Unretained(this)));
}

void SyncFileSystemCustomBindings::GetSyncFileSystemObject(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  if (args.Length() != 2) {
    NOTREACHED();
    return;
  }
  if (!args[0]->IsString()) {
    NOTREACHED();
    return;
  }
  if (!args[1]->IsString()) {
    NOTREACHED();
    return;
  }

  std::string name(*v8::String::Utf8Value(args[0]));
  if (name.empty()) {
    NOTREACHED();
    return;
  }
  std::string root_url(*v8::String::Utf8Value(args[1]));
  if (root_url.empty()) {
    NOTREACHED();
    return;
  }

  WebKit::WebFrame* webframe =
      WebKit::WebFrame::frameForContext(context()->v8_context());
  args.GetReturnValue().Set(
    webframe->createFileSystem(WebKit::WebFileSystemTypeExternal,
                               WebKit::WebString::fromUTF8(name),
                               WebKit::WebString::fromUTF8(root_url)));
}

}  // namespace extensions
