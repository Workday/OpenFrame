# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import shutil

from profile_creators import profile_generator
from profile_creators import small_profile_extender
from telemetry.page import page as page_module
from telemetry.page import shared_page_state
from telemetry import story


class Typical25ProfileSharedState(shared_page_state.SharedDesktopPageState):
  """Shared state associated with a profile generated from 25 navigations.

  Generates a shared profile on initialization.
  """

  def __init__(self, test, finder_options, story_set):
    super(Typical25ProfileSharedState, self).__init__(
        test, finder_options, story_set)
    generator = profile_generator.ProfileGenerator(
        small_profile_extender.SmallProfileExtender,
        'small_profile')
    self._out_dir, self._owns_out_dir = generator.Run(finder_options)
    if self._out_dir:
      finder_options.browser_options.profile_dir = self._out_dir
    else:
      finder_options.browser_options.dont_override_profile = True

  def TearDownState(self):
    """Clean up generated profile directory."""
    super(Typical25ProfileSharedState, self).TearDownState()
    if self._owns_out_dir:
      shutil.rmtree(self._out_dir)


class Typical25Page(page_module.Page):

  def __init__(self, url, page_set, run_no_page_interactions,
      shared_page_state_class=shared_page_state.SharedDesktopPageState):
    super(Typical25Page, self).__init__(
        url=url, page_set=page_set,
        shared_page_state_class=shared_page_state_class)
    self.archive_data_file = 'data/typical_25.json'
    self._run_no_page_interactions = run_no_page_interactions

  def RunPageInteractions(self, action_runner):
    if self._run_no_page_interactions:
      return
    with action_runner.CreateGestureInteraction('ScrollAction'):
      action_runner.ScrollPage()


class Typical25PageWithProfile(Typical25Page):
  """A page from the typical 25 set backed by a profile."""

  def __init__(self, url, page_set, run_no_page_interactions):
    super(Typical25PageWithProfile, self).__init__(
        url=url, page_set=page_set,
        run_no_page_interactions=run_no_page_interactions,
        shared_page_state_class=Typical25ProfileSharedState)


class Typical25PageSet(story.StorySet):

  """ Pages designed to represent the median, not highly optimized web """

  def __init__(self, run_no_page_interactions=False,
               page_class=Typical25Page):
    super(Typical25PageSet, self).__init__(
      archive_data_file='data/typical_25.json',
      cloud_storage_bucket=story.PARTNER_BUCKET)

    urls_list = [
      # Why: Alexa games #48
      'http://www.nick.com/games',
      # Why: Alexa sports #45
      'http://www.rei.com/',
      # Why: Alexa sports #50
      'http://www.fifa.com/',
      # Why: Alexa shopping #41
      'http://www.gamestop.com/ps3',
      # Why: Alexa shopping #25
      'http://www.barnesandnoble.com/u/books-bestselling-books/379003057/',
      # Why: Alexa news #55
      ('http://www.economist.com/news/science-and-technology/21573529-small-'
       'models-cosmic-phenomena-are-shedding-light-real-thing-how-build'),
      # Why: Alexa news #67
      'http://www.theonion.com',
      'http://arstechnica.com/',
      # Why: Alexa home #10
      'http://allrecipes.com/Recipe/Pull-Apart-Hot-Cross-Buns/Detail.aspx',
      'http://www.html5rocks.com/en/',
      'http://www.mlb.com/',
      # pylint: disable=line-too-long
      'http://gawker.com/5939683/based-on-a-true-story-is-a-rotten-lie-i-hope-you-never-believe',
      'http://www.imdb.com/title/tt0910970/',
      'http://www.flickr.com/search/?q=monkeys&f=hp',
      'http://money.cnn.com/',
      'http://www.nationalgeographic.com/',
      'http://premierleague.com',
      'http://www.osubeavers.com/',
      'http://walgreens.com',
      'http://colorado.edu',
      ('http://www.ticketmaster.com/JAY-Z-and-Justin-Timberlake-tickets/artist/'
       '1837448?brand=none&tm_link=tm_homeA_rc_name2'),
      # pylint: disable=line-too-long
      'http://www.theverge.com/2013/3/5/4061684/inside-ted-the-smartest-bubble-in-the-world',
      'http://www.airbnb.com/',
      'http://www.ign.com/',
      # Why: Alexa health #25
      'http://www.fda.gov',
    ]

    for url in urls_list:
      self.AddStory(
        page_class(url, self, run_no_page_interactions))


class Typical25PageSetWithProfile(Typical25PageSet):
  """ Similar to Typical25PageSet, but with a non-empty profile. """

  def __init__(self, run_no_page_interactions=False):
    super(Typical25PageSetWithProfile, self).__init__(
        run_no_page_interactions=run_no_page_interactions,
        page_class=Typical25PageWithProfile)
