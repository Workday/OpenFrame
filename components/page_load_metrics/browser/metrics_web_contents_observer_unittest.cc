// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/page_load_metrics/browser/metrics_web_contents_observer.h"

#include "base/memory/scoped_ptr.h"
#include "base/process/kill.h"
#include "base/test/histogram_tester.h"
#include "base/time/time.h"
#include "components/page_load_metrics/common/page_load_metrics_messages.h"
#include "components/rappor/rappor_utils.h"
#include "components/rappor/test_rappor_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace page_load_metrics {

namespace {

const char kDefaultTestUrl[] = "https://google.com";
const char kDefaultTestUrlAnchor[] = "https://google.com#samepage";
const char kDefaultTestUrl2[] = "https://whatever.com";

}  //  namespace

class TestPageLoadMetricsEmbedderInterface
    : public PageLoadMetricsEmbedderInterface {
 public:
  TestPageLoadMetricsEmbedderInterface() : is_prerendering_(false) {}
  rappor::TestRapporService* GetRapporService() override {
    return &rappor_tester_;
  }
  bool IsPrerendering(content::WebContents* web_contents) override {
    return is_prerendering_;
  }
  void set_is_prerendering(bool is_prerendering) {
    is_prerendering_ = is_prerendering;
  }

 private:
  bool is_prerendering_;
  rappor::TestRapporService rappor_tester_;
};

class MetricsWebContentsObserverTest
    : public content::RenderViewHostTestHarness {
 public:
  MetricsWebContentsObserverTest()
      : num_provisional_events_(0),
      num_provisional_events_bg_(0),
      num_committed_events_(0),
      num_committed_events_bg_(0),
      num_errors_(0) {}

  void SetUp() override {
    RenderViewHostTestHarness::SetUp();
    AttachObserver();
  }

  void AttachObserver() {
    embedder_interface_ = new TestPageLoadMetricsEmbedderInterface();
    observer_.reset(new MetricsWebContentsObserver(
        web_contents(), make_scoped_ptr(embedder_interface_)));
    observer_->WasShown();
  }

  void AssertNoHistogramsLogged() {
    histogram_tester_.ExpectTotalCount(kHistogramDomContentLoaded, 0);
    histogram_tester_.ExpectTotalCount(kHistogramLoad, 0);
    histogram_tester_.ExpectTotalCount(kHistogramFirstLayout, 0);
    histogram_tester_.ExpectTotalCount(kHistogramFirstTextPaint, 0);
  }

  void CheckProvisionalEvent(ProvisionalLoadEvent event,
                             int count,
                             bool background) {
    if (background) {
      histogram_tester_.ExpectBucketCount(kBackgroundProvisionalEvents, event,
                                          count);
      num_provisional_events_bg_ += count;
    } else {
      histogram_tester_.ExpectBucketCount(kProvisionalEvents, event, count);
      num_provisional_events_ += count;
    }
  }

  void CheckCommittedEvent(CommittedLoadEvent event,
                           int count,
                           bool background) {
    if (background) {
      histogram_tester_.ExpectBucketCount(kBackgroundCommittedEvents, event,
                                          count);
      num_committed_events_bg_ += count;
    } else {
      histogram_tester_.ExpectBucketCount(kCommittedEvents, event, count);
      num_committed_events_ += count;
    }
  }

  void CheckErrorEvent(InternalErrorLoadEvent error, int count) {
    histogram_tester_.ExpectBucketCount(kErrorEvents, error, count);
    num_errors_ += count;
  }

  void CheckTotalEvents() {
    histogram_tester_.ExpectTotalCount(kProvisionalEvents,
                                       num_provisional_events_);
    histogram_tester_.ExpectTotalCount(kCommittedEvents, num_committed_events_);
    histogram_tester_.ExpectTotalCount(kBackgroundProvisionalEvents,
                                       num_provisional_events_bg_);
    histogram_tester_.ExpectTotalCount(kBackgroundCommittedEvents,
                                       num_committed_events_bg_);
    histogram_tester_.ExpectTotalCount(kErrorEvents, num_errors_);
  }

 protected:
  base::HistogramTester histogram_tester_;
  TestPageLoadMetricsEmbedderInterface* embedder_interface_;
  scoped_ptr<MetricsWebContentsObserver> observer_;

 private:
  int num_provisional_events_;
  int num_provisional_events_bg_;
  int num_committed_events_;
  int num_committed_events_bg_;
  int num_errors_;

  DISALLOW_COPY_AND_ASSIGN(MetricsWebContentsObserverTest);
};

