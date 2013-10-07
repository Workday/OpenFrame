# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import posixpath
import traceback
import xml.dom.minidom as xml
from xml.parsers.expat import ExpatError

from appengine_url_fetcher import AppEngineUrlFetcher
from docs_server_utils import StringIdentity
from file_system import FileSystem, FileNotFoundError, StatInfo, ToUnicode
from future import Future
import svn_constants
import url_constants

def _ParseHTML(html):
  '''Unfortunately, the viewvc page has a stray </div> tag, so this takes care
  of all mismatched tags.
  '''
  try:
    return xml.parseString(html)
  except ExpatError as e:
    return _ParseHTML('\n'.join(
        line for (i, line) in enumerate(html.split('\n'))
        if e.lineno != i + 1))

def _InnerText(node):
  '''Like node.innerText in JS DOM, but strips surrounding whitespace.
  '''
  text = []
  if node.nodeValue:
    text.append(node.nodeValue)
  if hasattr(node, 'childNodes'):
    for child_node in node.childNodes:
      text.append(_InnerText(child_node))
  return ''.join(text).strip()

def _CreateStatInfo(html):
  parent_version = None
  child_versions = {}

  # Try all of the tables until we find the ones that contain the data (the
  # directory and file versions are in different tables).
  for table in _ParseHTML(html).getElementsByTagName('table'):
    # Within the table there is a list of files. However, there may be some
    # things beforehand; a header, "parent directory" list, etc. We will deal
    # with that below by being generous and just ignoring such rows.
    rows = table.getElementsByTagName('tr')

    for row in rows:
      cells = row.getElementsByTagName('td')

      # The version of the directory will eventually appear in the soup of
      # table rows, like this:
      #
      # <tr>
      #   <td>Directory revision:</td>
      #   <td><a href=... title="Revision 214692">214692</a> (of...)</td>
      # </tr>
      #
      # So look out for that.
      if len(cells) == 2 and _InnerText(cells[0]) == 'Directory revision:':
        links = cells[1].getElementsByTagName('a')
        if len(links) != 2:
          raise ValueError('ViewVC assumption invalid: directory revision ' +
                           'content did not have 2 <a> elements, instead %s' %
                           _InnerText(cells[1]))
        this_parent_version = _InnerText(links[0])
        int(this_parent_version)  # sanity check
        if parent_version is not None:
          raise ValueError('There was already a parent version %s, and we ' +
                           'just found a second at %s' % (parent_version,
                                                          this_parent_version))
        parent_version = this_parent_version

      # The version of each file is a list of rows with 5 cells: name, version,
      # age, author, and last log entry. Maybe the columns will change; we're
      # at the mercy viewvc, but this constant can be easily updated.
      if len(cells) != 5:
        continue
      name_element, version_element, _, __, ___ = cells

      name = _InnerText(name_element)  # note: will end in / for directories
      try:
        version = int(_InnerText(version_element))
      except StandardError:
        continue
      child_versions[name] = str(version)

    if parent_version and child_versions:
      break

  return StatInfo(parent_version, child_versions)

class _AsyncFetchFuture(object):
  def __init__(self, paths, fetcher, binary, args=None):
    def apply_args(path):
      return path if args is None else '%s?%s' % (path, args)
    # A list of tuples of the form (path, Future).
    self._fetches = [(path, fetcher.FetchAsync(apply_args(path)))
                     for path in paths]
    self._value = {}
    self._error = None
    self._binary = binary

  def _ListDir(self, directory):
    dom = xml.parseString(directory)
    files = [elem.childNodes[0].data for elem in dom.getElementsByTagName('a')]
    if '..' in files:
      files.remove('..')
    return files

  def Get(self):
    for path, future in self._fetches:
      try:
        result = future.Get()
      except Exception as e:
        raise FileNotFoundError(
            '%s fetching %s for Get: %s' % (e.__class__.__name__, path, e))
      if result.status_code == 404:
        raise FileNotFoundError('Got 404 when fetching %s for Get' % path)
      elif path.endswith('/'):
        self._value[path] = self._ListDir(result.content)
      elif not self._binary:
        self._value[path] = ToUnicode(result.content)
      else:
        self._value[path] = result.content
    if self._error is not None:
      raise self._error
    return self._value

class SubversionFileSystem(FileSystem):
  '''Class to fetch resources from src.chromium.org.
  '''
  @staticmethod
  def Create(branch='trunk', revision=None):
    if branch == 'trunk':
      svn_path = 'trunk/src/%s' % svn_constants.EXTENSIONS_PATH
    else:
      svn_path = 'branches/%s/src/%s' % (branch, svn_constants.EXTENSIONS_PATH)
    return SubversionFileSystem(
        AppEngineUrlFetcher('%s/%s' % (url_constants.SVN_URL, svn_path)),
        AppEngineUrlFetcher('%s/%s' % (url_constants.VIEWVC_URL, svn_path)),
        svn_path,
        revision=revision)

  def __init__(self, file_fetcher, stat_fetcher, svn_path, revision=None):
    self._file_fetcher = file_fetcher
    self._stat_fetcher = stat_fetcher
    self._svn_path = svn_path
    self._revision = revision

  def Read(self, paths, binary=False):
    args = None
    if self._revision is not None:
      # |fetcher| gets from svn.chromium.org which uses p= for version.
      args = 'p=%s' % self._revision
    return Future(delegate=_AsyncFetchFuture(paths,
                                             self._file_fetcher,
                                             binary,
                                             args=args))

  def Stat(self, path):
    directory, filename = posixpath.split(path)
    directory += '/'
    if self._revision is not None:
      # |stat_fetch| uses viewvc which uses pathrev= for version.
      directory += '?pathrev=%s' % self._revision

    try:
      result = self._stat_fetcher.Fetch(directory)
    except Exception as e:
      # Convert all errors (typically some sort of DeadlineExceededError but
      # explicitly catching that seems not to work) to a FileNotFoundError to
      # reduce the exception-catching surface area of this class.
      raise FileNotFoundError(
          '%s fetching %s for Stat: %s' % (e.__class__.__name__, path, e))

    if result.status_code != 200:
      raise FileNotFoundError('Got %s when fetching %s for Stat' % (
          result.status_code, path))

    stat_info = _CreateStatInfo(result.content)
    if stat_info.version is None:
      raise ValueError('Failed to find version of dir %s' % directory)
    if path.endswith('/'):
      return stat_info
    if filename not in stat_info.child_versions:
      raise FileNotFoundError(
          '%s from %s was not in child versions for Stat' % (filename, path))
    return StatInfo(stat_info.child_versions[filename])

  def GetIdentity(self):
    # NOTE: no revision here, since it would mess up the caching of reads. It
    # probably doesn't matter since all the caching classes will use the result
    # of Stat to decide whether to re-read - and Stat has a ceiling of the
    # revision - so when the revision changes, so might Stat. That is enough.
    return '@'.join((self.__class__.__name__, StringIdentity(self._svn_path)))
