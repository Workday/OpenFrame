# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'about_credits_file': '<(SHARED_INTERMEDIATE_DIR)/about_credits.html',
  },
  'targets': [
    {
      # GN version: //components/resources
      'target_name': 'components_resources',
      'type': 'none',
      'dependencies': [
        'about_credits',
      ],
      'hard_dependency': 1,
      'variables': {
        'grit_out_dir': '<(SHARED_INTERMEDIATE_DIR)/components',
      },
      'actions': [
        {
          # GN version: //components/resources:components_resources
          'action_name': 'generate_components_resources',
          'variables': {
            'grit_whitelist': '',
            'grit_grd_file': 'resources/components_resources.grd',
            'grit_additional_defines': [
              '-E', 'about_credits_file=<(about_credits_file)',
            ],
          },
          'includes': [ '../build/grit_action.gypi' ],
        },
        {
          # GN version: //components/resources:components_scaled_resources
          'action_name': 'generate_components_scaled_resources',
          'variables': {
            'grit_whitelist': '',
            'grit_grd_file': 'resources/components_scaled_resources.grd',
          },
          'includes': [ '../build/grit_action.gypi' ],
        },
      ],
      'includes': [ '../build/grit_target.gypi' ],
    },
    {
      # GN version: //components/resources:about_credits
      'target_name': 'about_credits',
      'type': 'none',
      'actions': [
        {
          'variables': {
            'generator_path': '../tools/licenses.py',
          },
          'action_name': 'generate_about_credits',
          'inputs': [
            # TODO(phajdan.jr): make licenses.py print license input files so
            # about:credits gets rebuilt when one changes.
            '<(generator_path)',
            'about_ui/resources/about_credits.tmpl',
            'about_ui/resources/about_credits_entry.tmpl',
          ],
          'outputs': [
            '<(about_credits_file)',
          ],
          'hard_dependency': 1,
          'action': ['python',
                     '<(generator_path)',
                     'credits',
                     '<(about_credits_file)',
          ],
          'message': 'Generating about:credits',
        },
      ],
    },
  ],
}
