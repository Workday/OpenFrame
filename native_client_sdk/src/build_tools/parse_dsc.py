#!/usr/bin/env python
# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import collections
import fnmatch
import optparse
import os
import sys

VALID_TOOLCHAINS = ['newlib', 'glibc', 'pnacl', 'win', 'linux', 'mac']

# 'KEY' : ( <TYPE>, [Accepted Values], <Required?>)
DSC_FORMAT = {
    'DISABLE': (bool, [True, False], False),
    'SEL_LDR': (bool, [True, False], False),
    'DISABLE_PACKAGE': (bool, [True, False], False),
    'TOOLS' : (list, VALID_TOOLCHAINS, True),
    'CONFIGS' : (list, ['Debug', 'Release'], False),
    'PREREQ' : (list, '', False),
    'TARGETS' : (list, {
        'NAME': (str, '', True),
        # main = nexe target
        # lib = library target
        # so = shared object target, automatically added to NMF
        # so-standalone =  shared object target, not put into NMF
        'TYPE': (str, ['main', 'lib', 'static-lib', 'so', 'so-standalone'],
                 True),
        'SOURCES': (list, '', True),
        'CFLAGS': (list, '', False),
        'CFLAGS_GCC': (list, '', False),
        'CXXFLAGS': (list, '', False),
        'DEFINES': (list, '', False),
        'LDFLAGS': (list, '', False),
        'INCLUDES': (list, '', False),
        'LIBS' : (dict, VALID_TOOLCHAINS, False),
        'DEPS' : (list, '', False)
    }, True),
    'HEADERS': (list, {
        'FILES': (list, '', True),
        'DEST': (str, '', True),
    }, False),
    'SEARCH': (list, '', False),
    'POST': (str, '', False),
    'PRE': (str, '', False),
    'DEST': (str, ['examples/getting_started', 'examples/api',
                   'examples/demo', 'examples/tutorial',
                   'src', 'tests'], True),
    'NAME': (str, '', False),
    'DATA': (list, '', False),
    'TITLE': (str, '', False),
    'GROUP': (str, '', False),
    'EXPERIMENTAL': (bool, [True, False], False),
    'PERMISSIONS': (list, '', False),
    'SOCKET_PERMISSIONS': (list, '', False)
}


class ValidationError(Exception):
  pass


def ValidateFormat(src, dsc_format):
  # Verify all required keys are there
  for key in dsc_format:
    exp_type, exp_value, required = dsc_format[key]
    if required and key not in src:
      raise ValidationError('Missing required key %s.' % key)

  # For each provided key, verify it's valid
  for key in src:
    # Verify the key is known
    if key not in dsc_format:
      raise ValidationError('Unexpected key %s.' % key)

    exp_type, exp_value, required = dsc_format[key]
    value = src[key]

    # Verify the value is non-empty if required
    if required and not value:
      raise ValidationError('Expected non-empty value for %s.' % key)

    # If the expected type is a dict, but the provided type is a list
    # then the list applies to all keys of the dictionary, so we reset
    # the expected type and value.
    if exp_type is dict:
      if type(value) is list:
        exp_type = list
        exp_value = ''

    # Verify the key is of the expected type
    if exp_type != type(value):
      raise ValidationError('Key %s expects %s not %s.' % (
          key, exp_type.__name__.upper(), type(value).__name__.upper()))

    # If it's a bool, the expected values are always True or False.
    if exp_type is bool:
      continue

    # If it's a string and there are expected values, make sure it matches
    if exp_type is str:
      if type(exp_value) is list and exp_value:
        if value not in exp_value:
          raise ValidationError("Value '%s' not expected for %s." %
                                (value, key))
      continue

    # if it's a list, then we need to validate the values
    if exp_type is list:
      # If we expect a dictionary, then call this recursively
      if type(exp_value) is dict:
        for val in value:
          ValidateFormat(val, exp_value)
        continue
      # If we expect a list of strings
      if type(exp_value) is str:
        for val in value:
          if type(val) is not str:
            raise ValidationError('Value %s in %s is not a string.' %
                                  (val, key))
        continue
      # if we expect a particular string
      if type(exp_value) is list:
        for val in value:
          if val not in exp_value:
            raise ValidationError('Value %s not expected in %s.' %
                                  (val, key))
        continue

    # if we are expecting a dict, verify the keys are allowed
    if exp_type is dict:
      print "Expecting dict\n"
      for sub in value:
        if sub not in exp_value:
          raise ValidationError('Sub key %s not expected in %s.' %
                                (sub, key))
      continue

    # If we got this far, it's an unexpected type
    raise ValidationError('Unexpected type %s for key %s.' %
                          (str(type(src[key])), key))


