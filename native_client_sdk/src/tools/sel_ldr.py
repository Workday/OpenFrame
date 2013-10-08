#!/usr/bin/env python
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Wrapper script for launching application within the sel_ldr.
"""

import optparse
import os
import subprocess
import sys

import create_nmf
import getos

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
NACL_SDK_ROOT = os.path.dirname(SCRIPT_DIR)

if sys.version_info < (2, 6, 0):
  sys.stderr.write("python 2.6 or later is required run this script\n")
  sys.exit(1)


class Error(Exception):
  pass


def Log(msg):
  if Log.verbose:
    sys.stderr.write(str(msg) + '\n')
Log.verbose = False


def main(argv):
  usage = 'Usage: %prog [options] <.nexe>'
  description = __doc__
  epilog = 'Example: sel_ldr.py my_nexe.nexe'
  parser = optparse.OptionParser(usage, description=description, epilog=epilog)
  parser.add_option('-v', '--verbose', action='store_true',
                    help='Verbose output')
  parser.add_option('-d', '--debug', action='store_true',
                    help='Enable debug stub')
  parser.add_option('--debug-libs', action='store_true',
                    help='For dynamic executables, reference debug '
                         'libraries rather then release')
  options, args = parser.parse_args(argv)
  if not args:
    parser.error('No executable file specified')

  nexe = args[0]
  if options.verbose:
    Log.verbose = True

  osname = getos.GetPlatform()
  if not os.path.exists(nexe):
    raise Error('executable not found: %s' % nexe)
  if not os.path.isfile(nexe):
    raise Error('not a file: %s' % nexe)

  arch, dynamic = create_nmf.ParseElfHeader(nexe)
  if osname == 'mac' and arch == 'x86-64':
    raise Error('Running of x86-64 executables is not supported on mac')

  if arch == 'arm':
    raise Error('Cannot run ARM executables under sel_ldr')

  arch_suffix = arch.replace('-', '_')

  sel_ldr = os.path.join(SCRIPT_DIR, 'sel_ldr_%s' % arch_suffix)
  irt = os.path.join(SCRIPT_DIR, 'irt_core_%s.nexe' % arch_suffix)
  if osname == 'win':
    sel_ldr += '.exe'
  Log('ROOT    = %s' % NACL_SDK_ROOT)
  Log('SEL_LDR = %s' % sel_ldr)
  Log('IRT     = %s' % irt)
  cmd = [sel_ldr, '-a', '-B', irt, '-l', os.devnull]

  if options.debug:
    cmd.append('-g')

  if osname == 'linux':
    helper = os.path.join(SCRIPT_DIR, 'nacl_helper_bootstrap_%s' % arch_suffix)
    Log('HELPER  = %s' % helper)
    cmd.insert(0, helper)

  if dynamic:
    if options.debug_libs:
      libpath = os.path.join(NACL_SDK_ROOT, 'lib',
                            'glibc_%s' % arch_suffix, 'Debug')
    else:
      libpath = os.path.join(NACL_SDK_ROOT, 'lib',
                            'glibc_%s' % arch_suffix, 'Release')
    toolchain = '%s_x86_glibc' % osname
    sdk_lib_dir = os.path.join(NACL_SDK_ROOT, 'toolchain',
                               toolchain, 'x86_64-nacl')
    if arch == 'x86-64':
      sdk_lib_dir = os.path.join(sdk_lib_dir, 'lib')
    else:
      sdk_lib_dir = os.path.join(sdk_lib_dir, 'lib32')
    ldso = os.path.join(sdk_lib_dir, 'runnable-ld.so')
    cmd.append(ldso)
    Log('LD.SO   = %s' % ldso)
    libpath += ':' + sdk_lib_dir
    cmd.append('--library-path')
    cmd.append(libpath)


  cmd += args
  Log(cmd)
  rtn = subprocess.call(cmd)
  return rtn


if __name__ == '__main__':
  try:
    sys.exit(main(sys.argv[1:]))
  except Error as e:
    sys.stderr.write(str(e) + '\n')
    sys.exit(1)
