// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_RENDERER_GC_EXTENSION_H_
#define CONTENT_SHELL_RENDERER_GC_EXTENSION_H_

namespace v8 {
class Extension;
}

namespace extensions_v8 {

// GCExtension is a v8 extension to expose a method into JS for triggering
// garbage collection. This should only be used for debugging.
class GCExtension {
 public:
  static v8::Extension* Get();
};

}  // namespace extensions_v8

#endif  // CONTENT_SHELL_RENDERER_GC_EXTENSION_H_
