#!/usr/bin/env python
# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import multiprocessing
import optparse
import os
import sys

import buildbot_common
import build_version
import generate_make
import parse_dsc

from build_paths import NACL_DIR, SDK_SRC_DIR, OUT_DIR, SDK_EXAMPLE_DIR
from build_paths import GSTORE
from generate_index import LandingPage

sys.path.append(os.path.join(SDK_SRC_DIR, 'tools'))
sys.path.append(os.path.join(NACL_DIR, 'build'))
import getos
import http_download


MAKE = 'nacl_sdk/make_3.99.90-26-gf80222c/make.exe'
LIB_DICT = {
  'linux': [],
  'mac': [],
  'win': ['x86_32']
}
VALID_TOOLCHAINS = ['newlib', 'glibc', 'pnacl', 'win', 'linux', 'mac']

# Global verbosity setting.
# If set to try (normally via a command line arg) then build_projects will
# add V=1 to all calls to 'make'
verbose = False


def CopyFilesFromTo(filelist, srcdir, dstdir):
  for filename in filelist:
    srcpath = os.path.join(srcdir, filename)
    dstpath = os.path.join(dstdir, filename)
    buildbot_common.CopyFile(srcpath, dstpath)


def UpdateHelpers(pepperdir, clobber=False):
  tools_dir = os.path.join(pepperdir, 'tools')
  if not os.path.exists(tools_dir):
    buildbot_common.ErrorExit('SDK tools dir is missing: %s' % tools_dir)

  exampledir = os.path.join(pepperdir, 'examples')
  if clobber:
    buildbot_common.RemoveDir(exampledir)
  buildbot_common.MakeDir(exampledir)

  # Copy files for individual bild and landing page
  files = ['favicon.ico', 'httpd.cmd']
  CopyFilesFromTo(files, SDK_EXAMPLE_DIR, exampledir)

  resourcesdir = os.path.join(SDK_EXAMPLE_DIR, 'resources')
  files = ['index.css', 'index.js', 'button_close.png',
      'button_close_hover.png']
  CopyFilesFromTo(files, resourcesdir, exampledir)

  # Copy tools scripts and make includes
  buildbot_common.CopyDir(os.path.join(SDK_SRC_DIR, 'tools', '*.py'),
      tools_dir)
  buildbot_common.CopyDir(os.path.join(SDK_SRC_DIR, 'tools', '*.mk'),
      tools_dir)

  # On Windows add a prebuilt make
  if getos.GetPlatform() == 'win':
    buildbot_common.BuildStep('Add MAKE')
    http_download.HttpDownload(GSTORE + MAKE,
                               os.path.join(tools_dir, 'make.exe'))


def ValidateToolchains(toolchains):
  invalid_toolchains = set(toolchains) - set(VALID_TOOLCHAINS)
  if invalid_toolchains:
    buildbot_common.ErrorExit('Invalid toolchain(s): %s' % (
        ', '.join(invalid_toolchains)))


def UpdateProjects(pepperdir, project_tree, toolchains,
                   clobber=False, configs=None, first_toolchain=False):
  if configs is None:
    configs = ['Debug', 'Release']
  if not os.path.exists(os.path.join(pepperdir, 'tools')):
    buildbot_common.ErrorExit('Examples depend on missing tools.')
  if not os.path.exists(os.path.join(pepperdir, 'toolchain')):
    buildbot_common.ErrorExit('Examples depend on missing toolchains.')

  ValidateToolchains(toolchains)

  # Create the library output directories
  libdir = os.path.join(pepperdir, 'lib')
  platform = getos.GetPlatform()
  for config in configs:
    for arch in LIB_DICT[platform]:
      dirpath = os.path.join(libdir, '%s_%s_host' % (platform, arch), config)
      if clobber:
        buildbot_common.RemoveDir(dirpath)
      buildbot_common.MakeDir(dirpath)

  landing_page = None
  for branch, projects in project_tree.iteritems():
    dirpath = os.path.join(pepperdir, branch)
    if clobber:
      buildbot_common.RemoveDir(dirpath)
    buildbot_common.MakeDir(dirpath)
    targets = [desc['NAME'] for desc in projects]

    # Generate master make for this branch of projects
    generate_make.GenerateMasterMakefile(pepperdir,
                                         os.path.join(pepperdir, branch),
                                         targets)

    if branch.startswith('examples') and not landing_page:
      landing_page = LandingPage()

    # Generate individual projects
    for desc in projects:
      srcroot = os.path.dirname(desc['FILEPATH'])
      generate_make.ProcessProject(pepperdir, srcroot, pepperdir, desc,
                                   toolchains, configs=configs,
                                   first_toolchain=first_toolchain)

      if branch.startswith('examples'):
        landing_page.AddDesc(desc)

  if landing_page:
    # Generate the landing page text file.
    index_html = os.path.join(pepperdir, 'examples', 'index.html')
    example_resources_dir = os.path.join(SDK_EXAMPLE_DIR, 'resources')
    index_template = os.path.join(example_resources_dir,
                                  'index.html.template')
    with open(index_html, 'w') as fh:
      out = landing_page.GeneratePage(index_template)
      fh.write(out)

  # Generate top Make for examples
  targets = ['api', 'demo', 'getting_started', 'tutorial']
  targets = [x for x in targets if 'examples/'+x in project_tree]
  branch_name = 'examples'
  generate_make.GenerateMasterMakefile(pepperdir,
                                       os.path.join(pepperdir, branch_name),
                                       targets)


