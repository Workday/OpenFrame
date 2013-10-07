// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_HISTORY_SCORED_HISTORY_MATCH_H_
#define CHROME_BROWSER_HISTORY_SCORED_HISTORY_MATCH_H_

#include <map>
#include <set>
#include <vector>

#include "base/strings/string16.h"
#include "chrome/browser/autocomplete/history_provider_util.h"
#include "chrome/browser/history/history_types.h"
#include "chrome/browser/history/in_memory_url_index_types.h"

class BookmarkService;

namespace history {

// An HistoryMatch that has a score as well as metrics defining where in the
// history item's URL and/or page title matches have occurred.
struct ScoredHistoryMatch : public history::HistoryMatch {
  ScoredHistoryMatch();  // Required by STL.

  // Creates a new match with a raw score calculated for the history
  // item given in |row| with recent visits as indicated in |visits|.
  // First determines if the row qualifies by seeing if all of the
  // terms in |terms_vector| occur in |row|. If so, calculates a raw score.
  // This raw score allows the matches to be ordered and can be used to
  // influence the final score calculated by the client of this index.
  // If the row does not qualify the raw score will be 0. |bookmark_service| is
  // used to determine if the match's URL is referenced by any bookmarks.
  // |languages| is used to help parse/format the URL before looking for
  // the terms.
  ScoredHistoryMatch(const URLRow& row,
                     const VisitInfoVector& visits,
                     const std::string& languages,
                     const string16& lower_string,
                     const String16Vector& terms_vector,
                     const RowWordStarts& word_starts,
                     const base::Time now,
                     BookmarkService* bookmark_service);
  ~ScoredHistoryMatch();

  // Compares two matches by score.  Functor supporting URLIndexPrivateData's
  // HistoryItemsForTerms function.  Looks at particular fields within
  // with url_info to make tie-breaking a bit smarter.
  static bool MatchScoreGreater(const ScoredHistoryMatch& m1,
                                const ScoredHistoryMatch& m2);

  // Return a topicality score based on how many matches appear in the
  // |url| and the page's title and where they are (e.g., at word
  // boundaries).  |url_matches| and |title_matches| provide details
  // about where the matches in the URL and title are and what terms
  // (identified by a term number < |num_terms|) match where.
  // |word_starts| explains where word boundaries are.  Its parts (title
  // and url) must be sorted.  Also, |url_matches| and
  // |titles_matches| should already be sorted and de-duped.
  static float GetTopicalityScore(const int num_terms,
                                  const string16& url,
                                  const TermMatches& url_matches,
                                  const TermMatches& title_matches,
                                  const RowWordStarts& word_starts);

  // Precalculates raw_term_score_to_topicality_score, used in
  // GetTopicalityScore().
  static void FillInTermScoreToTopicalityScoreArray();

  // Returns a recency score based on |last_visit_days_ago|, which is
  // how many days ago the page was last visited.
  static float GetRecencyScore(int last_visit_days_ago);

  // Pre-calculates days_ago_to_recency_numerator_, used in
  // GetRecencyScore().
  static void FillInDaysAgoToRecencyScoreArray();

  // Examines the first kMaxVisitsToScore and return a score (higher is
  // better) based the rate of visits and how often those visits are
  // typed navigations (i.e., explicitly invoked by the user).
  // |now| is passed in to avoid unnecessarily recomputing it frequently.
  static float GetFrecency(const base::Time& now,
                           const VisitInfoVector& visits);

  // Combines the two component scores into a final score that's
  // an appropriate value to use as a relevancy score.
  static float GetFinalRelevancyScore(
      float topicality_score,
      float frecency_score);

  // Sets also_do_hup_like_scoring and
  // max_assigned_score_for_non_inlineable_matches based on the field
  // trial state.
  static void InitializeAlsoDoHUPLikeScoringFieldAndMaxScoreField();

  // An interim score taking into consideration location and completeness
  // of the match.
  int raw_score;
  TermMatches url_matches;  // Term matches within the URL.
  TermMatches title_matches;  // Term matches within the page title.
  bool can_inline;  // True if this is a candidate for in-line autocompletion.

  // Pre-computed information to speed up calculating recency scores.
  // |days_ago_to_recency_score| is a simple array mapping how long
  // ago a page was visited (in days) to the recency score we should
  // assign it.  This allows easy lookups of scores without requiring
  // math.  This is initialized upon first use of GetRecencyScore(),
  // which calls FillInDaysAgoToRecencyScoreArray(),
  static const int kDaysToPrecomputeRecencyScoresFor = 366;
  static float* days_ago_to_recency_score;

  // Pre-computed information to speed up calculating topicality
  // scores.  |raw_term_score_to_topicality_score| is a simple array
  // mapping how raw terms scores (a weighted sum of the number of
  // hits for the term, weighted by how important the hit is:
  // hostname, path, etc.) to the topicality score we should assign
  // it.  This allows easy lookups of scores without requiring math.
  // This is initialized upon first use of GetTopicalityScore(),
  // which calls FillInTermScoreToTopicalityScoreArray().
  static const int kMaxRawTermScore = 30;
  static float* raw_term_score_to_topicality_score;

  // Used so we initialize static variables only once (on first use).
  static bool initialized_;

  // The maximum number of recent visits to examine in GetFrecency().
  static const size_t kMaxVisitsToScore;

  // If true, assign raw scores to be max(whatever it normally would be,
  // a score that's similar to the score HistoryURL provider would assign).
  // This variable is set in the constructor by examining the field trial
  // state.
  static bool also_do_hup_like_scoring;

  // The maximum score that can be assigned to non-inlineable matches.
  // This is useful because often we want inlineable matches to come
  // first (even if they don't sometimes score as well as non-inlineable
  // matches) because if a non-inlineable match comes first than all matches
  // will get demoted later in HistoryQuickProvider to non-inlineable scores.
  // Set to -1 to indicate no maximum score.
  static int max_assigned_score_for_non_inlineable_matches;
};
typedef std::vector<ScoredHistoryMatch> ScoredHistoryMatches;

}  // namespace history

#endif  // CHROME_BROWSER_HISTORY_SCORED_HISTORY_MATCH_H_