TEST_F(MetricsWebContentsObserverTest, NoMetrics) {
  AssertNoHistogramsLogged();
}

TEST_F(MetricsWebContentsObserverTest, NotInMainFrame) {
  base::TimeDelta first_layout = base::TimeDelta::FromMilliseconds(1);

  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(1);
  timing.first_layout = first_layout;

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  content::RenderFrameHostTester* rfh_tester =
      content::RenderFrameHostTester::For(main_rfh());
  content::RenderFrameHost* subframe = rfh_tester->AppendChild("subframe");

  content::RenderFrameHostTester* subframe_tester =
      content::RenderFrameHostTester::For(subframe);
  subframe_tester->SimulateNavigationStart(GURL(kDefaultTestUrl2));
  subframe_tester->SimulateNavigationCommit(GURL(kDefaultTestUrl2));
  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      subframe);
  subframe_tester->SimulateNavigationStop();

  // Navigate again to see if the timing updated for a subframe message.
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  AssertNoHistogramsLogged();
}

TEST_F(MetricsWebContentsObserverTest, SamePageNoTrigger) {
  base::TimeDelta first_layout = base::TimeDelta::FromMilliseconds(1);

  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(1);
  timing.first_layout = first_layout;

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      web_contents()->GetMainFrame());
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrlAnchor));
  // A same page navigation shouldn't trigger logging UMA for the original.
  AssertNoHistogramsLogged();
}

TEST_F(MetricsWebContentsObserverTest, SamePageNoTriggerUntilTrueNavCommit) {
  base::TimeDelta first_layout = base::TimeDelta::FromMilliseconds(1);

  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(1);
  timing.first_layout = first_layout;

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      web_contents()->GetMainFrame());
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrlAnchor));
  // A same page navigation shouldn't trigger logging UMA for the original.
  AssertNoHistogramsLogged();

  // But we should keep the timing info and log it when we get another
  // navigation.
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl2));
  histogram_tester_.ExpectTotalCount(kHistogramDomContentLoaded, 0);
  histogram_tester_.ExpectTotalCount(kHistogramLoad, 0);
  histogram_tester_.ExpectTotalCount(kHistogramFirstLayout, 1);
  histogram_tester_.ExpectBucketCount(kHistogramFirstLayout,
                                      first_layout.InMilliseconds(), 1);
  histogram_tester_.ExpectTotalCount(kHistogramFirstTextPaint, 0);
}

TEST_F(MetricsWebContentsObserverTest, SingleMetricAfterCommit) {
  base::TimeDelta first_layout = base::TimeDelta::FromMilliseconds(1);

  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(1);
  timing.first_layout = first_layout;

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      web_contents()->GetMainFrame());

  AssertNoHistogramsLogged();

  // Navigate again to force histogram recording.
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl2));

  histogram_tester_.ExpectTotalCount(kHistogramDomContentLoaded, 0);
  histogram_tester_.ExpectTotalCount(kHistogramLoad, 0);
  histogram_tester_.ExpectTotalCount(kHistogramFirstLayout, 1);
  histogram_tester_.ExpectBucketCount(kHistogramFirstLayout,
                                      first_layout.InMilliseconds(), 1);
  histogram_tester_.ExpectTotalCount(kHistogramFirstTextPaint, 0);
}

