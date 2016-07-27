# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
from telemetry.page import page as page_module
from telemetry.page import shared_page_state
from telemetry import story

TIME_TO_WAIT_BEFORE_STARTING_IN_SECONDS = 5

# TODO(ulan): Remove this once crbug.com/541508 is fixed.
STARTUP_SCRIPT = '''
    window.WebSocket = undefined;
    window.Worker = undefined;
    window.performance = undefined;'''

def _ScrollAction(action_runner, scroll_amount, delay, repeat):
  with action_runner.CreateInteraction('Begin'):
    action_runner.tab.browser.DumpMemory()
  with action_runner.CreateInteraction('Scrolling'):
    action_runner.RepeatableBrowserDrivenScroll(
      y_scroll_distance_ratio=scroll_amount,
      repeat_delay_ms=delay,
      repeat_count=repeat)
  with action_runner.CreateInteraction('End'):
    action_runner.tab.browser.DumpMemory()

def _WaitAction(action_runner):
  action_runner.WaitForJavaScriptCondition(
    'document.body != null && '
    'document.body.scrollHeight > window.innerHeight && '
    '!document.body.addEventListener("touchstart", function() {})')
  action_runner.Wait(TIME_TO_WAIT_BEFORE_STARTING_IN_SECONDS)
  action_runner.ForceGarbageCollection()

def _CreateInfiniteScrollPageClass(base_page_cls):
  class DerivedSmoothPage(base_page_cls):  # pylint: disable=no-init
    def RunPageInteractions(self, action_runner):
      _WaitAction(action_runner)
      _ScrollAction(action_runner, self.scroll_amount, self.delay, self.repeat)
  return DerivedSmoothPage

class InfiniteScrollPage(page_module.Page):
  def __init__(self, url, page_set, name, scroll_amount, delay, repeat,
               credentials=None):
    super(InfiniteScrollPage, self).__init__(
        url=url, page_set=page_set, name=name,
        shared_page_state_class=shared_page_state.SharedPageState,
       credentials_path='data/credentials.json')
    self.credentials = credentials
    self.script_to_evaluate_on_commit = STARTUP_SCRIPT
    self.scroll_amount = scroll_amount
    self.delay = delay
    self.repeat = repeat

class InfiniteScrollPageSet(story.StorySet):
  """ Top pages that can be scrolled for many pages. """
  def __init__(self):
    super(InfiniteScrollPageSet, self).__init__(
        archive_data_file='data/infinite_scroll.json',
        cloud_storage_bucket=story.PARTNER_BUCKET)
    # The scroll distance is chosen such that the page can be scrolled
    # continiously throught the test without hitting the end of the page.
    SCROLL_FAR = 30
    SCROLL_NEAR = 13
    SCROLL_PAGE = 1
    pages = [
        ('https://www.facebook.com/shakira', 'facebook', SCROLL_FAR, 0, 0),
        ('https://twitter.com/taylorswift13', 'twitter', SCROLL_PAGE, 10, 30),
        ('http://espn.go.com/', 'espn', SCROLL_NEAR, 0, 0),
        ('https://www.yahoo.com', 'yahoo', SCROLL_NEAR, 0, 0),
        ('http://techcrunch.tumblr.com/', 'tumblr', SCROLL_FAR, 0, 0),
        ('https://www.flickr.com/explore', 'flickr', SCROLL_FAR, 0, 0)
    ]
    for (url, name, scroll_amount, delay, repeat) in pages:
      page_class = _CreateInfiniteScrollPageClass(InfiniteScrollPage)
      self.AddStory(page_class(url, self, name, scroll_amount, delay, repeat))
