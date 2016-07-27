// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_spdy_server_stream.h"

#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"
#include "net/quic/quic_flags.h"
#include "net/quic/quic_spdy_session.h"
#include "net/quic/quic_spdy_stream.h"
#include "net/quic/spdy_utils.h"
#include "net/spdy/spdy_protocol.h"
#include "net/tools/quic/quic_in_memory_cache.h"

using base::StringPiece;
using base::StringToInt;
using std::string;

namespace net {
namespace tools {

QuicSpdyServerStream::QuicSpdyServerStream(QuicStreamId id,
                                           QuicSpdySession* session)
    : QuicSpdyStream(id, session), content_length_(-1) {}

QuicSpdyServerStream::~QuicSpdyServerStream() {
}

void QuicSpdyServerStream::OnStreamHeadersComplete(bool fin, size_t frame_len) {
  QuicSpdyStream::OnStreamHeadersComplete(fin, frame_len);
  if (!ParseRequestHeaders(decompressed_headers().data(),
                           decompressed_headers().length())) {
    DVLOG(1) << "Invalid headers";
    SendErrorResponse();
  }
  MarkHeadersConsumed(decompressed_headers().length());
}

void QuicSpdyServerStream::OnDataAvailable() {
  while (HasBytesToRead()) {
    struct iovec iov;
    if (GetReadableRegions(&iov, 1) == 0) {
      // No more data to read.
      break;
    }
    DVLOG(1) << "Processed " << iov.iov_len << " bytes for stream " << id();
    body_.append(static_cast<char*>(iov.iov_base), iov.iov_len);

    if (content_length_ >= 0 &&
        static_cast<int>(body_.size()) > content_length_) {
      DVLOG(1) << "Body size (" << body_.size() << ") > content length ("
               << content_length_ << ").";
      SendErrorResponse();
      return;
    }
    MarkConsumed(iov.iov_len);
  }
  if (!sequencer()->IsClosed()) {
    sequencer()->SetUnblocked();
    return;
  }

  // If the sequencer is closed, then all the body, including the fin, has been
  // consumed.
  OnFinRead();

  if (write_side_closed() || fin_buffered()) {
    return;
  }

  if (request_headers_.empty()) {
    DVLOG(1) << "Request headers empty.";
    SendErrorResponse();
    return;
  }

  if (content_length_ > 0 &&
      content_length_ != static_cast<int>(body_.size())) {
    DVLOG(1) << "Content length (" << content_length_ << ") != body size ("
             << body_.size() << ").";
    SendErrorResponse();
    return;
  }

  SendResponse();
}

bool QuicSpdyServerStream::ParseRequestHeaders(const char* data,
                                               uint32 data_len) {
  DCHECK(headers_decompressed());
  SpdyFramer framer(HTTP2);
  if (!framer.ParseHeaderBlockInBuffer(data, data_len, &request_headers_) ||
      request_headers_.empty()) {
    return false;  // Headers were invalid.
  }

  if (ContainsKey(request_headers_, "content-length")) {
    string delimiter;
    delimiter.push_back('\0');
    std::vector<string> values =
        base::SplitString(request_headers_["content-length"], delimiter,
                          base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);

    for (const string& value : values) {
      int new_value;
      if (!StringToInt(value, &new_value) || new_value < 0) {
        return false;
      }
      if (content_length_ < 0) {
        content_length_ = new_value;
        continue;
      }
      if (new_value != content_length_) {
        return false;
      }
    }
  }

  return true;
}

void QuicSpdyServerStream::SendResponse() {
  if (!ContainsKey(request_headers_, ":authority") ||
      !ContainsKey(request_headers_, ":path")) {
    DVLOG(1) << "Request headers do not contain :authority or :path.";
    SendErrorResponse();
    return;
  }

  // Find response in cache. If not found, send error response.
  const QuicInMemoryCache::Response* response =
      QuicInMemoryCache::GetInstance()->GetResponse(
          request_headers_[":authority"], request_headers_[":path"]);
  if (response == nullptr) {
    DVLOG(1) << "Response not found in cache.";
    SendErrorResponse();
    return;
  }

  if (response->response_type() == QuicInMemoryCache::CLOSE_CONNECTION) {
    DVLOG(1) << "Special response: closing connection.";
    CloseConnection(QUIC_NO_ERROR);
    return;
  }

  if (response->response_type() == QuicInMemoryCache::IGNORE_REQUEST) {
    DVLOG(1) << "Special response: ignoring request.";
    return;
  }

  // Examing response status, if it was not pure integer as typical h2 response
  // status, send error response.
  string request_url = request_headers_[":scheme"].as_string() + "://" +
                       request_headers_[":authority"].as_string() +
                       request_headers_[":path"].as_string();
  int response_code;
  SpdyHeaderBlock response_headers = response->headers();
  if (!base::StringToInt(response_headers[":status"], &response_code)) {
    DVLOG(1) << "Illegal (non-integer) response :status from cache: "
             << response_headers[":status"].as_string() << " for request "
             << request_url;
    SendErrorResponse();
    return;
  }

  if (id() % 2 == 0) {
    // A server initiated stream is only used for a server push response,
    // and only 200 and 30X response codes are supported for server push.
    // This behavior mirrors the HTTP/2 implementation.
    bool is_redirection = response_code / 100 == 3;
    if (response_code != 200 && !is_redirection) {
      LOG(WARNING) << "Response to server push request " << request_url
                   << " result in response code " << response_code;
      Reset(QUIC_STREAM_CANCELLED);
      return;
    }
  }
  DVLOG(1) << "Sending response for stream " << id();
  SendHeadersAndBody(response->headers(), response->body());
}

void QuicSpdyServerStream::SendErrorResponse() {
  DVLOG(1) << "Sending error response for stream " << id();
  SpdyHeaderBlock headers;
  headers[":status"] = "500";
  headers["content-length"] = base::UintToString(strlen(kErrorResponseBody));
  SendHeadersAndBody(headers, kErrorResponseBody);
}

void QuicSpdyServerStream::SendHeadersAndBody(
    const SpdyHeaderBlock& response_headers,
    StringPiece body) {
  // This server only supports SPDY and HTTP, and neither handles bidirectional
  // streaming.
  if (!reading_stopped()) {
    StopReading();
  }

  WriteHeaders(response_headers, body.empty(), nullptr);

  if (!body.empty()) {
    WriteOrBufferData(body, true, nullptr);
  }
}

const char* const QuicSpdyServerStream::kErrorResponseBody = "bad";

}  // namespace tools
}  // namespace net
