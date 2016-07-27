# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'includes': [
    'chrome_features.gypi',
  ],
  'targets': [
    {
      # GN version: //chrome/common:features
      'target_name': 'chrome_common_features',
      'includes': [ '../build/buildflag_header.gypi' ],
      'variables': {
        'buildflag_header_path': 'chrome/common/features.h',
        'buildflag_flags': [
          'ENABLE_GOOGLE_NOW=<(enable_google_now)',
        ],
      },
    },
  ]
}
