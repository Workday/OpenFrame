# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'chromium_code': 1,
    'webkit_compositor_bindings_tests_sources': [
      'web_animation_unittest.cc',
      'web_float_animation_curve_unittest.cc',
      'web_layer_impl_fixed_bounds_unittest.cc',
    ],
  },
  'targets': [
    {
      'target_name': 'webkit_compositor_bindings_unittests',
      'type' : '<(gtest_target_type)',
      'dependencies': [
        '<(DEPTH)/base/base.gyp:test_support_base',
        '<(DEPTH)/cc/cc.gyp:cc',
        '<(DEPTH)/cc/cc_tests.gyp:cc_test_support',
        '<(DEPTH)/skia/skia.gyp:skia',
        '<(DEPTH)/testing/gmock.gyp:gmock',
        '<(DEPTH)/testing/gtest.gyp:gtest',
        'compositor_bindings.gyp:webkit_compositor_bindings',
      ],
      'sources': [
        '<@(webkit_compositor_bindings_tests_sources)',
        'test/run_all_unittests.cc',
      ],
      'include_dirs': [
        '<(DEPTH)'
      ],
      'conditions': [
        ['OS == "android" and gtest_target_type == "shared_library"', {
          'dependencies': [
            '<(DEPTH)/testing/android/native_test.gyp:native_test_native_code',
          ],
        }],
        [ 'os_posix == 1 and OS != "mac" and OS != "android" and OS != "ios"', {
          'conditions': [
            [ 'linux_use_tcmalloc==1', {
              'dependencies': [
                '<(DEPTH)/base/allocator/allocator.gyp:allocator',
              ],
            }],
          ],
        }],
      ],
    },
  ],
  'conditions': [
    # Special target to wrap a gtest_target_type==shared_library
    # package webkit_compositor_bindings_unittests into an android apk for execution.
    ['OS == "android" and gtest_target_type == "shared_library"', {
      'targets': [
        {
          'target_name': 'webkit_compositor_bindings_unittests_apk',
          'type': 'none',
          'dependencies': [
            'webkit_compositor_bindings_unittests',
          ],
          'variables': {
            'test_suite_name': 'webkit_compositor_bindings_unittests',
            'input_shlib_path': '<(SHARED_LIB_DIR)/<(SHARED_LIB_PREFIX)webkit_compositor_bindings_unittests<(SHARED_LIB_SUFFIX)',
          },
          'includes': [ '../../../build/apk_test.gypi' ],
        },
      ],
    }],
  ],
}