def BuildProjectsBranch(pepperdir, branch, deps, clean, config, args=None):
  make_dir = os.path.join(pepperdir, branch)
  print "\nMake: " + make_dir

  if getos.GetPlatform() == 'win':
    # We need to modify the environment to build host on Windows.
    make = os.path.join(make_dir, 'make.bat')
  else:
    make = 'make'

  env = None
  if os.environ.get('USE_GOMA') == '1':
    env = dict(os.environ)
    env['NACL_COMPILER_PREFIX'] = 'gomacc'
    # Add -m32 to the CFLAGS when building using i686-nacl-gcc
    # otherwise goma won't recognise it as different to the x86_64
    # build.
    env['X86_32_CFLAGS'] = '-m32'
    env['X86_32_CXXFLAGS'] = '-m32'
    jobs = '50'
  else:
    jobs = str(multiprocessing.cpu_count())

  make_cmd = [make, '-j', jobs]

  make_cmd.append('CONFIG='+config)
  if not deps:
    make_cmd.append('IGNORE_DEPS=1')

  if verbose:
    make_cmd.append('V=1')

  if args:
    make_cmd += args
  else:
    make_cmd.append('TOOLCHAIN=all')

  buildbot_common.Run(make_cmd, cwd=make_dir, env=env)
  if clean:
    # Clean to remove temporary files but keep the built
    buildbot_common.Run(make_cmd + ['clean'], cwd=make_dir, env=env)


def BuildProjects(pepperdir, project_tree, deps=True,
                  clean=False, config='Debug'):

  # Make sure we build libraries (which live in 'src') before
  # any of the examples.
  build_first = [p for p in project_tree if p != 'src']
  build_second = [p for p in project_tree if p == 'src']

  for branch in build_first + build_second:
    BuildProjectsBranch(pepperdir, branch, deps, clean, config)


def main(args):
  parser = optparse.OptionParser()
  parser.add_option('-c', '--clobber',
      help='Clobber project directories before copying new files',
      action='store_true', default=False)
  parser.add_option('-b', '--build',
      help='Build the projects.', action='store_true')
  parser.add_option('--config',
      help='Choose configuration to build (Debug or Release).  Builds both '
           'by default')
  parser.add_option('-x', '--experimental',
      help='Build experimental projects', action='store_true')
  parser.add_option('-t', '--toolchain',
      help='Build using toolchain. Can be passed more than once.',
      action='append', default=[])
  parser.add_option('-d', '--dest',
      help='Select which build destinations (project types) are valid.',
      action='append')
  parser.add_option('-p', '--project',
      help='Select which projects are valid.',
      action='append')
  parser.add_option('-v', '--verbose', action='store_true')

  options, args = parser.parse_args(args[1:])
  if args:
    parser.error('Not expecting any arguments.')

  if 'NACL_SDK_ROOT' in os.environ:
    # We don't want the currently configured NACL_SDK_ROOT to have any effect
    # on the build.
    del os.environ['NACL_SDK_ROOT']

  pepper_ver = str(int(build_version.ChromeMajorVersion()))
  pepperdir = os.path.join(OUT_DIR, 'pepper_' + pepper_ver)

  if not options.toolchain:
    options.toolchain = ['newlib', 'glibc', 'pnacl', 'host']

  if 'host' in options.toolchain:
    options.toolchain.remove('host')
    options.toolchain.append(getos.GetPlatform())
    print 'Adding platform: ' + getos.GetPlatform()

  ValidateToolchains(options.toolchain)

  filters = {}
  if options.toolchain:
    filters['TOOLS'] = options.toolchain
    print 'Filter by toolchain: ' + str(options.toolchain)
  if not options.experimental:
    filters['EXPERIMENTAL'] = False
  if options.dest:
    filters['DEST'] = options.dest
    print 'Filter by type: ' + str(options.dest)
  if options.project:
    filters['NAME'] = options.project
    print 'Filter by name: ' + str(options.project)

  try:
    project_tree = parse_dsc.LoadProjectTree(SDK_SRC_DIR, include=filters)
  except parse_dsc.ValidationError as e:
    buildbot_common.ErrorExit(str(e))
  parse_dsc.PrintProjectTree(project_tree)

  UpdateHelpers(pepperdir, clobber=options.clobber)
  UpdateProjects(pepperdir, project_tree, options.toolchain,
                 clobber=options.clobber)

  if options.verbose:
    global verbose
    verbose = True

  if options.build:
    if options.config:
      configs = [options.config]
    else:
      configs = ['Debug', 'Release']
    for config in configs:
      BuildProjects(pepperdir, project_tree, config=config)

  return 0


if __name__ == '__main__':
  script_name = os.path.basename(sys.argv[0])
  try:
    sys.exit(main(sys.argv))
  except parse_dsc.ValidationError as e:
    buildbot_common.ErrorExit('%s: %s' % (script_name, e))
  except KeyboardInterrupt:
    buildbot_common.ErrorExit('%s: interrupted' % script_name)
