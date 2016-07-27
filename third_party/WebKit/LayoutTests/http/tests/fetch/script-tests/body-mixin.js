if (self.importScripts) {
  importScripts('../resources/fetch-test-helpers.js');
}

function readStream(reader, values) {
  reader.read().then(function(r) {
      if (!r.done) {
        values.push(r.value);
        readStream(reader, values);
      }
    });
  return reader.closed;
}

function isLocked(stream) {
  try {
    var reader = stream.getReader();
    reader.releaseLock();
    return false;
  } catch(e) {
    return true;
  }
}

promise_test(function(test) {
    return fetch('/fetch/resources/doctype.html')
      .then(function(response) {
          // Accessing the body property makes the stream start working.
          var stream = response.body;
          return response.text();
        })
      .then(function(text) {
          assert_equals(text, '<!DOCTYPE html>\n');
        })
    }, 'FetchTextAfterAccessingStreamTest');

promise_test(function(test) {
    var chunks = [];
    var actual = '';
    return fetch('/fetch/resources/doctype.html')
      .then(function(response) {
          r = response;
          return readStream(response.body.getReader(), chunks);
        })
      .then(function() {
          var decoder = new TextDecoder();
          for (var chunk of chunks) {
            actual += decoder.decode(chunk, {stream: true});
          }
          // Put an empty buffer without the stream option to end decoding.
          actual += decoder.decode(new Uint8Array(0));
          assert_equals(actual, '<!DOCTYPE html>\n');
        })
    }, 'FetchStreamTest');

promise_test(function(test) {
    return fetch('/fetch/resources/progressive.php')
      .then(function(response) {
          var p1 = response.text();
          // Because progressive.php takes some time to load, we expect
          // response.text() is not yet completed here.
          var p2 = response.text().then(function() {
              return Promise.reject(new Error('resolved unexpectedly'));
            }, function(e) {
              return e;
            });
          return Promise.all([p1, p2]);
        })
      .then(function(results) {
          assert_equals(results[0].length, 190);
          assert_equals(results[1].name, 'TypeError');
        })
    }, 'FetchTwiceTest');

promise_test(function(test) {
    var response;
    return fetch('/fetch/resources/doctype.html')
      .then(function(res) {
          response = res;
          assert_false(response.bodyUsed);
          var p = response.arrayBuffer();
          assert_true(response.bodyUsed);
          assert_true(isLocked(response.body));
          return p;
        })
      .then(function(b) {
          assert_true(isLocked(response.body));
          assert_equals(b.byteLength, 16);
        })
    }, 'ArrayBufferTest');

promise_test(function(test) {
    var response;
    return fetch('/fetch/resources/doctype.html')
      .then(function(res) {
          response = res;
          assert_false(response.bodyUsed);
          var p = response.blob();
          assert_true(response.bodyUsed);
          assert_true(isLocked(response.body));
          return p;
        })
      .then(function(blob) {
          assert_true(isLocked(response.body));
          assert_equals(blob.size, 16);
          assert_equals(blob.type, 'text/html');
        })
    }, 'BlobTest');

promise_test(function(test) {
    var response;
    return fetch('/fetch/resources/doctype.html')
      .then(function(res) {
          response = res;
          assert_false(response.bodyUsed);
          var p = response.json();
          assert_true(response.bodyUsed);
          assert_true(isLocked(response.body));
          return p;
        })
      .then(
        test.unreached_func('json() must fail'),
        function(e) {
          assert_true(isLocked(response.body));
          assert_equals(e.name, 'SyntaxError', 'expected JSON error');
        })
    }, 'JSONFailedTest');

promise_test(function(test) {
    var response;
    return fetch('/fetch/resources/simple.json')
      .then(function(res) {
          response = res;
          assert_false(response.bodyUsed);
          var p = response.json();
          assert_true(response.bodyUsed);
          assert_true(isLocked(response.body));
          return p;
        })
      .then(function(json) {
          assert_true(isLocked(response.body));
          assert_equals(json['a'], 1);
          assert_equals(json['b'], 2);
        })
    }, 'JSONTest');

