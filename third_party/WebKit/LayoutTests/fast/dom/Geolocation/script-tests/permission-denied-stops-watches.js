description("Tests that when Geolocation permission is denied, watches are stopped, as well as one-shots.");

if (!window.testRunner || !window.internals)
    debug('This test can not run without testRunner or internals');

internals.setGeolocationClientMock(document);

// Configure the mock Geolocation service to report a position to cause permission
// to be requested, then deny it.
internals.setGeolocationPermission(document, false);
internals.setGeolocationPosition(document, 51.478, -0.166, 100.0);

var error;
var errorCallbackInvoked = false;
navigator.geolocation.watchPosition(function(p) {
    testFailed('Success callback invoked unexpectedly');
    finishJSTest();
}, function(e) {
    if (errorCallbackInvoked) {
        testFailed('Error callback invoked unexpectedly : ' + error.message);
        finishJSTest();
    }
    errorCallbackInvoked = true;

    error = e;
    shouldBe('error.code', 'error.PERMISSION_DENIED');
    shouldBe('error.message', '"User denied Geolocation"');

    // Update the mock Geolocation service to report a new position, then
    // yield to allow a chance for the success callback to be invoked.
    internals.setGeolocationPosition(document, 55.478, -0.166, 100);
    window.setTimeout(finishJSTest, 0);
});


window.jsTestIsAsync = true;