TEST_F(MetricsWebContentsObserverTest, MultipleMetricsAfterCommits) {
  base::TimeDelta first_layout_1 = base::TimeDelta::FromMilliseconds(1);
  base::TimeDelta first_layout_2 = base::TimeDelta::FromMilliseconds(20);
  base::TimeDelta response = base::TimeDelta::FromMilliseconds(10);
  base::TimeDelta first_text_paint = base::TimeDelta::FromMilliseconds(30);
  base::TimeDelta dom_content = base::TimeDelta::FromMilliseconds(40);
  base::TimeDelta load = base::TimeDelta::FromMilliseconds(100);

  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(1);
  timing.first_layout = first_layout_1;
  timing.response_start = response;
  timing.first_text_paint = first_text_paint;
  timing.dom_content_loaded_event_start = dom_content;
  timing.load_event_start = load;

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      web_contents()->GetMainFrame());

  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl2));

  PageLoadTiming timing2;
  timing2.navigation_start = base::Time::FromDoubleT(200);
  timing2.first_layout = first_layout_2;

  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing2),
      web_contents()->GetMainFrame());

  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  histogram_tester_.ExpectBucketCount(kHistogramFirstLayout,
                                      first_layout_1.InMilliseconds(), 1);
  histogram_tester_.ExpectTotalCount(kHistogramFirstLayout, 2);
  histogram_tester_.ExpectBucketCount(kHistogramFirstLayout,
                                      first_layout_1.InMilliseconds(), 1);
  histogram_tester_.ExpectBucketCount(kHistogramFirstLayout,
                                      first_layout_2.InMilliseconds(), 1);

  histogram_tester_.ExpectTotalCount(kHistogramFirstTextPaint, 1);
  histogram_tester_.ExpectBucketCount(kHistogramFirstTextPaint,
                                      first_text_paint.InMilliseconds(), 1);

  histogram_tester_.ExpectTotalCount(kHistogramDomContentLoaded, 1);
  histogram_tester_.ExpectBucketCount(kHistogramDomContentLoaded,
                                      dom_content.InMilliseconds(), 1);

  histogram_tester_.ExpectTotalCount(kHistogramLoad, 1);
  histogram_tester_.ExpectBucketCount(kHistogramLoad, load.InMilliseconds(), 1);
}

TEST_F(MetricsWebContentsObserverTest, BackgroundDifferentHistogram) {
  base::TimeDelta first_layout = base::TimeDelta::FromSeconds(2);

  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(1);
  timing.first_layout = first_layout;

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());

  // Simulate "Open link in new tab."
  observer_->WasHidden();
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      web_contents()->GetMainFrame());

  // Simulate switching to the tab and making another navigation.
  observer_->WasShown();
  AssertNoHistogramsLogged();

  // Navigate again to force histogram recording.
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl2));

  histogram_tester_.ExpectTotalCount(kBackgroundHistogramDomContentLoaded, 0);
  histogram_tester_.ExpectTotalCount(kBackgroundHistogramLoad, 0);
  histogram_tester_.ExpectTotalCount(kBackgroundHistogramFirstLayout, 1);
  histogram_tester_.ExpectBucketCount(kBackgroundHistogramFirstLayout,
                                      first_layout.InMilliseconds(), 1);
  histogram_tester_.ExpectTotalCount(kBackgroundHistogramFirstTextPaint, 0);

  histogram_tester_.ExpectTotalCount(kHistogramDomContentLoaded, 0);
  histogram_tester_.ExpectTotalCount(kHistogramLoad, 0);
  histogram_tester_.ExpectTotalCount(kHistogramFirstLayout, 0);
  histogram_tester_.ExpectTotalCount(kHistogramFirstTextPaint, 0);
}

TEST_F(MetricsWebContentsObserverTest, DontLogPrerender) {
  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(1);
  timing.first_layout = base::TimeDelta::FromMilliseconds(10);
  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  embedder_interface_->set_is_prerendering(true);

  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));
  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      web_contents()->GetMainFrame());

  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl2));
  AssertNoHistogramsLogged();
}

