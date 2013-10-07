// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/messaging/native_message_process_host.h"

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/platform_file.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/messaging/native_messaging_host_manifest.h"
#include "chrome/browser/extensions/api/messaging/native_process_launcher.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/extensions/features/feature.h"
#include "extensions/common/constants.h"
#include "net/base/file_stream.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/net_util.h"
#include "url/gurl.h"

namespace {

// Maximum message size in bytes for messages received from Native Messaging
// hosts. Message size is limited mainly to prevent Chrome from crashing when
// native application misbehaves (e.g. starts writing garbage to the pipe).
const size_t kMaximumMessageSize = 1024 * 1024;

// Message header contains 4-byte integer size of the message.
const size_t kMessageHeaderSize = 4;

// Size of the buffer to be allocated for each read.
const size_t kReadBufferSize = 4096;

const char kFailedToStartError[] = "Failed to start native messaging host.";
const char kInvalidNameError[] =
    "Invalid native messaging host name specified.";
const char kNotFoundError[] = "Specified native messaging host not found.";
const char kForbiddenError[] =
    "Access to the specified native messaging host is forbidden.";
const char kHostInputOuputError[] =
    "Error when communicating with the native messaging host.";

}  // namespace

namespace extensions {

NativeMessageProcessHost::NativeMessageProcessHost(
    base::WeakPtr<Client> weak_client_ui,
    const std::string& source_extension_id,
    const std::string& native_host_name,
    int destination_port,
    scoped_ptr<NativeProcessLauncher> launcher)
    : weak_client_ui_(weak_client_ui),
      source_extension_id_(source_extension_id),
      native_host_name_(native_host_name),
      destination_port_(destination_port),
      launcher_(launcher.Pass()),
      closed_(false),
      read_pending_(false),
      read_eof_(false),
      write_pending_(false) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  // It's safe to use base::Unretained() here because NativeMessagePort always
  // deletes us on the IO thread.
  content::BrowserThread::PostTask(content::BrowserThread::IO, FROM_HERE,
      base::Bind(&NativeMessageProcessHost::LaunchHostProcess,
                 base::Unretained(this)));
}

NativeMessageProcessHost::~NativeMessageProcessHost() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));
  Close(std::string());
}

// static
scoped_ptr<NativeMessageProcessHost> NativeMessageProcessHost::Create(
    gfx::NativeView native_view,
    base::WeakPtr<Client> weak_client_ui,
    const std::string& source_extension_id,
    const std::string& native_host_name,
    int destination_port) {
  return CreateWithLauncher(weak_client_ui, source_extension_id,
                            native_host_name, destination_port,
                            NativeProcessLauncher::CreateDefault(native_view));
}

// static
scoped_ptr<NativeMessageProcessHost>
NativeMessageProcessHost::CreateWithLauncher(
    base::WeakPtr<Client> weak_client_ui,
    const std::string& source_extension_id,
    const std::string& native_host_name,
    int destination_port,
    scoped_ptr<NativeProcessLauncher> launcher) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  scoped_ptr<NativeMessageProcessHost> process(new NativeMessageProcessHost(
      weak_client_ui, source_extension_id, native_host_name,
      destination_port, launcher.Pass()));

  return process.Pass();
}

void NativeMessageProcessHost::LaunchHostProcess() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));

  GURL origin(std::string(kExtensionScheme) + "://" + source_extension_id_);
  launcher_->Launch(origin, native_host_name_,
                    base::Bind(&NativeMessageProcessHost::OnHostProcessLaunched,
                               base::Unretained(this)));
}

void NativeMessageProcessHost::OnHostProcessLaunched(
    NativeProcessLauncher::LaunchResult result,
    base::PlatformFile read_file,
    base::PlatformFile write_file) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));

  switch (result) {
    case NativeProcessLauncher::RESULT_INVALID_NAME:
      Close(kInvalidNameError);
      return;
    case NativeProcessLauncher::RESULT_NOT_FOUND:
      Close(kNotFoundError);
      return;
    case NativeProcessLauncher::RESULT_FORBIDDEN:
      Close(kForbiddenError);
      return;
    case NativeProcessLauncher::RESULT_FAILED_TO_START:
      Close(kFailedToStartError);
      return;
    case NativeProcessLauncher::RESULT_SUCCESS:
      break;
  }

  read_file_ = read_file;

  scoped_refptr<base::TaskRunner> task_runner(
      content::BrowserThread::GetBlockingPool()->
          GetTaskRunnerWithShutdownBehavior(
              base::SequencedWorkerPool::SKIP_ON_SHUTDOWN));

  read_stream_.reset(new net::FileStream(
      read_file, base::PLATFORM_FILE_READ | base::PLATFORM_FILE_ASYNC, NULL,
      task_runner));
  write_stream_.reset(new net::FileStream(
      write_file, base::PLATFORM_FILE_WRITE | base::PLATFORM_FILE_ASYNC, NULL,
      task_runner));

  WaitRead();
  DoWrite();
}

