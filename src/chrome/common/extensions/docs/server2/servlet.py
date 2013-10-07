# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

class Request(object):
  '''Request data.
  '''
  def __init__(self, path, host, headers):
    self.path = path.lstrip('/')
    self.host = host.rstrip('/')
    self.headers = headers

  @staticmethod
  def ForTest(path, host='http://developer.chrome.com', headers=None):
    return Request(path, host, headers or {})

  def __repr__(self):
    return 'Request(path=%s, host=%s, headers=%s)' % (
        self.path, self.host, self.headers)

  def __str__(self):
    return repr(self)

class _ContentBuilder(object):
  '''Builds the response content.
  '''
  def __init__(self):
    self._buf = []

  def Append(self, content):
    if isinstance(content, unicode):
      content = content.encode('utf-8', 'replace')
    self._buf.append(content)

  def ToString(self):
    self._Collapse()
    return self._buf[0]

  def __str__(self):
    return self.ToString()

  def __len__(self):
    return len(self.ToString())

  def _Collapse(self):
    self._buf = [''.join(self._buf)]

class Response(object):
  '''The response from Get().
  '''
  def __init__(self, content=None, headers=None, status=None):
    self.content = _ContentBuilder()
    if content is not None:
      self.content.Append(content)
    self.headers = {}
    if headers is not None:
      self.headers.update(headers)
    self.status = status

  @staticmethod
  def Ok(content, headers=None):
    '''Returns an OK (200) response.
    '''
    return Response(content=content, headers=headers, status=200)

  @staticmethod
  def Redirect(url, permanent=False):
    '''Returns a redirect (301 or 302) response.
    '''
    status = 301 if permanent else 302
    return Response(headers={'Location': url}, status=status)

  @staticmethod
  def NotFound(content, headers=None):
    '''Returns a not found (404) response.
    '''
    return Response(content=content, headers=headers, status=404)

  @staticmethod
  def InternalError(content, headers=None):
    '''Returns an internal error (500) response.
    '''
    return Response(content=content, headers=headers, status=500)

  def Append(self, content):
    '''Appends |content| to the response content.
    '''
    self.content.append(content)

  def AddHeader(self, key, value):
    '''Adds a header to the response.
    '''
    self.headers[key] = value

  def AddHeaders(self, headers):
    '''Adds several headers to the response.
    '''
    self.headers.update(headers)

  def SetStatus(self, status):
    self.status = status

  def GetRedirect(self):
    if self.headers.get('Location') is None:
      return (None, None)
    return (self.headers.get('Location'), self.status == 301)

  def __eq__(self, other):
    return (isinstance(other, self.__class__) and
            str(other.content) == str(self.content) and
            other.headers == self.headers and
            other.status == self.status)

  def __ne__(self, other):
    return not (self == other)

  def __repr__(self):
    return 'Response(content=%s bytes, status=%s, headers=%s)' % (
        len(self.content), self.status, self.headers)

  def __str__(self):
    return repr(self)

class Servlet(object):
  def __init__(self, request):
    self._request = request

  def Get(self):
    '''Returns a Response.
    '''
    raise NotImplemented()
