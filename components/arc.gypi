# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    {
      # GN version: //components/arc
      'target_name': 'arc',
      'type': 'static_library',
      'include_dirs': [
        '..',
      ],
      'dependencies': [
        '../base/base.gyp:base',
        '../chromeos/chromeos.gyp:chromeos',
        '../ipc/ipc.gyp:ipc',
      ],
      'sources': [
        'arc/arc_bridge_service.cc',
        'arc/arc_bridge_service.h',
        'arc/arc_bridge_service_impl.cc',
        'arc/arc_bridge_service_impl.h',
        'arc/common/arc_host_messages.h',
        'arc/common/arc_instance_messages.h',
        'arc/common/arc_message_generator.cc',
        'arc/common/arc_message_generator.h',
      ],
    },
  ],
}
