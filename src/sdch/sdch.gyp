# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'chromium_code': 1,
  },
  'targets': [
    {
      'target_name': 'sdch',
      'type': 'static_library',
      'dependencies': [
        '../third_party/zlib/zlib.gyp:zlib',
      ],
      'sources': [
        'open-vcdiff/src/addrcache.cc',
        'open-vcdiff/src/blockhash.cc',
        'open-vcdiff/src/blockhash.h',
        'open-vcdiff/src/checksum.h',
        'open-vcdiff/src/codetable.cc',
        'open-vcdiff/src/codetable.h',
        'open-vcdiff/src/compile_assert.h',
        'open-vcdiff/src/decodetable.cc',
        'open-vcdiff/src/decodetable.h',
        'open-vcdiff/src/encodetable.cc',
        'open-vcdiff/src/encodetable.h',
        'open-vcdiff/src/google/output_string.h',
        'open-vcdiff/src/google/vcdecoder.h',
        'open-vcdiff/src/headerparser.cc',
        'open-vcdiff/src/headerparser.h',
        'open-vcdiff/src/instruction_map.cc',
        'open-vcdiff/src/instruction_map.h',
        'open-vcdiff/src/logging.cc',
        'open-vcdiff/src/logging.h',
        'open-vcdiff/src/rolling_hash.h',
        'open-vcdiff/src/testing.h',
        'open-vcdiff/src/varint_bigendian.cc',
        'open-vcdiff/src/varint_bigendian.h',
        'open-vcdiff/src/vcdecoder.cc',
        'open-vcdiff/src/vcdiff_defs.h',
        'open-vcdiff/src/vcdiffengine.cc',
        'open-vcdiff/src/vcdiffengine.h',
        'open-vcdiff/vsprojects/config.h',
        'open-vcdiff/vsprojects/stdint.h',
      ],
      'include_dirs': [
        'open-vcdiff/src',
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          'open-vcdiff/src',
        ],
      },
      'conditions': [
        [ 'OS == "linux" or OS == "android"', { 'include_dirs': [ 'linux' ] } ],
        [ 'os_bsd==1 or OS=="solaris"', { 'include_dirs': [ 'bsd' ] } ],
        [ 'OS == "ios"', { 'include_dirs': [ 'ios' ] } ],
        [ 'OS == "mac"', { 'include_dirs': [ 'mac' ] } ],
        [ 'OS == "win"', { 'include_dirs': [ 'open-vcdiff/vsprojects' ] } ],
      ],
    },
  ],
}
