/*
 * Copyright (C) 2012 Samsung Electronics. All rights reserved.
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

var initialize_InspectorTest = function() {

InspectorTest.evaluateInInspectedPage = function(expression, callback)
{
    InspectorTest.sendCommand("Runtime.evaluate", { expression: expression }, callback);
}

}

var outputElement;

/**
 * Logs message to process stdout via alert (hopefully implemented with immediate flush).
 * @param {string} text
 */
function debugLog(text)
{
    alert(text);
}

/**
 * @param {string} text
 */
function log(text)
{
    if (!outputElement) {
        var intermediate = document.createElement("div");
        document.body.appendChild(intermediate);

        var intermediate2 = document.createElement("div");
        intermediate.appendChild(intermediate2);

        outputElement = document.createElement("div");
        outputElement.className = "output";
        outputElement.id = "output";
        outputElement.style.whiteSpace = "pre";
        intermediate2.appendChild(outputElement);
    }
    outputElement.appendChild(document.createTextNode(text));
    outputElement.appendChild(document.createElement("br"));
}

function closeTest()
{
    closeInspector();
    testRunner.notifyDone();
}

function runTest()
{
    if (!window.testRunner) {
        console.error("This test requires DumpRenderTree");
        return;
    }
    testRunner.dumpAsText();
    testRunner.waitUntilDone();
    testRunner.setCanOpenWindows(true);

    openInspector();
}

function closeInspector()
{
    testRunner.closeWebInspector();
}

var lastFrontendEvalId = 0;
function evaluateInFrontend(script)
{
    testRunner.evaluateInWebInspector(++lastFrontendEvalId, script);
}

function openInspector()
{
    var scriptTags = document.getElementsByTagName("script");
    var scriptUrlBasePath = "";
    for (var i = 0; i < scriptTags.length; ++i) {
        var index = scriptTags[i].src.lastIndexOf("/inspector-protocol-test.js");
        if (index > -1 ) {
            scriptUrlBasePath = scriptTags[i].src.slice(0, index);
            break;
        }
    }

    var dummyFrontendURL = scriptUrlBasePath + "/resources/protocol-test.html";
    testRunner.showWebInspector("", dummyFrontendURL);
    // FIXME: rename this 'test' global field across all tests.
    var testFunction = window.test;
    if (typeof testFunction === "function") {
        var initializers = "";
        for (var symbol in window) {
            if (!/^initialize_/.test(symbol) || typeof window[symbol] !== "function")
                continue;
            initializers += "(" + window[symbol].toString() + ")();\n";
        }
        evaluateInFrontend(initializers + "(" + testFunction.toString() +")();");
        return;
    }
    // Kill waiting process if failed to send.
    alert("Failed to send test function");
    testRunner.notifyDone();
}
