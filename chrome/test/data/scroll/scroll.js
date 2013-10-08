// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Inject this script on any page to measure framerate as the page is scrolled
// from top to bottom.
//
// Usage:
//   1. Define a callback that takes the results array as a parameter.
//   2. To start the test, call new __ScrollTest(callback).
//   3a. When the test is complete, the callback will be called.
//   3b. If no callback is specified, the results are sent to the console.

(function() {
  var getTimeMs = (function() {
    if (window.performance)
      return (performance.now       ||
              performance.mozNow    ||
              performance.msNow     ||
              performance.oNow      ||
              performance.webkitNow).bind(window.performance);
    else
      return function() { return new Date().getTime(); };
  })();

  var requestAnimationFrame = (function() {
    return window.requestAnimationFrame       ||
           window.webkitRequestAnimationFrame ||
           window.mozRequestAnimationFrame    ||
           window.oRequestAnimationFrame      ||
           window.msRequestAnimationFrame     ||
           function(callback) {
             window.setTimeout(callback, 1000 / 60);
           };
  })().bind(window);

  /**
   * Scrolls a given element down a certain amount to emulate user scrolling.
   * Uses smooth scrolling capabilities provided by the platform, if available.
   * @constructor
   */
  function SmoothScrollDownGesture(opt_element, opt_isGmailTest) {
    this.element_ = opt_element || document.body;
    this.isGmailTest_ = opt_isGmailTest;
  };

  SmoothScrollDownGesture.prototype.start = function(callback) {
    this.callback_ = callback;
    if (chrome &&
        chrome.gpuBenchmarking &&
        chrome.gpuBenchmarking.beginSmoothScrollDown) {
      chrome.gpuBenchmarking.beginSmoothScrollDown(true, function() {
        callback();
      });
      return;
    }

    if (this.isGmailTest_) {
      this.element_.scrollByLines(1);
      requestAnimationFrame(callback);
      return;
    }

    var SCROLL_DELTA = 100;
    this.element_.scrollTop += SCROLL_DELTA;
    requestAnimationFrame(callback);
  }

  /**
   * Tracks rendering performance using the gpuBenchmarking.renderingStats API.
   * @constructor
   */
  function GpuBenchmarkingRenderingStats() {
  }

  GpuBenchmarkingRenderingStats.prototype.start = function() {
    this.initialStats_ = this.getRenderingStats_();
  }
  GpuBenchmarkingRenderingStats.prototype.stop = function() {
    this.finalStats_ = this.getRenderingStats_();
  }

  GpuBenchmarkingRenderingStats.prototype.getResult = function() {
    if (!this.initialStats_)
      throw new Error("Start not called.");

    if (!this.finalStats_)
      throw new Error("Stop was not called.");

    var stats = this.finalStats_;
    for (var key in stats)
      stats[key] -= this.initialStats_[key];
    return stats;
  };

  GpuBenchmarkingRenderingStats.prototype.getRenderingStats_ = function() {
    var stats = chrome.gpuBenchmarking.renderingStats();
    stats.totalTimeInSeconds = getTimeMs() / 1000;
    return stats;
  };

  /**
   * Tracks rendering performance using requestAnimationFrame.
   * @constructor
   */
  function RafRenderingStats() {
    this.recording_ = false;
    this.frameTimes_ = [];
  }

  RafRenderingStats.prototype.start = function() {
    if (this.recording_)
      throw new Error("Already started.");
    this.recording_ = true;
    requestAnimationFrame(this.recordFrameTime_.bind(this));
  }

  RafRenderingStats.prototype.stop = function() {
    this.recording_ = false;
  }

  RafRenderingStats.prototype.getResult = function() {
    var result = {};
    result.numAnimationFrames = this.frameTimes_.length - 1;
    result.droppedFrameCount = this.getDroppedFrameCount_(this.frameTimes_);
    result.totalTimeInSeconds = (this.frameTimes_[this.frameTimes_.length - 1] -
        this.frameTimes_[0]) / 1000;
    return result;
  };

  RafRenderingStats.prototype.recordFrameTime_ = function(timestamp) {
    if (!this.recording_)
      return;

    this.frameTimes_.push(timestamp);
    requestAnimationFrame(this.recordFrameTime_.bind(this));
  };

  RafRenderingStats.prototype.getDroppedFrameCount_ = function(frameTimes) {
    var droppedFrameCount = 0;
    for (var i = 1; i < frameTimes.length; i++) {
      var frameTime = frameTimes[i] - frameTimes[i-1];
      if (frameTime > 1000 / 55)
        droppedFrameCount++;
    }
    return droppedFrameCount;
  };

  // This class scrolls a page from the top to the bottom a given number of
  // times.
  //
  // Each full transit of the page is called a "pass."
  //
  // The page is scrolled down by a set of scroll gestures. These gestures
  // correspond to a reading gesture on that platform.
  //
  // i.e. for TOTAL_ITERATIONS_ = 2, we do
  // start_ -> startPass_ -> ...scrolling... -> onGestureComplete_ ->
  //        -> startPass_ -> .. scrolling... -> onGestureComplete_ -> callback_
  //
  // TODO(nduca): This test starts in its constructor. That is strange. We
  // should change it to start explicitly.
  function ScrollTest(opt_callback, opt_isGmailTest) {
    var self = this;

    this.TOTAL_ITERATIONS_ = 2;

    this.callback_ = opt_callback;
    this.isGmailTest_ = opt_isGmailTest;
    this.iteration_ = 0;

    this.results_ = []

    if (this.isGmailTest_) {
      gmonkey.load('2.0', function(api) {
          self.start_(api.getScrollableElement());
      });
    } else {
      if (document.readyState == 'complete')
        this.start_();
      else
        window.addEventListener('load', function() { self.start_(); });
    }
  }

  ScrollTest.prototype.start_ = function(opt_element) {
    // Assign this.element_ here instead of constructor, because the constructor
    // ensures this method will be called after the document is loaded.
    this.element_ = opt_element || document.body;
    requestAnimationFrame(this.startPass_.bind(this));
  };

  ScrollTest.prototype.startPass_ = function() {
    this.element_.scrollTop = 0;
    if (window.chrome && chrome.gpuBenchmarking)
      this.renderingStats_ = new GpuBenchmarkingRenderingStats();
    else
      this.renderingStats_ = new RafRenderingStats();
    this.renderingStats_.start();

    this.gesture_ = new SmoothScrollDownGesture(this.element_,
                                                this.isGmailTest_);
    this.gesture_.start(this.onGestureComplete_.bind(this));
  };

  ScrollTest.prototype.onGestureComplete_ = function(timestamp) {
    // clientHeight is "special" for the body element.
    var clientHeight;
    if (this.element_ == document.body)
      clientHeight = window.innerHeight;
    else
      clientHeight = this.element_.clientHeight;

    var isPassComplete =
        this.element_.scrollTop + clientHeight >= this.element_.scrollHeight;

    if (!isPassComplete) {
      this.gesture_.start(this.onGestureComplete_.bind(this));
      return;
    }

    this.endPass_();

    var isTestComplete = this.iteration_ >= this.TOTAL_ITERATIONS_;
    if (!isTestComplete) {
      this.startPass_();
      return;
    }

    // Send results.
    if (this.callback_)
      this.callback_(this.results_);
    else
      console.log(this.results_);
  };

  ScrollTest.prototype.endPass_ = function() {
    this.renderingStats_.stop();
    this.results_.push(this.renderingStats_.getResult());
    this.iteration_++;
  };


  window.__ScrollTest = ScrollTest;
})();
