// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/chromoting_param_traits.h"

#include "base/strings/stringprintf.h"

namespace IPC {

// static
void ParamTraits<webrtc::DesktopVector>::Write(Message* m,
                                               const webrtc::DesktopVector& p) {
  m->WriteInt(p.x());
  m->WriteInt(p.y());
}

// static
bool ParamTraits<webrtc::DesktopVector>::Read(const Message* m,
                                              PickleIterator* iter,
                                              webrtc::DesktopVector* r) {
  int x, y;
  if (!m->ReadInt(iter, &x) || !m->ReadInt(iter, &y))
    return false;
  *r = webrtc::DesktopVector(x, y);
  return true;
}

// static
void ParamTraits<webrtc::DesktopVector>::Log(const webrtc::DesktopVector& p,
                                             std::string* l) {
  l->append(base::StringPrintf("webrtc::DesktopVector(%d, %d)",
                               p.x(), p.y()));
}

// static
void ParamTraits<webrtc::DesktopSize>::Write(Message* m,
                                             const webrtc::DesktopSize& p) {
  m->WriteInt(p.width());
  m->WriteInt(p.height());
}

// static
bool ParamTraits<webrtc::DesktopSize>::Read(const Message* m,
                                            PickleIterator* iter,
                                            webrtc::DesktopSize* r) {
  int width, height;
  if (!m->ReadInt(iter, &width) || !m->ReadInt(iter, &height))
    return false;
  *r = webrtc::DesktopSize(width, height);
  return true;
}

// static
void ParamTraits<webrtc::DesktopSize>::Log(const webrtc::DesktopSize& p,
                                           std::string* l) {
  l->append(base::StringPrintf("webrtc::DesktopSize(%d, %d)",
                               p.width(), p.height()));
}

// static
void ParamTraits<webrtc::DesktopRect>::Write(Message* m,
                                             const webrtc::DesktopRect& p) {
  m->WriteInt(p.left());
  m->WriteInt(p.top());
  m->WriteInt(p.right());
  m->WriteInt(p.bottom());
}

// static
bool ParamTraits<webrtc::DesktopRect>::Read(const Message* m,
                                            PickleIterator* iter,
                                            webrtc::DesktopRect* r) {
  int left, right, top, bottom;
  if (!m->ReadInt(iter, &left) || !m->ReadInt(iter, &top) ||
      !m->ReadInt(iter, &right) || !m->ReadInt(iter, &bottom)) {
    return false;
  }
  *r = webrtc::DesktopRect::MakeLTRB(left, top, right, bottom);
  return true;
}

// static
void ParamTraits<webrtc::DesktopRect>::Log(const webrtc::DesktopRect& p,
                                           std::string* l) {
  l->append(base::StringPrintf("webrtc::DesktopRect(%d, %d, %d, %d)",
                               p.left(), p.top(), p.right(), p.bottom()));
}

// static
void ParamTraits<remoting::ScreenResolution>::Write(
    Message* m,
    const remoting::ScreenResolution& p) {
  ParamTraits<webrtc::DesktopSize>::Write(m, p.dimensions());
  ParamTraits<webrtc::DesktopVector>::Write(m, p.dpi());
}

// static
bool ParamTraits<remoting::ScreenResolution>::Read(
    const Message* m,
    PickleIterator* iter,
    remoting::ScreenResolution* r) {
  webrtc::DesktopSize size;
  webrtc::DesktopVector dpi;
  if (!ParamTraits<webrtc::DesktopSize>::Read(m, iter, &size) ||
      !ParamTraits<webrtc::DesktopVector>::Read(m, iter, &dpi)) {
    return false;
  }
  if (size.width() < 0 || size.height() < 0 ||
      dpi.x() < 0 || dpi.y() < 0) {
    return false;
  }
  *r = remoting::ScreenResolution(size, dpi);
  return true;
}

// static
void ParamTraits<remoting::ScreenResolution>::Log(
    const remoting::ScreenResolution& p,
    std::string* l) {
  l->append(base::StringPrintf("webrtc::ScreenResolution(%d, %d, %d, %d)",
                               p.dimensions().width(), p.dimensions().height(),
                               p.dpi().x(), p.dpi().y()));
}

}  // namespace IPC

