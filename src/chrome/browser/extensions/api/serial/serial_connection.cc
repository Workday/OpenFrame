// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/serial/serial_connection.h"

#include <string>

#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/strings/string_util.h"
#include "chrome/browser/extensions/api/api_resource_manager.h"

namespace extensions {

const char kSerialConnectionNotFoundError[] = "Serial connection not found";

static base::LazyInstance<ProfileKeyedAPIFactory<
    ApiResourceManager<SerialConnection> > >
        g_factory = LAZY_INSTANCE_INITIALIZER;

// static
template <>
ProfileKeyedAPIFactory<ApiResourceManager<SerialConnection> >*
ApiResourceManager<SerialConnection>::GetFactoryInstance() {
  return &g_factory.Get();
}

SerialConnection::SerialConnection(const std::string& port, int bitrate,
                                   const std::string& owner_extension_id)
    : ApiResource(owner_extension_id), port_(port), bitrate_(bitrate),
      file_(base::kInvalidPlatformFileValue) {
  CHECK_GE(bitrate, 0);
}

SerialConnection::~SerialConnection() {
  Close();
}

bool SerialConnection::Open() {
  bool created = false;

  // It's the responsibility of the API wrapper around SerialConnection to
  // validate the supplied path against the set of valid port names, and
  // it is a reasonable assumption that serial port names are ASCII.
  CHECK(IsStringASCII(port_));
  base::FilePath file_path(
      base::FilePath::FromUTF8Unsafe(MaybeFixUpPortName(port_)));

  file_ = base::CreatePlatformFile(file_path,
    base::PLATFORM_FILE_OPEN | base::PLATFORM_FILE_READ |
    base::PLATFORM_FILE_WRITE | base::PLATFORM_FILE_EXCLUSIVE_READ |
    base::PLATFORM_FILE_EXCLUSIVE_WRITE |
    base::PLATFORM_FILE_TERMINAL_DEVICE, &created, NULL);
  if (file_ == base::kInvalidPlatformFileValue) {
    return false;
  }
  return PostOpen();
}

void SerialConnection::Close() {
  if (file_ != base::kInvalidPlatformFileValue) {
    base::ClosePlatformFile(file_);
    file_ = base::kInvalidPlatformFileValue;
  }
}

int SerialConnection::Read(scoped_refptr<net::IOBufferWithSize> io_buffer) {
  DCHECK(io_buffer->data());
  return base::ReadPlatformFileAtCurrentPos(file_, io_buffer->data(),
                                            io_buffer->size());
}

int SerialConnection::Write(scoped_refptr<net::IOBuffer> io_buffer,
                            int byte_count) {
  DCHECK(io_buffer->data());
  DCHECK_GE(byte_count, 0);
  return base::WritePlatformFileAtCurrentPos(file_, io_buffer->data(),
                                             byte_count);
}

void SerialConnection::Flush() {
  base::FlushPlatformFile(file_);
}

}  // namespace extensions
