description("Tests that when Geolocation permission has been denied prior to a call to watchPosition, and the watch is cleared in the error callback, there is no crash. This a regression test for https://bugs.webkit.org/show_bug.cgi?id=32111.");

if (!window.testRunner || !window.internals)
    debug('This test can not run without testRunner or internals');

internals.setGeolocationClientMock(document);

// Prime the Geolocation instance by denying permission.
internals.setGeolocationPermission(document, false);
internals.setGeolocationPosition(document, 51.478, -0.166, 100);

var error;
navigator.geolocation.getCurrentPosition(function(p) {
    testFailed('Success callback invoked unexpectedly');
    finishJSTest();
}, function(e) {
    error = e;
    shouldBe('error.code', 'error.PERMISSION_DENIED');
    shouldBe('error.message', '"User denied Geolocation"');
    debug('');
    continueTest();
});

function continueTest()
{
    // Make another request, with permission already denied.
    var watchId = navigator.geolocation.watchPosition(function(p) {
        testFailed('Success callback invoked unexpectedly');
        finishJSTest();
    }, function(e) {
        error = e;
        shouldBe('error.code', 'error.PERMISSION_DENIED');
        shouldBe('error.message', '"User denied Geolocation"');
        navigator.geolocation.clearWatch(watchId);
        window.setTimeout(finishJSTest, 0);
    });
}

window.jsTestIsAsync = true;
