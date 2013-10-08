# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'action_name': 'repack_chrome_touch_resources_100_percent',
  'variables': {
    'pak_inputs': [
      '<(grit_out_dir)/renderer_resources_100_percent.pak',
      '<(grit_out_dir)/theme_resources_touch_100_percent.pak',
      '<(SHARED_INTERMEDIATE_DIR)/ui/ui_resources/ui_resources_touch_100_percent.pak',
    ],
    'conditions': [
      ['OS != "ios"', {
        'pak_inputs': [
          '<(SHARED_INTERMEDIATE_DIR)/webkit/webkit_resources_100_percent.pak',
        ],
      }],
    ],
  },
  'inputs': [
    '<(repack_path)',
    '<@(pak_inputs)',
  ],
  'outputs': [
    '<(SHARED_INTERMEDIATE_DIR)/repack/chrome_touch_100_percent.pak',
  ],
  'action': ['python', '<(repack_path)', '<@(_outputs)', '<@(pak_inputs)'],
}
