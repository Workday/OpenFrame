// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file mocks out just enough of the WebClipboard API for running the
// webkit tests. This is so we can run webkit tests without them sharing a
// clipboard, which allows for running them in parallel and having the tests
// not interact with actual user actions.

#ifndef WEBKIT_SUPPORT_MOCK_WEBCLIPBOARD_IMPL_H_
#define WEBKIT_SUPPORT_MOCK_WEBCLIPBOARD_IMPL_H_

#include <map>

#include "base/strings/string16.h"
#include "third_party/WebKit/public/platform/WebClipboard.h"
#include "third_party/WebKit/public/platform/WebDragData.h"
#include "third_party/WebKit/public/platform/WebImage.h"

class MockWebClipboardImpl : public WebKit::WebClipboard {
 public:
  MockWebClipboardImpl();
  virtual ~MockWebClipboardImpl();

  virtual bool isFormatAvailable(WebKit::WebClipboard::Format format,
                                 WebKit::WebClipboard::Buffer buffer);
  virtual WebKit::WebVector<WebKit::WebString> readAvailableTypes(
      WebKit::WebClipboard::Buffer buffer, bool* containsFilenames);

  virtual WebKit::WebString readPlainText(WebKit::WebClipboard::Buffer buffer);
  virtual WebKit::WebString readHTML(WebKit::WebClipboard::Buffer buffer,
                                     WebKit::WebURL* url,
                                     unsigned* fragmentStart,
                                     unsigned* fragmentEnd);
  virtual WebKit::WebData readImage(WebKit::WebClipboard::Buffer buffer);
  virtual WebKit::WebString readCustomData(WebKit::WebClipboard::Buffer buffer,
                                           const WebKit::WebString& type);

  virtual void writePlainText(const WebKit::WebString& plain_text);
  virtual void writeHTML(
      const WebKit::WebString& htmlText, const WebKit::WebURL& url,
      const WebKit::WebString& plainText, bool writeSmartPaste);
  virtual void writeURL(
      const WebKit::WebURL& url, const WebKit::WebString& title);
  virtual void writeImage(
      const WebKit::WebImage& image, const WebKit::WebURL& url,
      const WebKit::WebString& title);
  virtual void writeDataObject(const WebKit::WebDragData& data);

 private:
  void clear();

  WebKit::WebString m_plainText;
  WebKit::WebString m_htmlText;
  WebKit::WebImage m_image;
  std::map<base::string16, base::string16> m_customData;
  bool m_writeSmartPaste;
};

#endif  // WEBKIT_SUPPORT_MOCK_WEBCLIPBOARD_IMPL_H_
