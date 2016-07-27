# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import os
import sys

sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
from catapult_base import support_binaries
from catapult_base.dependency_manager import base_config
from catapult_base.dependency_manager import exceptions


DEFAULT_TYPE = 'default'


class DependencyManager(object):
  def __init__(self, configs, supported_config_types=None):
    """Manages file dependencies found locally or in cloud_storage.

    Args:
        configs: A list of instances of BaseConfig or it's subclasses, passed
            in decreasing order of precedence.
        supported_config_types: A list of whitelisted config_types.
            No restrictions if None is specified.

    Raises:
        ValueError: If |configs| is not a list of instances of BaseConfig or
            its subclasses.
        UnsupportedConfigFormatError: If supported_config_types is specified and
            configs contains a config not in the supported config_types.

    Example: DependencyManager([config1, config2, config3])
        No requirements on the type of Config, and any dependencies that have
        local files for the same platform will first look in those from
        config1, then those from config2, and finally those from config3.
    """
    if configs is None or type(configs) != list:
      raise ValueError(
          'Must supply a list of config files to DependencyManager')
    # self._lookup_dict is a dictionary with the following format:
    # { dependency1: {platform1: dependency_info1,
    #                 platform2: dependency_info2}
    #   dependency2: {platform1: dependency_info3,
    #                  ...}
    #   ...}
    #
    # Where the dependencies and platforms are strings, and the
    # dependency_info's are DependencyInfo instances.
    self._lookup_dict = {}
    self.supported_configs = supported_config_types or []
    for config in configs:
      self._UpdateDependencies(config)

  def FetchPath(self, dependency, platform, try_support_binaries=False):
    """Get a path to an executable for |dependency|, downloading as needed.

    A path to a default executable may be returned if a platform specific
    version is not specified in the config(s).

    Args:
        dependency: Name of the desired dependency, as given in the config(s)
            used in this DependencyManager.
        platform: Name of the platform the dependency will run on. Often of the
            form 'os_architecture'. Must match those specified in the config(s)
            used in this DependencyManager.
        try_support_binaries: True if support_binaries should be queried if the
            dependency_manager was not initialized with data for |dependency|.

    Returns:
        A path to an executable of |dependency| that will run on |platform|,
        downloading from cloud storage if needed.

    Raises:
        NoPathFoundError: If a local copy of the executable cannot be found and
            a remote path could not be downloaded from cloud_storage.
        CredentialsError: If cloud_storage credentials aren't configured.
        PermissionError: If cloud_storage credentials are configured, but not
            with an account that has permission to download the remote file.
        NotFoundError: If the remote file does not exist where expected in
            cloud_storage.
        ServerError: If an internal server error is hit while downloading the
            remote file.
        CloudStorageError: If another error occured while downloading the remote
            path.
        FileNotFoundError: If an attempted download was otherwise unsuccessful.

    """
    dependency_info = self._GetDependencyInfo(dependency, platform)
    if not dependency_info:
      if not try_support_binaries:
        raise exceptions.NoPathFoundError(dependency, platform)
      # TODO(aiolos): Remove the support_binaries call and always raise
      # NoPathFound once the binary dependencies are moved over to the new
      # system.

      # platform should be of the form '%s_%s' % (os_name, arch_name) when
      # called from the binary_manager.
      platform_parts = platform.split('_', 1)
      assert len(platform_parts) == 2
      platform_os, platform_arch = platform_parts
      logging.info('Calling into support_binaries with dependency %s, platform '
                   '%s and arch %s. support_binaries is deprecated.'
                   % (dependency, platform_os, platform_arch))
      return support_binaries.FindPath(dependency, platform_arch,
                                       platform_os)
    path = self._LocalPath(dependency_info)
    if not path or not os.path.exists(path):
      path = dependency_info.GetRemotePath()
      if not path or not os.path.exists(path):
        raise exceptions.NoPathFoundError(dependency, platform)
    return path

  def LocalPath(self, dependency, platform, try_support_binaries=False):
    """Get a path to a locally stored executable for |dependency|.

    A path to a default executable may be returned if a platform specific
    version is not specified in the config(s).
    Will not download the executable.

    Args:
        dependency: Name of the desired dependency, as given in the config(s)
            used in this DependencyManager.
        platform: Name of the platform the dependency will run on. Often of the
            form 'os_architecture'. Must match those specified in the config(s)
            used in this DependencyManager.
        try_support_binaries: True if support_binaries should be queried if the
            dependency_manager was not initialized with data for |dependency|.

    Returns:
        A path to an executable for |dependency| that will run on |platform|.

    Raises:
        NoPathFoundError: If a local copy of the executable cannot be found.
    """
    # TODO(aiolos): Remove the support_binaries call and always raise
    # NoPathFound once the binary dependencies are moved over to the new
    # system.
    dependency_info = self._GetDependencyInfo(dependency, platform)
    if not dependency_info:
      if not try_support_binaries:
        raise exceptions.NoPathFoundError(dependency, platform)
      return support_binaries.FindLocallyBuiltPath(dependency)
    local_path = self._LocalPath(dependency_info)
    if not local_path or not os.path.exists(local_path):
      raise exceptions.NoPathFoundError(dependency, platform)
    return local_path

  def _UpdateDependencies(self, config):
    """Add the dependency information stored in |config| to this instance.

    Args:
        config: An instances of BaseConfig or a subclasses.

    Raises:
        UnsupportedConfigFormatError: If supported_config_types was specified
        and config is not in the supported config_types.
    """
    if not isinstance(config, base_config.BaseConfig):
      raise ValueError('Must use a BaseConfig or subclass instance with the '
                       'DependencyManager.')
    if (self.supported_configs and
        config.GetConfigType() not in self.supported_configs):
      raise exceptions.UnsupportedConfigFormatError(config.GetConfigType(),
                                                    config.config_path)
    for dep_info in config.IterDependencyInfo():
      dependency = dep_info.dependency
      platform = dep_info.platform
      if dependency not in self._lookup_dict:
        self._lookup_dict[dependency] = {}
      if platform not in self._lookup_dict[dependency]:
        self._lookup_dict[dependency][platform] = dep_info
      else:
        self._lookup_dict[dependency][platform].Update(dep_info)


  def _GetDependencyInfo(self, dependency, platform):
    """Get information for |dependency| on |platform|, or a default if needed.

    Args:
        dependency: Name of the desired dependency, as given in the config(s)
            used in this DependencyManager.
        platform: Name of the platform the dependency will run on. Often of the
            form 'os_architecture'. Must match those specified in the config(s)
            used in this DependencyManager.

    Returns: The dependency_info for |dependency| on |platform| if it exists.
        Or the default version of |dependency| if it exists, or None if neither
        exist.
    """
    if not self._lookup_dict or dependency not in self._lookup_dict:
      return None
    dependency_dict = self._lookup_dict[dependency]
    device_type = platform
    if not device_type in dependency_dict:
      device_type = DEFAULT_TYPE
    return dependency_dict.get(device_type)

  @staticmethod
  def _LocalPath(dependency_info):
    """Return a path to a locally stored file for |dependency_info|.

    Will not download the file.

    Args:
        dependency_info: A DependencyInfo instance for the dependency to be
            found and the platform it should run on.

    Returns: A path to a local file, or None if not found.
    """
    if dependency_info:
      paths = dependency_info.local_paths
      for local_path in paths:
        if os.path.exists(local_path):
          return local_path
    return None