void NativeMessageProcessHost::Send(const std::string& json) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));

  if (closed_)
    return;

  // Allocate new buffer for the message.
  scoped_refptr<net::IOBufferWithSize> buffer =
      new net::IOBufferWithSize(json.size() + kMessageHeaderSize);

  // Copy size and content of the message to the buffer.
  COMPILE_ASSERT(sizeof(uint32) == kMessageHeaderSize, incorrect_header_size);
  *reinterpret_cast<uint32*>(buffer->data()) = json.size();
  memcpy(buffer->data() + kMessageHeaderSize, json.data(), json.size());

  // Push new message to the write queue.
  write_queue_.push(buffer);

  // Send() may be called before the host process is started. In that case the
  // message will be written when OnHostProcessLaunched() is called. If it's
  // already started then write the message now.
  if (write_stream_)
    DoWrite();
}

#if defined(OS_POSIX)
void NativeMessageProcessHost::OnFileCanReadWithoutBlocking(int fd) {
  DCHECK_EQ(fd, read_file_);
  DoRead();
}

void NativeMessageProcessHost::OnFileCanWriteWithoutBlocking(int fd) {
  NOTREACHED();
}
#endif  // !defined(OS_POSIX)

void NativeMessageProcessHost::ReadNowForTesting() {
  DoRead();
}

void NativeMessageProcessHost::WaitRead() {
  if (closed_ || read_eof_)
    return;

  DCHECK(!read_pending_);

  // On POSIX FileStream::Read() uses blocking thread pool, so it's better to
  // wait for the file to become readable before calling DoRead(). Otherwise it
  // would always be consuming one thread in the thread pool. On Windows
  // FileStream uses overlapped IO, so that optimization isn't necessary there.
#if defined(OS_POSIX)
  base::MessageLoopForIO::current()->WatchFileDescriptor(
    read_file_, false /* persistent */, base::MessageLoopForIO::WATCH_READ,
    &read_watcher_, this);
#else  // defined(OS_POSIX)
  DoRead();
#endif  // defined(!OS_POSIX)
}

void NativeMessageProcessHost::DoRead() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));

  while (!closed_ && !read_eof_ && !read_pending_) {
    read_buffer_ = new net::IOBuffer(kReadBufferSize);
    int result = read_stream_->Read(
        read_buffer_.get(),
        kReadBufferSize,
        base::Bind(&NativeMessageProcessHost::OnRead, base::Unretained(this)));
    HandleReadResult(result);
  }
}

void NativeMessageProcessHost::OnRead(int result) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));
  DCHECK(read_pending_);
  read_pending_ = false;

  HandleReadResult(result);
  WaitRead();
}

void NativeMessageProcessHost::HandleReadResult(int result) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));

  if (closed_)
    return;

  if (result > 0) {
    ProcessIncomingData(read_buffer_->data(), result);
  } else if (result == 0) {
    read_eof_ = true;
  } else if (result == net::ERR_IO_PENDING) {
    read_pending_ = true;
  } else {
    LOG(ERROR) << "Error when reading from Native Messaging host: " << result;
    Close(kHostInputOuputError);
  }
}

void NativeMessageProcessHost::ProcessIncomingData(
    const char* data, int data_size) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));

  incoming_data_.append(data, data_size);

  while (true) {
    if (incoming_data_.size() < kMessageHeaderSize)
      return;

    size_t message_size =
        *reinterpret_cast<const uint32*>(incoming_data_.data());

    if (message_size > kMaximumMessageSize) {
      LOG(ERROR) << "Native Messaging host tried sending a message that is "
                 << message_size << " bytes long.";
      Close(kHostInputOuputError);
      return;
    }

    if (incoming_data_.size() < message_size + kMessageHeaderSize)
      return;

    content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
        base::Bind(&Client::PostMessageFromNativeProcess, weak_client_ui_,
            destination_port_,
            incoming_data_.substr(kMessageHeaderSize, message_size)));

    incoming_data_.erase(0, kMessageHeaderSize + message_size);
  }
}

void NativeMessageProcessHost::DoWrite() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));

  while (!write_pending_ && !closed_) {
    if (!current_write_buffer_.get() ||
        !current_write_buffer_->BytesRemaining()) {
      if (write_queue_.empty())
        return;
      current_write_buffer_ = new net::DrainableIOBuffer(
          write_queue_.front().get(), write_queue_.front()->size());
      write_queue_.pop();
    }

    int result =
        write_stream_->Write(current_write_buffer_.get(),
                             current_write_buffer_->BytesRemaining(),
                             base::Bind(&NativeMessageProcessHost::OnWritten,
                                        base::Unretained(this)));
    HandleWriteResult(result);
  }
}

void NativeMessageProcessHost::HandleWriteResult(int result) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));

  if (result <= 0) {
    if (result == net::ERR_IO_PENDING) {
      write_pending_ = true;
    } else {
      LOG(ERROR) << "Error when writing to Native Messaging host: " << result;
      Close(kHostInputOuputError);
    }
    return;
  }

  current_write_buffer_->DidConsume(result);
}

void NativeMessageProcessHost::OnWritten(int result) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));

  DCHECK(write_pending_);
  write_pending_ = false;

  HandleWriteResult(result);
  DoWrite();
}

void NativeMessageProcessHost::Close(const std::string& error_message) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));

  if (!closed_) {
    closed_ = true;
    read_stream_.reset();
    write_stream_.reset();
    content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
        base::Bind(&Client::CloseChannel, weak_client_ui_,
                   destination_port_, error_message));
  }
}

}  // namespace extensions
