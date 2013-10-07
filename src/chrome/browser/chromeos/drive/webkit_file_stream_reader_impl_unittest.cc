// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/webkit_file_stream_reader_impl.h"

#include <string>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/drive/fake_file_system.h"
#include "chrome/browser/chromeos/drive/file_system_interface.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#include "chrome/browser/chromeos/drive/test_util.h"
#include "chrome/browser/drive/fake_drive_service.h"
#include "chrome/browser/google_apis/time_util.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace drive {
namespace internal {

class WebkitFileStreamReaderImplTest : public ::testing::Test {
 protected:
  // Because the testee should live on IO thread, the main thread is
  // reused as IO thread, and UI thread will be run on background.
  WebkitFileStreamReaderImplTest()
      : thread_bundle_(content::TestBrowserThreadBundle::IO_MAINLOOP) {
  }

  virtual void SetUp() OVERRIDE {
    worker_thread_.reset(new base::Thread("WebkitFileStreamReaderImplTest"));
    ASSERT_TRUE(worker_thread_->Start());

    // Initialize FakeDriveService.
    fake_drive_service_.reset(new FakeDriveService);
    fake_drive_service_->LoadResourceListForWapi(
        "gdata/root_feed.json");
    fake_drive_service_->LoadAccountMetadataForWapi(
        "gdata/account_metadata.json");

    // Create a testee instance.
    fake_file_system_.reset(
        new test_util::FakeFileSystem(fake_drive_service_.get()));
    fake_file_system_->Initialize();
  }

  FileSystemInterface* GetFileSystem() {
    return fake_file_system_.get();
  }

  DriveFileStreamReader::FileSystemGetter GetFileSystemGetter() {
    return base::Bind(&WebkitFileStreamReaderImplTest::GetFileSystem,
                      base::Unretained(this));
  }

  content::TestBrowserThreadBundle thread_bundle_;

  scoped_ptr<base::Thread> worker_thread_;