TEST_F(MetricsWebContentsObserverTest, OnlyBackgroundLaterEvents) {
  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(1);
  // Set these events at 1 microsecond so they are definitely occur before we
  // background the tab later in the test.
  timing.response_start = base::TimeDelta::FromMicroseconds(1);
  timing.dom_content_loaded_event_start = base::TimeDelta::FromMicroseconds(1);

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());

  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));
  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      web_contents()->GetMainFrame());

  // Background the tab, then forground it.
  observer_->WasHidden();
  observer_->WasShown();
  timing.first_layout = base::TimeDelta::FromSeconds(3);
  timing.first_text_paint = base::TimeDelta::FromSeconds(4);
  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      web_contents()->GetMainFrame());

  // Navigate again to force histogram recording.
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl2));

  histogram_tester_.ExpectTotalCount(kBackgroundHistogramDomContentLoaded, 0);
  histogram_tester_.ExpectTotalCount(kBackgroundHistogramLoad, 0);
  histogram_tester_.ExpectTotalCount(kBackgroundHistogramFirstLayout, 1);
  histogram_tester_.ExpectBucketCount(kBackgroundHistogramFirstLayout,
                                      timing.first_layout.InMilliseconds(), 1);
  histogram_tester_.ExpectTotalCount(kBackgroundHistogramFirstTextPaint, 1);
  histogram_tester_.ExpectBucketCount(kBackgroundHistogramFirstTextPaint,
                                      timing.first_text_paint.InMilliseconds(),
                                      1);

  histogram_tester_.ExpectTotalCount(kHistogramDomContentLoaded, 1);
  histogram_tester_.ExpectBucketCount(
      kHistogramDomContentLoaded,
      timing.dom_content_loaded_event_start.InMilliseconds(), 1);
  histogram_tester_.ExpectTotalCount(kHistogramLoad, 0);
  histogram_tester_.ExpectTotalCount(kHistogramFirstLayout, 0);
  histogram_tester_.ExpectTotalCount(kHistogramFirstTextPaint, 0);
}

TEST_F(MetricsWebContentsObserverTest, DontBackgroundQuickerLoad) {
  // Set this event at 1 microsecond so it occurs before we foreground later in
  // the test.
  base::TimeDelta first_layout = base::TimeDelta::FromMicroseconds(1);

  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(1);
  timing.first_layout = first_layout;

  observer_->WasHidden();

  // Open in new tab
  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());

  web_contents_tester->StartNavigation(GURL(kDefaultTestUrl));

  content::RenderFrameHostTester* rfh_tester =
      content::RenderFrameHostTester::For(main_rfh());

  // Switch to the tab
  observer_->WasShown();

  // Start another provisional load
  web_contents_tester->StartNavigation(GURL(kDefaultTestUrl2));
  rfh_tester->SimulateNavigationCommit(GURL(kDefaultTestUrl2));
  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      main_rfh());
  rfh_tester->SimulateNavigationStop();

  // Navigate again to see if the timing updated for the foregrounded load.
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  histogram_tester_.ExpectTotalCount(kHistogramDomContentLoaded, 0);
  histogram_tester_.ExpectTotalCount(kHistogramLoad, 0);
  histogram_tester_.ExpectTotalCount(kHistogramFirstLayout, 1);
  histogram_tester_.ExpectBucketCount(kHistogramFirstLayout,
                                      first_layout.InMilliseconds(), 1);
  histogram_tester_.ExpectTotalCount(kHistogramFirstTextPaint, 0);
}

TEST_F(MetricsWebContentsObserverTest, FailProvisionalLoad) {
  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());

  web_contents_tester->StartNavigation(GURL(kDefaultTestUrl));
  content::RenderFrameHostTester* rfh_tester =
      content::RenderFrameHostTester::For(main_rfh());
  rfh_tester->SimulateNavigationError(GURL(kDefaultTestUrl),
                                      net::ERR_TIMED_OUT);
  rfh_tester->SimulateNavigationStop();

  CheckProvisionalEvent(PROVISIONAL_LOAD_ERR_FAILED_NON_ABORT, 1, false);
  CheckProvisionalEvent(PROVISIONAL_LOAD_ERR_ABORTED, 0, false);
  CheckTotalEvents();
}

TEST_F(MetricsWebContentsObserverTest, AbortProvisionalLoad) {
  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());

  web_contents_tester->StartNavigation(GURL(kDefaultTestUrl));
  content::RenderFrameHostTester* rfh_tester =
      content::RenderFrameHostTester::For(main_rfh());
  rfh_tester->SimulateNavigationError(GURL(kDefaultTestUrl), net::ERR_ABORTED);
  rfh_tester->SimulateNavigationStop();

  CheckProvisionalEvent(PROVISIONAL_LOAD_ERR_FAILED_NON_ABORT, 0, false);
  CheckProvisionalEvent(PROVISIONAL_LOAD_ERR_ABORTED, 1, false);
  CheckTotalEvents();
}

