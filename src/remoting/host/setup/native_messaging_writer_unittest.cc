// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/setup/native_messaging_writer.h"

#include "base/basictypes.h"
#include "base/json/json_reader.h"
#include "base/memory/scoped_ptr.h"
#include "base/platform_file.h"
#include "base/stl_util.h"
#include "base/values.h"
#include "remoting/host/setup/test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace remoting {

class NativeMessagingWriterTest : public testing::Test {
 public:
  NativeMessagingWriterTest();
  virtual ~NativeMessagingWriterTest();

  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

 protected:
  scoped_ptr<NativeMessagingWriter> writer_;
  base::PlatformFile read_handle_;
  base::PlatformFile write_handle_;
  bool read_handle_open_;
};

NativeMessagingWriterTest::NativeMessagingWriterTest() {}

NativeMessagingWriterTest::~NativeMessagingWriterTest() {}

void NativeMessagingWriterTest::SetUp() {
  ASSERT_TRUE(MakePipe(&read_handle_, &write_handle_));
  writer_.reset(new NativeMessagingWriter(write_handle_));
  read_handle_open_ = true;
}

void NativeMessagingWriterTest::TearDown() {
  // |write_handle_| is owned by NativeMessagingWriter's FileStream, so don't
  // try to close it here. And close |read_handle_| only if it hasn't
  // already been closed.
  if (read_handle_open_)
    base::ClosePlatformFile(read_handle_);
}

TEST_F(NativeMessagingWriterTest, GoodMessage) {
  base::DictionaryValue message;
  message.SetInteger("foo", 42);
  EXPECT_TRUE(writer_->WriteMessage(message));

  // Read from the pipe and verify the content.
  uint32 length;
  int read = base::ReadPlatformFileAtCurrentPos(
      read_handle_, reinterpret_cast<char*>(&length), 4);
  EXPECT_EQ(4, read);
  std::string content(length, '\0');
  read = base::ReadPlatformFileAtCurrentPos(read_handle_,
                                            string_as_array(&content), length);
  EXPECT_EQ(static_cast<int>(length), read);

  // |content| should now contain serialized |message|.
  scoped_ptr<base::Value> written_message(base::JSONReader::Read(content));
  EXPECT_TRUE(message.Equals(written_message.get()));

  // Nothing more should have been written. Close the write-end of the pipe,
  // and verify the read end immediately hits EOF.
  writer_.reset(NULL);
  char unused;
  read = base::ReadPlatformFileAtCurrentPos(read_handle_, &unused, 1);
  EXPECT_LE(read, 0);
}

TEST_F(NativeMessagingWriterTest, SecondMessage) {
  base::DictionaryValue message1;
  base::DictionaryValue message2;
  message2.SetInteger("foo", 42);
  EXPECT_TRUE(writer_->WriteMessage(message1));
  EXPECT_TRUE(writer_->WriteMessage(message2));
  writer_.reset(NULL);

  // Read two messages.
  uint32 length;
  int read;
  std::string content;
  for (int i = 0; i < 2; i++) {
    read = base::ReadPlatformFileAtCurrentPos(
        read_handle_, reinterpret_cast<char*>(&length), 4);
    EXPECT_EQ(4, read) << "i = " << i;
    content.resize(length);
    read = base::ReadPlatformFileAtCurrentPos(read_handle_,
                                              string_as_array(&content),
                                              length);
    EXPECT_EQ(static_cast<int>(length), read) << "i = " << i;
  }

  // |content| should now contain serialized |message2|.
  scoped_ptr<base::Value> written_message2(base::JSONReader::Read(content));
  EXPECT_TRUE(message2.Equals(written_message2.get()));
}

TEST_F(NativeMessagingWriterTest, FailedWrite) {
  // Close the read end so that writing fails immediately.
  base::ClosePlatformFile(read_handle_);
  read_handle_open_ = false;

  base::DictionaryValue message;
  EXPECT_FALSE(writer_->WriteMessage(message));
}

}  // namespace remoting