def LoadProject(filename):
  with open(filename, 'r') as descfile:
    desc = eval(descfile.read(), {}, {})
  if desc.get('DISABLE', False):
    return None
  ValidateFormat(desc, DSC_FORMAT)
  desc['FILEPATH'] = os.path.abspath(filename)
  return desc


def LoadProjectTreeUnfiltered(srcpath):
  # Build the tree
  out = collections.defaultdict(list)
  for root, _, files in os.walk(srcpath):
    for filename in files:
      if fnmatch.fnmatch(filename, '*.dsc'):
        filepath = os.path.join(root, filename)
        try:
          desc = LoadProject(filepath)
        except ValidationError as e:
          raise ValidationError("Failed to validate: %s: %s" % (filepath, e))
        if desc:
          key = desc['DEST']
          out[key].append(desc)
  return out


def LoadProjectTree(srcpath, include, exclude=None):
  out = LoadProjectTreeUnfiltered(srcpath)
  return FilterTree(out, MakeDefaultFilterFn(include, exclude))


def GenerateProjects(tree):
  for key in tree:
    for val in tree[key]:
      yield key, val


def FilterTree(tree, filter_fn):
  out = collections.defaultdict(list)
  for branch, desc in GenerateProjects(tree):
    if filter_fn(desc):
      out[branch].append(desc)
  return out


def MakeDefaultFilterFn(include, exclude):
  def DefaultFilterFn(desc):
    matches_include = not include or DescMatchesFilter(desc, include)
    matches_exclude = exclude and DescMatchesFilter(desc, exclude)

    # Exclude list overrides include list.
    if matches_exclude:
      return False
    return matches_include

  return DefaultFilterFn


def DescMatchesFilter(desc, filters):
  for key, expected in filters.iteritems():
    # For any filtered key which is unspecified, assumed False
    value = desc.get(key, False)

    # If we provide an expected list, match at least one
    if type(expected) != list:
      expected = set([expected])
    if type(value) != list:
      value = set([value])

    if not set(expected) & set(value):
      return False

  # If we fall through, then we matched the filters
  return True


def PrintProjectTree(tree):
  for key in tree:
    print key + ':'
    for val in tree[key]:
      print '\t' + val['NAME']


def main(argv):
  parser = optparse.OptionParser(usage='%prog [options] <dir>')
  parser.add_option('-e', '--experimental',
      help='build experimental examples and libraries', action='store_true')
  parser.add_option('-t', '--toolchain',
      help='Build using toolchain. Can be passed more than once.',
      action='append')

  options, args = parser.parse_args(argv[1:])
  filters = {}

  load_from_dir = '.'
  if len(args) > 1:
    parser.error('Expected 0 or 1 args, got %d.' % len(args))

  if args:
    load_from_dir = args[0]

  if options.toolchain:
    filters['TOOLS'] = options.toolchain

  if not options.experimental:
    filters['EXPERIMENTAL'] = False

  try:
    tree = LoadProjectTree(load_from_dir, include=filters)
  except ValidationError as e:
    sys.stderr.write(str(e) + '\n')
    return 1

  PrintProjectTree(tree)
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv))
