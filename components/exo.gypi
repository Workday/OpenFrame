# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    {
      # GN version: //components/exo
      'target_name': 'exo',
      'type': 'static_library',
      'include_dirs': [
        '..',
      ],
      'dependencies': [
        '../ash/ash.gyp:ash',
        '../base/base.gyp:base',
        '../cc/cc.gyp:cc',
        '../gpu/gpu.gyp:gpu',
        '../skia/skia.gyp:skia',
        '../ui/aura/aura.gyp:aura',
        '../ui/compositor/compositor.gyp:compositor',
        '../ui/gfx/gfx.gyp:gfx',
        '../ui/gfx/gfx.gyp:gfx_geometry',
        '../ui/gl/gl.gyp:gl',
        '../ui/views/views.gyp:views',
      ],
      'sources': [
        # Note: sources list duplicated in GN build.
        'exo/buffer.cc',
        'exo/buffer.h',
        'exo/display.cc',
        'exo/display.h',
        'exo/shared_memory.cc',
        'exo/shared_memory.h',
        'exo/shell_surface.cc',
        'exo/shell_surface.h',
        'exo/sub_surface.cc',
        'exo/sub_surface.h',
        'exo/surface.cc',
        'exo/surface.h',
        'exo/surface_delegate.h',
        'exo/surface_observer.h',
      ],
    },
  ],
  'conditions': [
    [ 'OS=="linux"', {
      'targets': [
        {
          # GN version: //components/exo:wayland
          'target_name': 'exo_wayland',
          'type': 'static_library',
          'include_dirs': [
            '..',
          ],
          'dependencies': [
             '../base/base.gyp:base',
            '../skia/skia.gyp:skia',
            '../third_party/wayland/wayland.gyp:wayland_server',
            'exo',
          ],
          'sources': [
            # Note: sources list duplicated in GN build.
            'exo/wayland/scoped_wl_types.cc',
            'exo/wayland/scoped_wl_types.h',
            'exo/wayland/server.cc',
            'exo/wayland/server.h',
          ],
          'conditions': [
            ['use_ozone==1', {
              'dependencies': [
                '../third_party/mesa/mesa.gyp:wayland_drm_protocol',
              ],
            }],
          ],
        },
      ],
    }],
  ],
}