TEST_F(MetricsWebContentsObserverTest, AbortProvisionalLoadInBackground) {
  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());

  observer_->WasHidden();
  web_contents_tester->StartNavigation(GURL(kDefaultTestUrl));
  content::RenderFrameHostTester* rfh_tester =
      content::RenderFrameHostTester::For(main_rfh());
  rfh_tester->SimulateNavigationError(GURL(kDefaultTestUrl), net::ERR_ABORTED);
  rfh_tester->SimulateNavigationStop();

  CheckProvisionalEvent(PROVISIONAL_LOAD_ERR_FAILED_NON_ABORT, 0, true);
  CheckProvisionalEvent(PROVISIONAL_LOAD_ERR_ABORTED, 1, true);
  CheckTotalEvents();
}

TEST_F(MetricsWebContentsObserverTest, DontLogIrrelevantNavigation) {
  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(10);

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());

  GURL about_blank_url = GURL("about:blank");
  web_contents_tester->NavigateAndCommit(about_blank_url);

  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      main_rfh());

  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  CheckProvisionalEvent(PROVISIONAL_LOAD_COMMITTED, 2, false);
  CheckCommittedEvent(COMMITTED_LOAD_STARTED, 1, false);
  CheckErrorEvent(ERR_IPC_FROM_BAD_URL_SCHEME, 1);
  CheckErrorEvent(ERR_IPC_WITH_NO_COMMITTED_LOAD, 1);
  CheckTotalEvents();
}

TEST_F(MetricsWebContentsObserverTest, NotInMainError) {
  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(1);
  timing.first_layout = base::TimeDelta::FromMilliseconds(1);

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  content::RenderFrameHostTester* rfh_tester =
      content::RenderFrameHostTester::For(main_rfh());
  content::RenderFrameHost* subframe = rfh_tester->AppendChild("subframe");

  content::RenderFrameHostTester* subframe_tester =
      content::RenderFrameHostTester::For(subframe);
  subframe_tester->SimulateNavigationStart(GURL(kDefaultTestUrl2));
  subframe_tester->SimulateNavigationCommit(GURL(kDefaultTestUrl2));
  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      subframe);
  CheckErrorEvent(ERR_IPC_FROM_WRONG_FRAME, 1);
}

TEST_F(MetricsWebContentsObserverTest, AbortCommittedLoadBeforeFirstLayout) {
  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(10);

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      main_rfh());
  // Navigate again to force histogram logging.
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl2));

  CheckProvisionalEvent(PROVISIONAL_LOAD_COMMITTED, 2, false);
  CheckCommittedEvent(COMMITTED_LOAD_STARTED, 2, false);
  CheckCommittedEvent(COMMITTED_LOAD_FAILED_BEFORE_FIRST_LAYOUT, 1, false);
  CheckTotalEvents();
}

TEST_F(MetricsWebContentsObserverTest, SuccessfulFirstLayoutInForegroundEvent) {
  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(10);
  timing.first_layout = base::TimeDelta::FromMilliseconds(100);

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      main_rfh());
  // Navigate again to force histogram logging.
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl2));

  CheckProvisionalEvent(PROVISIONAL_LOAD_COMMITTED, 2, false);
  CheckCommittedEvent(COMMITTED_LOAD_STARTED, 2, false);
  CheckCommittedEvent(COMMITTED_LOAD_SUCCESSFUL_FIRST_LAYOUT, 1, false);
  CheckTotalEvents();
}

TEST_F(MetricsWebContentsObserverTest,
       SuccessfulFirstLayoutInBackgroundEvent) {
  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(1);
  timing.first_layout = base::TimeDelta::FromSeconds(30);

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  // Background the tab.
  observer_->WasHidden();
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      main_rfh());

  observer_->WasShown();
  // Navigate again to force histogram logging.
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl2));

  CheckProvisionalEvent(PROVISIONAL_LOAD_COMMITTED, 1, true);
  CheckProvisionalEvent(PROVISIONAL_LOAD_COMMITTED, 1, false);
  CheckCommittedEvent(COMMITTED_LOAD_STARTED, 1, true);
  CheckCommittedEvent(COMMITTED_LOAD_STARTED, 1, false);
  CheckCommittedEvent(COMMITTED_LOAD_SUCCESSFUL_FIRST_LAYOUT, 1, true);
  CheckTotalEvents();
}

