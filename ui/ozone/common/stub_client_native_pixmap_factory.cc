// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/common/stub_client_native_pixmap_factory.h"

namespace ui {

namespace {

class StubClientNativePixmapFactory : public ClientNativePixmapFactory {
 public:
  StubClientNativePixmapFactory() {}
  ~StubClientNativePixmapFactory() override {}

  // ClientNativePixmapFactory:
  void Initialize(base::ScopedFD device_fd) override {}
  bool IsConfigurationSupported(gfx::BufferFormat format,
                                gfx::BufferUsage usage) const override {
    return false;
  }
  scoped_ptr<ClientNativePixmap> ImportFromHandle(
      const gfx::NativePixmapHandle& handle,
      const gfx::Size& size,
      gfx::BufferUsage usage) override {
    NOTREACHED();
    return nullptr;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(StubClientNativePixmapFactory);
};

}  // namespace

ClientNativePixmapFactory* CreateStubClientNativePixmapFactory() {
  return new StubClientNativePixmapFactory;
}

}  // namespace ui
