// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Global variable.
var gl_context;

initializeWebGL = function(canvas) {
  gl_context = null;
  // Try to grab the standard context.
  gl_context = canvas.getContext("webgl") ||
               canvas.getContext("experimental-webgl");
  // If we don't have a GL context, give up now
  if (!gl_context) {
    alert("Unable to initialize WebGL. Your browser may not support it.");
  }
}

startWebGLContext = function() {
  var canvas = document.getElementById("glcanvas");
  // Initialize the GL context.
  initializeWebGL(canvas);

  // Only continue if WebGL is available and working.
  if (gl_context) {
    gl_context.clearColor(0.0, 0.0, 0.0, 1.0);
    gl_context.enable(gl_context.DEPTH_TEST);
    gl_context.depthFunc(gl_context.LEQUAL);
    gl_context.clearDepth(1);
    gl_context.clear(gl_context.COLOR_BUFFER_BIT |
                     gl_context.DEPTH_BUFFER_BIT);
  }
}