// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/fake_connection_to_client.h"

#include "remoting/protocol/session.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capturer.h"

namespace remoting {
namespace protocol {

FakeVideoStream::FakeVideoStream() : weak_factory_(this) {}
FakeVideoStream::~FakeVideoStream() {}

void FakeVideoStream::Pause(bool pause) {}

void FakeVideoStream::OnInputEventReceived(int64_t event_timestamp) {}

void FakeVideoStream::SetLosslessEncode(bool want_lossless) {}

void FakeVideoStream::SetLosslessColor(bool want_lossless) {}

void FakeVideoStream::SetSizeCallback(const SizeCallback& size_callback) {
  size_callback_ = size_callback;
}

base::WeakPtr<FakeVideoStream> FakeVideoStream::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

FakeConnectionToClient::FakeConnectionToClient(scoped_ptr<Session> session)
    : session_(session.Pass()) {}

FakeConnectionToClient::~FakeConnectionToClient() {}

void FakeConnectionToClient::SetEventHandler(EventHandler* event_handler) {
  event_handler_ = event_handler;
}

scoped_ptr<VideoStream> FakeConnectionToClient::StartVideoStream(
    scoped_ptr<webrtc::DesktopCapturer> desktop_capturer) {
  scoped_ptr<FakeVideoStream> result(new FakeVideoStream());
  last_video_stream_ = result->GetWeakPtr();
  return result.Pass();
}

AudioStub* FakeConnectionToClient::audio_stub() {
  return audio_stub_;
}

ClientStub* FakeConnectionToClient::client_stub() {
  return client_stub_;
}

void FakeConnectionToClient::Disconnect(ErrorCode disconnect_error) {
  CHECK(is_connected_);

  is_connected_ = false;
  disconnect_error_ = disconnect_error;
  if (event_handler_)
    event_handler_->OnConnectionClosed(this, disconnect_error_);
}

Session* FakeConnectionToClient::session() {
  return session_.get();
}

void FakeConnectionToClient::OnInputEventReceived(int64_t timestamp) {}

void FakeConnectionToClient::set_clipboard_stub(ClipboardStub* clipboard_stub) {
  clipboard_stub_ = clipboard_stub;
}

void FakeConnectionToClient::set_host_stub(HostStub* host_stub) {
  host_stub_ = host_stub;
}

void FakeConnectionToClient::set_input_stub(InputStub* input_stub) {
  input_stub_ = input_stub;
}

}  // namespace protocol
}  // namespace remoting
