# Copyright (c) 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from threading import Lock

from common import utils
import crash_utils


class Blame(object):
  """Represents a blame object.

  The object contains blame information for one line of stack, and this
  information is shown when there are no CLs that change the crashing files.
  Attributes:
    line_content: The content of the line to find the blame for.
    component_name: The name of the component for this line.
    stack_frame_index: The stack frame index of this file.
    file_name: The name of the file.
    line_number: The line that caused a crash.
    author: The author of this line on the latest revision.
    revision: The latest revision of this line before the crash revision.
    message: The commit message for the revision.
    time: When the revision was committed.
    url: The url of the change for the revision.
    range_start: The starting range of the regression for this component.
    range_end: The ending range of the regression.

  """

  def __init__(self, line_content, component_name, stack_frame_index,
               file_name, line_number, author, revision, message, time,
               url, range_start, range_end):
    # Set all the variables from the arguments.
    self.line_content = line_content
    self.component_name = component_name
    self.stack_frame_index = stack_frame_index
    self.file = file_name
    self.line_number = line_number
    self.author = author
    self.revision = revision
    self.message = message
    self.time = time
    self.url = url
    self.range_start = range_start
    self.range_end = range_end


class BlameList(object):
  """Represents a list of blame objects.

  Thread-safe.
  """

  def __init__(self):
    self.blame_list = []
    self.blame_list_lock = Lock()

  def __getitem__(self, index):
    return self.blame_list[index]

  def FindBlame(self, callstack, component_to_crash_revision_dict,
                component_to_regression_dict, parsers,
                top_n_frames=10):
    """Given a stack within a stacktrace, retrieves blame information.

    Only either first 'top_n_frames' or the length of stack, whichever is
    shorter, results are returned. The default value of 'top_n_frames' is 10.

    Args:
      callstack: The list of stack frames.
      component_to_crash_revision_dict: A dictionary that maps component to its
                                        crash revision.
      component_to_regression_dict: A dictionary that maps component to its
                                    revision range.
      parsers: A list of two parsers, svn_parser and git_parser
      top_n_frames: A number of stack frames to show the blame result for.
    """
    # Only return blame information for first 'top_n_frames' frames.
    stack_frames = callstack.GetTopNFrames(top_n_frames)
    tasks = []
    # Iterate through frames in stack.
    for stack_frame in stack_frames:
      # If the component this line is from does not have a crash revision,
      # it is not possible to get blame information, so ignore this line.
      component_path = stack_frame.component_path
      if component_path not in component_to_crash_revision_dict:
        continue

      crash_revision = component_to_crash_revision_dict[
          component_path]['revision']
      range_start = None
      range_end = None
      repository_type = crash_utils.GetRepositoryType(crash_revision)
      repository_parser = parsers[repository_type]

      # If the revision is in SVN, and if regression information is available,
      # get it. For Git, we cannot know the ordering between hash numbers.
      if repository_type == 'svn':
        if component_to_regression_dict and \
            component_path in component_to_regression_dict:
          component_object = component_to_regression_dict[component_path]
          range_start = int(component_object['old_revision'])
          range_end = int(component_object['new_revision'])

      # Create a task to generate blame entry.
      tasks.append({
          'function': self.__GenerateBlameEntry,
          'args': [repository_parser, stack_frame, crash_revision,
                   range_start, range_end]})

    # Run all the tasks.
    crash_utils.RunTasks(tasks)

  def __GenerateBlameEntry(self, repository_parser, stack_frame,
                           crash_revision, range_start, range_end):
    """Generates blame list from the arguments."""
    stack_frame_index = stack_frame.index
    component_path = stack_frame.component_path
    component_name = stack_frame.component_name
    file_name = stack_frame.file_name
    file_path = stack_frame.file_path
    crashed_line_number = stack_frame.crashed_line_range[0]

    if file_path.startswith(component_path):
      file_path = file_path[len(component_path):]

    # Parse blame information.
    parsed_blame_info = repository_parser.ParseBlameInfo(
        component_path, file_path, crashed_line_number, crash_revision)

    # If it fails to retrieve information, do not do anything.
    if not parsed_blame_info:
      return

    # Create blame object from the parsed info and add it to the list.
    (line_content, revision, author, url, message, time) = parsed_blame_info
    blame = Blame(line_content, component_name, stack_frame_index, file_name,
                  crashed_line_number, author, revision, message, time, url,
                  range_start, range_end)

    with self.blame_list_lock:
      self.blame_list.append(blame)

  def FilterAndSortBlameList(self):
    """Filters and sorts the blame list."""
    # Sort the blame list by its position in stack.
    self.blame_list.sort(key=lambda blame: blame.stack_frame_index)

    filtered_blame_list = []

    for blame in self.blame_list:
      # If regression information is available, check if it needs to be
      # filtered.
      if blame.range_start and blame.range_end:

        # Discards results that are after the end of regression.
        if not utils.IsGitHash(blame.revision) and (
            int(blame.range_end) <= int(blame.revision)):
          continue

      filtered_blame_list.append(blame)

    self.blame_list = filtered_blame_list
