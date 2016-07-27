# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'action_service',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'constants',
      'dependencies': ['<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr'],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'externs',
      'includes': ['../../../../third_party/closure_compiler/externs_js.gypi'],
    },
    {
      'target_name': 'item',
      'dependencies': [
        # TODO(dbeam): create compiled_resources2.gyp files for Polymer.
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        'constants',
        'action_service',
        'externs',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'manager',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:util',
        '<(DEPTH)/ui/webui/resources/js/cr/compiled_resources2.gyp:ui',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:command',
        'action_service',
        'item',
        'toolbar',
        '<(EXTERNS_GYP):chrome_send',
        '../downloads/compiled_resources2.gyp:externs',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'toolbar',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:util',
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_search_field/compiled_resources2.gyp:cr_search_field',
        'action_service',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
