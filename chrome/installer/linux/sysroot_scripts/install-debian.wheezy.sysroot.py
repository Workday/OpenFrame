#!/usr/bin/env python
# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Script to install a Debian Wheezy sysroot for making official Google Chrome
# Linux builds.
# The sysroot is needed to make Chrome work for Debian Wheezy.
# This script can be run manually but is more often run as part of gclient
# hooks. When run from hooks this script should be a no-op on non-linux
# platforms.

# The sysroot image could be constructed from scratch based on the current
# state or Debian Wheezy but for consistency we currently use a pre-built root
# image. The image will normally need to be rebuilt every time chrome's build
# dependancies are changed.

import platform
import optparse
import os
import re
import shutil
import subprocess
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
URL_PREFIX = 'https://commondatastorage.googleapis.com'
URL_PATH = 'chrome-linux-sysroot/toolchain'
REVISION = 36982
TARBALL_AMD64 = 'debian_wheezy_amd64_sysroot.tgz'
TARBALL_I386 = 'debian_wheezy_i386_sysroot.tgz'
SYSROOT_DIR_AMD64 = 'debian_wheezy_amd64-sysroot'
SYSROOT_DIR_I386 = 'debian_wheezy_i386-sysroot'


def main(args):
  if options.arch not in ['amd64', 'i386']:
    print 'Unknown architecture: %s' % options.arch
    return 1

  if options.linux_only:
    # This argument is passed when run from the gclient hooks.
    # In this case we return early on non-linux platforms.
    if not sys.platform.startswith('linux'):
      return 0

    # Only install the sysroot for an Official Chrome Linux build.
    defined = ['branding=Chrome', 'buildtype=Official']
    undefined = ['chromeos=1']
    gyp_defines = os.environ.get('GYP_DEFINES', '')
    for option in defined:
      if option not in gyp_defines:
        return 0
    for option in undefined:
      if option in gyp_defines:
        return 0

    # Check for optional target_arch and only install for that architecture.
    # If target_arch is not specified, then only install for the host
    # architecture.
    host_arch = ''
    if 'target_arch=x64' in gyp_defines:
      host_arch = 'amd64'
    elif 'target_arch=ia32' in gyp_defines:
      host_arch = 'i386'
    else:
      # Figure out host arch, like the host_arch variable in build/common.gypi.
      machine_type = platform.machine()
      if machine_type in ['amd64', 'x86_64']:
        host_arch = 'amd64'
      elif re.match('(i[3-6]86|i86pc)$', machine_type):
        host_arch = 'i386'
    if host_arch != options.arch:
      return 0

  # The sysroot directory should match the one specified in build/common.gypi.
  # TODO(thestig) Consider putting this else where to avoid having to recreate
  # it on every build.
  linux_dir = os.path.dirname(SCRIPT_DIR)
  if options.arch == 'amd64':
    sysroot = os.path.join(linux_dir, SYSROOT_DIR_AMD64)
    tarball_filename = TARBALL_AMD64
  else:
    sysroot = os.path.join(linux_dir, SYSROOT_DIR_I386)
    tarball_filename = TARBALL_I386
  url = '%s/%s/%s/%s' % (URL_PREFIX, URL_PATH, REVISION, tarball_filename)

  stamp = os.path.join(sysroot, '.stamp')
  if os.path.exists(stamp):
    with open(stamp) as s:
      if s.read() == url:
        print 'Debian Wheezy %s root image already up-to-date: %s' % \
            (options.arch, sysroot)
        return 0

  print 'Installing Debian Wheezy %s root image: %s' % (options.arch, sysroot)
  if os.path.isdir(sysroot):
    shutil.rmtree(sysroot)
  os.mkdir(sysroot)
  tarball = os.path.join(sysroot, tarball_filename)
  subprocess.check_call(['curl', '-L', url, '-o', tarball])
  subprocess.check_call(['tar', 'xf', tarball, '-C', sysroot])
  os.remove(tarball)

  with open(stamp, 'w') as s:
    s.write(url)
  return 0


if __name__ == '__main__':
  parser = optparse.OptionParser('usage: %prog [OPTIONS]')
  parser.add_option('', '--linux-only', dest='linux_only', action='store_true',
                    default=False, help='Only install sysroot for official '
                                        'Linux builds')
  parser.add_option('', '--arch', dest='arch',
                    help='Sysroot architecture, i386 or amd64')
  options, args = parser.parse_args()
  sys.exit(main(options))