  scoped_ptr<FakeDriveService> fake_drive_service_;
  scoped_ptr<test_util::FakeFileSystem> fake_file_system_;
};

TEST_F(WebkitFileStreamReaderImplTest, ReadThenGetLength) {
  const base::FilePath kDriveFile =
      util::GetDriveMyDriveRootPath().AppendASCII("File 1.txt");

  scoped_ptr<WebkitFileStreamReaderImpl> reader(new WebkitFileStreamReaderImpl(
      GetFileSystemGetter(),
      worker_thread_->message_loop_proxy().get(),
      kDriveFile,
      0,               // offset
      base::Time()));  // expected modification time

  std::string content;
  ASSERT_EQ(net::OK, test_util::ReadAllData(reader.get(), &content));

  net::TestInt64CompletionCallback callback;
  int64 length = reader->GetLength(callback.callback());
  length = callback.GetResult(length);
  EXPECT_EQ(content.size(), static_cast<size_t>(length));
}

TEST_F(WebkitFileStreamReaderImplTest, GetLengthThenRead) {
  const base::FilePath kDriveFile =
      util::GetDriveMyDriveRootPath().AppendASCII("File 1.txt");

  scoped_ptr<WebkitFileStreamReaderImpl> reader(new WebkitFileStreamReaderImpl(
      GetFileSystemGetter(),
      worker_thread_->message_loop_proxy().get(),
      kDriveFile,
      0,               // offset
      base::Time()));  // expected modification time

  net::TestInt64CompletionCallback callback;
  int64 length = reader->GetLength(callback.callback());
  length = callback.GetResult(length);

  std::string content;
  ASSERT_EQ(net::OK, test_util::ReadAllData(reader.get(), &content));
  EXPECT_EQ(content.size(), static_cast<size_t>(length));
}

TEST_F(WebkitFileStreamReaderImplTest, ReadWithOffset) {
  const base::FilePath kDriveFile =
      util::GetDriveMyDriveRootPath().AppendASCII("File 1.txt");
  const int kOffset = 5;

  scoped_ptr<WebkitFileStreamReaderImpl> reader(new WebkitFileStreamReaderImpl(
      GetFileSystemGetter(),
      worker_thread_->message_loop_proxy().get(),
      kDriveFile,
      kOffset,
      base::Time()));  // expected modification time

  std::string content;
  ASSERT_EQ(net::OK, test_util::ReadAllData(reader.get(), &content));

  net::TestInt64CompletionCallback callback;
  int64 length = reader->GetLength(callback.callback());
  length = callback.GetResult(length);
  EXPECT_EQ(content.size() + kOffset, static_cast<size_t>(length));
}

TEST_F(WebkitFileStreamReaderImplTest, ReadError) {
  const base::FilePath kDriveFile =
      util::GetDriveMyDriveRootPath().AppendASCII("non-existing.txt");

  scoped_ptr<WebkitFileStreamReaderImpl> reader(new WebkitFileStreamReaderImpl(
      GetFileSystemGetter(),
      worker_thread_->message_loop_proxy().get(),
      kDriveFile,
      0,               // offset
      base::Time()));  // expected modification time

  const int kBufferSize = 10;
  scoped_refptr<net::IOBuffer> io_buffer(new net::IOBuffer(kBufferSize));
  net::TestCompletionCallback callback;
  int result = reader->Read(io_buffer.get(), kBufferSize, callback.callback());
  result = callback.GetResult(result);
  EXPECT_EQ(net::ERR_FILE_NOT_FOUND, result);
}

TEST_F(WebkitFileStreamReaderImplTest, GetLengthError) {
  const base::FilePath kDriveFile =
      util::GetDriveMyDriveRootPath().AppendASCII("non-existing.txt");

  scoped_ptr<WebkitFileStreamReaderImpl> reader(new WebkitFileStreamReaderImpl(
      GetFileSystemGetter(),
      worker_thread_->message_loop_proxy().get(),
      kDriveFile,
      0,               // offset
      base::Time()));  // expected modification time

  net::TestInt64CompletionCallback callback;
  int64 result = reader->GetLength(callback.callback());
  result = callback.GetResult(result);
  EXPECT_EQ(net::ERR_FILE_NOT_FOUND, result);
}

TEST_F(WebkitFileStreamReaderImplTest, LastModification) {
  const base::FilePath kDriveFile =
      util::GetDriveMyDriveRootPath().AppendASCII("File 1.txt");

  base::Time expected_modification_time;
  ASSERT_TRUE(google_apis::util::GetTimeFromString(
      "2011-12-14T00:40:47.330Z", &expected_modification_time));
  scoped_ptr<WebkitFileStreamReaderImpl> reader(
      new WebkitFileStreamReaderImpl(GetFileSystemGetter(),
                                     worker_thread_->message_loop_proxy().get(),
                                     kDriveFile,
                                     0,  // offset
                                     expected_modification_time));

  net::TestInt64CompletionCallback callback;
  int64 result = reader->GetLength(callback.callback());
  result = callback.GetResult(result);

  std::string content;
  ASSERT_EQ(net::OK, test_util::ReadAllData(reader.get(), &content));
  EXPECT_GE(content.size(), static_cast<size_t>(result));
}

TEST_F(WebkitFileStreamReaderImplTest, LastModificationError) {
  const base::FilePath kDriveFile =
      util::GetDriveMyDriveRootPath().AppendASCII("File 1.txt");

  scoped_ptr<WebkitFileStreamReaderImpl> reader(
      new WebkitFileStreamReaderImpl(GetFileSystemGetter(),
                                     worker_thread_->message_loop_proxy().get(),
                                     kDriveFile,
                                     0,  // offset
                                     base::Time::FromInternalValue(1)));

  net::TestInt64CompletionCallback callback;
  int64 result = reader->GetLength(callback.callback());
  result = callback.GetResult(result);
  EXPECT_EQ(net::ERR_UPLOAD_FILE_CHANGED, result);
}

}  // namespace internal
}  // namespace drive
