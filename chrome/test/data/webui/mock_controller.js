// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Create a mock function that records function calls and validates against
 * expectations.
 * @constructor.
 */
function MockMethod() {
  var fn = function() {
    var args = Array.prototype.slice.call(arguments);
    fn.recordCall(args);
    return this.returnValue;
  };

  /**
   * List of signatures for fucntion calls.
   * @type {!Array.<!Array>}
   * @private
   */
  fn.calls_ = [];

  /**
   * List of expected call signatures.
   * @type {!Array.<!Array>}
   * @private
   */
  fn.expectations_ = [];

  /**
   * Value returned from call to function.
   * @type {*}
   */
  fn.returnValue = undefined;

  fn.__proto__ = MockMethod.prototype;
  return fn;
}

MockMethod.prototype = {
  /**
   * Adds an expected call signature.
   * @param {...}  var_args Expected arguments for the function call.
   */
  addExpectation: function() {
    var args = Array.prototype.slice.call(arguments);
    this.expectations_.push(args);
  },

  /**
   * Adds a call signature.
   * @param {!Array} args.
   */
  recordCall: function(args) {
    this.calls_.push(args);
  },

  /**
   * Verifies that the function is called the expected number of times and with
   * the correct signature for each call.
   */
  verifyMock: function() {
    assertEquals(this.expectations_.length,
                 this.calls_.length,
                 'Number of method calls did not match expectation.');
    for (var i = 0; i < this.expectations_.length; i++) {
      assertDeepEquals(this.expectations_[i],
                       this.calls_[i]);
    }
  }
};

/**
 * Controller for mocking methods. Tracks calls to mocked methods and verifies
 * that call signatures match expectations.
 * @constructor.
 */
function MockController() {
  /**
   * Original functions implementations, which are restored when |reset| is
   * called.
   * @type {!Array.<!Object>}
   * @private
   */
  this.overrides_ = [];

  /**
   * List of registered mocks.
   * @type {!Array.<!MockMethod>}
   * @private
   */
  this.mocks_ = [];
}

MockController.prototype = {
  /**
   * Creates a mock function.
   * @param {Object=} opt_parent Optional parent object for the function.
   * @param {string=} opt_functionName Optional name of the function being
   *     mocked. If the parent and function name are both provided, the
   *     mock is automatically substituted for the original and replaced on
   *     reset.
   */
  createFunctionMock: function(opt_parent, opt_functionName) {
    var fn = new MockMethod();

    // Register mock.
    if (opt_parent && opt_functionName) {
      this.overrides_.push({
        parent: opt_parent,
        functionName: opt_functionName,
        originalFunction: opt_parent[opt_functionName]
      });
      opt_parent[opt_functionName] = fn;
    }
    this.mocks_.push(fn);

    return fn;
  },

  /**
   * Validates all mocked methods. An exception is thrown if the
   * expected and actual calls to a mocked function to not align.
   */
  verifyMocks: function() {
    for (var i = 0; i < this.mocks_.length; i++) {
      this.mocks_[i].verifyMock();
    }
  },

  /**
   * Discard mocks reestoring default behavior.
   */
  reset: function() {
    for (var i = 0; i < this.overrides_.length; i++) {
      var override = this.overrides_[i];
      override.parent[override.functionName] = override.originalFunction;
    }
  },
};
