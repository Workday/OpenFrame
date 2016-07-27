var StereoPannerTest = (function () {

  // Constants
  var PI_OVER_TWO = Math.PI * 0.5;

  var gSampleRate = 44100;

  // Time step when each panner node starts.
  var gTimeStep = 0.001;

  // How many panner nodes to create for the test
  var gNodesToCreate = 100;

  // Total render length for all of our nodes.
  var gRenderLength = gTimeStep * (gNodesToCreate + 1) + gSampleRate;

  // Calculates channel gains based on equal power panning model.
  // See: http://webaudio.github.io/web-audio-api/#panning-algorithm
  function getChannelGain(pan, numberOfChannels) {
    // The internal panning clips the pan value between -1, 1.
    pan = Math.min(Math.max(pan, -1), 1);
    var gainL, gainR;
    // Consider number of channels and pan value's polarity.
    if (numberOfChannels == 1) {
      var panRadian = (pan * 0.5 + 0.5) * PI_OVER_TWO;
      gainL = Math.cos(panRadian);
      gainR = Math.sin(panRadian);
    } else {
      var panRadian = (pan <= 0 ? pan + 1 : pan) * PI_OVER_TWO;
      if (pan <= 0) {
        gainL = 1 + Math.cos(panRadian);
        gainR = Math.sin(panRadian);
      } else {
        gainL = Math.cos(panRadian);
        gainR = 1 + Math.sin(panRadian);
      }
    }
    return {
      gainL: gainL,
      gainR: gainR
    };
  }


  /**
   * Test implementation class.
   * @param {Object} options Test options
   * @param {Object} options.description Test description
   * @param {Object} options.numberOfInputChannels Number of input channels
   */
  function Test(options) {

    // Primary test flag.
    this.success = true;

    this.context = null;
    this.numberOfInputChannels = (options.numberOfInputChannels || 1);
    switch (this.numberOfInputChannels) {
      case 1:
        this.description = 'Test for mono input';
        break;
      case 2:
        this.description = 'Test for stereo input';
        break;
    }

    // Onset time position of each impulse.
    this.onsets = [];

    // Pan position value of each impulse.
    this.panPositions = [];

    // Locations of where the impulses aren't at the expected locations.
    this.errors = [];

    // The index of the current impulse being verified.
    this.impulseIndex = 0;

    // The max error we allow between the rendered impulse and the
    // expected value.  This value is experimentally determined.  Set
    // to 0 to make the test fail to see what the actual error is.
    this.maxAllowedError = 1.3e-6;

    // Max (absolute) error and the index of the maxima for the left
    // and right channels.
    this.maxErrorL = 0;
    this.maxErrorR = 0;
    this.maxErrorIndexL = 0;
    this.maxErrorIndexR = 0;

  }


  Test.prototype.init = function () {
    this.context = new OfflineAudioContext(2, gRenderLength, gSampleRate);
  };

  // Prepare an audio graph for testing. Create multiple impulse generators and
  // panner nodes, then play them sequentially while varying the pan position.
  Test.prototype.prepare = function () {
    var impulse;
    var impulseLength = Math.round(gTimeStep * gSampleRate);
    var sources = [];
    var panners = [];

    // Moves the pan value for each panner by pan step unit from -2 to 2.
    // This is to check if the internal panning value is clipped properly.
    var panStep = 4 / (gNodesToCreate - 1);

    if (this.numberOfInputChannels === 1) {
      impulse = createImpulseBuffer(this.context, impulseLength);
    } else {
      impulse = createStereoImpulseBuffer(this.context, impulseLength);
    }

    for (var i = 0; i < gNodesToCreate; i++) {
      sources[i] = this.context.createBufferSource();
      panners[i] = this.context.createStereoPanner();
      sources[i].connect(panners[i]);
      panners[i].connect(this.context.destination);
      sources[i].buffer = impulse;
      panners[i].pan.value = this.panPositions[i] = panStep * i - 2;

      // Store the onset time position of impulse.
      this.onsets[i] = gTimeStep * i;

      sources[i].start(this.onsets[i]);
    }
  };


  Test.prototype.verify = function () {
    var chanL = this.renderedBufferL;
    var chanR = this.renderedBufferR;
    for (var i = 0; i < chanL.length; i++) {
      // Left and right channels must start at the same instant.
      if (chanL[i] !== 0 || chanR[i] !== 0) {

        // Get amount of error between actual and expected gain.
        var expected = getChannelGain(
          this.panPositions[this.impulseIndex],
          this.numberOfInputChannels
        );
        var errorL = Math.abs(chanL[i] - expected.gainL);
        var errorR = Math.abs(chanR[i] - expected.gainR);

        if (errorL > this.maxErrorL) {
          this.maxErrorL = errorL;
          this.maxErrorIndexL = this.impulseIndex;
        }
        if (errorR > this.maxErrorR) {
          this.maxErrorR = errorR;
          this.maxErrorIndexR = this.impulseIndex;
        }

        // Keep track of the impulses that didn't show up where we expected
        // them to be.
        var expectedOffset = timeToSampleFrame(
          this.onsets[this.impulseIndex],
          gSampleRate
        );
        if (i != expectedOffset) {
          this.errors.push({
            actual: i,
            expected: expectedOffset
          });
        }

        this.impulseIndex++;
      }
    }
  };


  Test.prototype.showResult = function () {
    if (this.impulseIndex === gNodesToCreate) {
      testPassed('Number of impulses matches the number of panner nodes.');
    } else {
      testFailed('Number of impulses is incorrect. (Found '
        + this.impulseIndex
        + ' but expected '
        + gNodesToCreate
        + ')'
      );
      this.success = false;
    }

    if (this.errors.length === 0) {
      testPassed('All impulses at expected offsets.');
    } else {
      testFailed(this.errors.length + ' timing errors found in '
        + this.nodesToCreate + ' panner nodes.'
      );
      for (var i = 0; i < this.errors.length; i++) {
        testFailed('Impulse at sample ' + this.errors[i].actual
          + ' but expected ' + this.errors[i].expected
        );
      }
      this.success = false;
    }

    if (this.maxErrorL <= this.maxAllowedError) {
      testPassed('Left channel gain values are correct.');
    } else {
      testFailed('Left channel gain values are incorrect.  Max error = '
        + this.maxErrorL + ' at time ' + this.onsets[this.maxErrorIndexL]
        + ' (threshold = ' + this.maxAllowedError + ')'
      );
      this.success = false;
    }

    if (this.maxErrorR <= this.maxAllowedError) {
      testPassed('Right channel gain values are correct.');
    } else {
      testFailed('Right channel gain values are incorrect.  Max error = '
        + this.maxErrorR + ' at time ' + this.onsets[this.maxErrorIndexR]
        + ' (threshold = ' + this.maxAllowedError + ')'
      );
      this.success = false;
    }
  };


  Test.prototype.finish = function () {
    if (this.success)
      testPassed(this.description + ': passed.');
    else
      testFailed(this.description + ': failed.');
  };


  Test.prototype.run = function (done) {

    this.init();
    this.prepare();
    this.context.oncomplete = function (event) {
      this.renderedBufferL = event.renderedBuffer.getChannelData(0);
      this.renderedBufferR = event.renderedBuffer.getChannelData(1);
      this.verify();
      this.showResult();
      this.finish();
      done();
    }.bind(this);
    this.context.startRendering();

  };


  return {
    create: function (options) {
      return new Test(options);
    }
  };

})();