promise_test(function(test) {
    var response;
    return fetch('/fetch/resources/doctype.html')
      .then(function(res) {
          response = res;
          assert_false(response.bodyUsed);
          var p = response.text();
          assert_true(response.bodyUsed);
          assert_true(isLocked(response.body));
          return p;
        })
      .then(function(text) {
          assert_true(isLocked(response.body));
          assert_equals(text, '<!DOCTYPE html>\n');
        })
    }, 'TextTest');

promise_test(function(test) {
    return fetch('/fetch/resources/non-ascii.txt')
      .then(function(response) {
          assert_false(response.bodyUsed);
          var p = response.text();
          assert_true(response.bodyUsed);
          return p;
        })
      .then(function(text) {
          assert_equals(text, '\u4e2d\u6587 Gem\u00fcse\n');
        })
    }, 'NonAsciiTextTest');

test(t => {
    var req = new Request('/');
    assert_false(req.bodyUsed);
    req.text();
    assert_false(req.bodyUsed);
  }, 'BodyUsedShouldNotBeSetForNullBody');

test(t => {
    var req = new Request('/', {method: 'POST', body: ''});
    assert_false(req.bodyUsed);
    req.text();
    assert_true(req.bodyUsed);
  }, 'BodyUsedShouldBeSetForEmptyBody');

test(t => {
    var res = new Response('');
    assert_false(res.bodyUsed);
    var reader = res.body.getReader();
    assert_false(res.bodyUsed);
    reader.read();
    assert_true(res.bodyUsed);
  }, 'BodyUsedShouldBeSetWhenRead');

test(t => {
    var res = new Response('');
    assert_false(res.bodyUsed);
    var reader = res.body.getReader();
    assert_false(res.bodyUsed);
    reader.cancel();
    assert_true(res.bodyUsed);
  }, 'BodyUsedShouldBeSetWhenCancelled');

promise_test(t => {
    var res = new Response('');
    res.body.cancel();
    return res.arrayBuffer().then(unreached_fulfillment(t), e => {
        assert_equals(e.name, 'TypeError');
      });
  }, 'Used => arrayBuffer');

promise_test(t => {
    var res = new Response('');
    res.body.cancel();
    return res.blob().then(unreached_fulfillment(t), e => {
        assert_equals(e.name, 'TypeError');
      });
  }, 'Used => blob');

promise_test(t => {
    var res = new Response('');
    res.body.cancel();
    return res.json().then(unreached_fulfillment(t), e => {
        assert_equals(e.name, 'TypeError');
      });
  }, 'Used => json');

promise_test(t => {
    var res = new Response('');
    res.body.cancel();
    return res.text().then(unreached_fulfillment(t), e => {
        assert_equals(e.name, 'TypeError');
      });
  }, 'Used => text');

promise_test(t => {
    var res = new Response('');
    res.body.getReader();
    return res.arrayBuffer().then(unreached_fulfillment(t), e => {
        assert_equals(e.name, 'TypeError');
      });
  }, 'Locked => arrayBuffer');

promise_test(t => {
    var res = new Response('');
    res.body.getReader();
    return res.blob().then(unreached_fulfillment(t), e => {
        assert_equals(e.name, 'TypeError');
      });
  }, 'Locked => blob');

promise_test(t => {
    var res = new Response('');
    res.body.getReader();
    return res.json().then(unreached_fulfillment(t), e => {
        assert_equals(e.name, 'TypeError');
      });
  }, 'Locked => json');

promise_test(t => {
    var res = new Response('');
    res.body.getReader();
    return res.text().then(unreached_fulfillment(t), e => {
        assert_equals(e.name, 'TypeError');
      });
  }, 'Locked => text');

done();