TEST_F(MetricsWebContentsObserverTest, BadIPC) {
  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(10);
  PageLoadTiming timing2;
  timing2.navigation_start = base::Time::FromDoubleT(100);

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      main_rfh());
  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing2),
      main_rfh());

  CheckProvisionalEvent(PROVISIONAL_LOAD_COMMITTED, 1, false);
  CheckCommittedEvent(COMMITTED_LOAD_STARTED, 1, false);
  CheckErrorEvent(ERR_BAD_TIMING_IPC, 1);
  CheckTotalEvents();
}

TEST_F(MetricsWebContentsObserverTest, ObservePartialNavigation) {
  // Delete the observer for this test, add it once the navigation has started.
  observer_.reset();
  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(10);
  timing.first_layout = base::TimeDelta::FromSeconds(2);

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  content::RenderFrameHostTester* rfh_tester =
      content::RenderFrameHostTester::For(main_rfh());

  // Start the navigation, then start observing the web contents. This used to
  // crash us. Make sure we bail out and don't log histograms.
  web_contents_tester->StartNavigation(GURL(kDefaultTestUrl));
  AttachObserver();
  rfh_tester->SimulateNavigationCommit(GURL(kDefaultTestUrl));

  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      main_rfh());
  // Navigate again to force histogram logging.
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl2));
  AssertNoHistogramsLogged();
}

TEST_F(MetricsWebContentsObserverTest, NoRappor) {
  rappor::TestSample::Shadow* sample_obj =
      embedder_interface_->GetRapporService()->GetRecordedSampleForMetric(
          kRapporMetricsNameCoarseTiming);
  EXPECT_EQ(sample_obj, nullptr);
}

TEST_F(MetricsWebContentsObserverTest, RapporLongPageLoad) {
  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(1);
  timing.first_image_paint = base::TimeDelta::FromSeconds(40);

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      main_rfh());

  // Navigate again to force logging RAPPOR.
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl2));
  rappor::TestSample::Shadow* sample_obj =
      embedder_interface_->GetRapporService()->GetRecordedSampleForMetric(
          kRapporMetricsNameCoarseTiming);
  const auto& string_it = sample_obj->string_fields.find("Domain");
  EXPECT_NE(string_it, sample_obj->string_fields.end());
  EXPECT_EQ(rappor::GetDomainAndRegistrySampleFromGURL(GURL(kDefaultTestUrl)),
            string_it->second);

  const auto& flag_it = sample_obj->flag_fields.find("IsSlow");
  EXPECT_NE(flag_it, sample_obj->flag_fields.end());
  EXPECT_EQ(1u, flag_it->second);
}

TEST_F(MetricsWebContentsObserverTest, RapporQuickPageLoad) {
  PageLoadTiming timing;
  timing.navigation_start = base::Time::FromDoubleT(1);
  timing.first_text_paint = base::TimeDelta::FromSeconds(1);

  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl));

  observer_->OnMessageReceived(
      PageLoadMetricsMsg_TimingUpdated(observer_->routing_id(), timing),
      main_rfh());

  // Navigate again to force logging RAPPOR.
  web_contents_tester->NavigateAndCommit(GURL(kDefaultTestUrl2));
  rappor::TestSample::Shadow* sample_obj =
      embedder_interface_->GetRapporService()->GetRecordedSampleForMetric(
          kRapporMetricsNameCoarseTiming);
  const auto& string_it = sample_obj->string_fields.find("Domain");
  EXPECT_NE(string_it, sample_obj->string_fields.end());
  EXPECT_EQ(rappor::GetDomainAndRegistrySampleFromGURL(GURL(kDefaultTestUrl)),
            string_it->second);

  const auto& flag_it = sample_obj->flag_fields.find("IsSlow");
  EXPECT_NE(flag_it, sample_obj->flag_fields.end());
  EXPECT_EQ(0u, flag_it->second);
}

}  // namespace page_load_metrics
