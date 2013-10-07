# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging

_APP_YAML_CONTAINER = '''
application: chrome-apps-doc
version: %s
runtime: python27
api_version: 1
threadsafe: false
'''

class AppYamlHelper(object):
  '''Parses the app.yaml file, and is able to step back in the host file
  system's revision history to find when it changed to some given version.
  '''
  def __init__(self,
               app_yaml_path,
               file_system_at_head,
               object_store_creator,
               host_file_system_creator):
    self._app_yaml_path = app_yaml_path
    self._file_system_at_head = file_system_at_head
    self._store = object_store_creator.Create(
        AppYamlHelper,
        category=file_system_at_head.GetIdentity(),
        start_empty=False)
    self._host_file_system_creator = host_file_system_creator

  @staticmethod
  def ExtractVersion(app_yaml, key='version'):
    '''Extracts the 'version' key from the contents of an app.yaml file.
    Allow overriding the key to parse e.g. the cron file ('target').
    '''
    # We could properly parse this using a yaml library but Python doesn't have
    # one built in so whatevs.
    key_colon = '%s:' % key
    versions = [line.strip()[len(key_colon):].strip()
                for line in app_yaml.split('\n')
                if line.strip().startswith(key_colon)]
    if not versions:
      raise ValueError('No versions found for %s in %s' % (
          key, app_yaml))
    if len(set(versions)) > 1:
      raise ValueError('Inconsistent versions found for %s in %s: %s' % (
          key, app_yaml, versions))
    return versions[0]

  @staticmethod
  def IsGreater(lhs, rhs):
    '''Return whether the app.yaml version |lhs| > |rhs|. This is tricky
    because versions are typically not numbers but rather 2-0-9, 2-0-12,
    2-1-0, etc - and 2-1-0 > 2-0-10 > 2-0-9.
    '''
    lhs_parts = lhs.replace('-', '.').split('.')
    rhs_parts = rhs.replace('-', '.').split('.')
    while lhs_parts and rhs_parts:
      lhs_msb = int(lhs_parts.pop(0))
      rhs_msb = int(rhs_parts.pop(0))
      if lhs_msb != rhs_msb:
        return lhs_msb > rhs_msb
    return len(lhs) > len(rhs)

  @staticmethod
  def GenerateAppYaml(version):
    '''Probably only useful for tests.
    '''
    return _APP_YAML_CONTAINER % version

  def IsUpToDate(self, app_version):
    '''Returns True if the |app_version| is up to date with respect to the one
    checked into the host file system.
    '''
    checked_in_app_version = AppYamlHelper.ExtractVersion(
        self._file_system_at_head.ReadSingle(self._app_yaml_path))
    if app_version == checked_in_app_version:
      return True
    if AppYamlHelper.IsGreater(app_version, checked_in_app_version):
      logging.warning(
          'Server is too new! Checked in %s < currently running %s' % (
              checked_in_app_version, app_version))
      return True
    return False

  def GetFirstRevisionGreaterThan(self, app_version):
    '''Finds the first revision that the version in app.yaml was greater than
    |app_version|.

    WARNING: if there is no such revision (e.g. the app is up to date, or
    *oops* the app is even newer) then this will throw a ValueError. Use
    IsUpToDate to validate the input before calling this method.
    '''
    stored = self._store.Get(app_version).Get()
    if stored is None:
      stored = self._GetFirstRevisionGreaterThanImpl(app_version)
      assert stored is not None
      self._store.Set(app_version, stored)
    return stored

  def _GetFirstRevisionGreaterThanImpl(self, app_version):
    def get_app_yaml_revision(file_system):
      return int(file_system.Stat(self._app_yaml_path).version)

    def has_greater_app_version(file_system):
      app_version_in_file_system = AppYamlHelper.ExtractVersion(
          file_system.ReadSingle(self._app_yaml_path))
      return AppYamlHelper.IsGreater(app_version_in_file_system, app_version)

    found = None
    next_file_system = self._file_system_at_head

    while has_greater_app_version(next_file_system):
      found = get_app_yaml_revision(next_file_system)
      # Back up a revision then find when app.yaml was last updated before then.
      if found == 0:
        logging.warning('All revisions are greater than %s' % app_version)
        return 0
      next_file_system = self._host_file_system_creator.Create(
          revision=found - 1)

    if found is None:
      raise ValueError('All revisions are less than %s' % app_version)
    return found
