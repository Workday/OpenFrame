// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "modules/fetch/BodyStreamBuffer.h"

#include "core/testing/DummyPageHolder.h"
#include "modules/fetch/DataConsumerHandleTestUtil.h"
#include "platform/testing/UnitTestHelpers.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "wtf/OwnPtr.h"

namespace blink {

namespace {

using ::testing::InSequence;
using ::testing::_;
using ::testing::SaveArg;
using Checkpoint = ::testing::StrictMock<::testing::MockFunction<void(int)>>;
using Command = DataConsumerHandleTestUtil::Command;
using ReplayingHandle = DataConsumerHandleTestUtil::ReplayingHandle;
using MockFetchDataLoaderClient = DataConsumerHandleTestUtil::MockFetchDataLoaderClient;

class BodyStreamBufferTest : public ::testing::Test {
public:
    BodyStreamBufferTest()
    {
        m_page = DummyPageHolder::create(IntSize(1, 1));
    }
    ~BodyStreamBufferTest() override {}

protected:
    ScriptState* scriptState() { return ScriptState::forMainWorld(m_page->document().frame()); }
    ExecutionContext* executionContext() { return &m_page->document(); }

    OwnPtr<DummyPageHolder> m_page;
};

TEST_F(BodyStreamBufferTest, ReleaseHandle)
{
    OwnPtr<FetchDataConsumerHandle> handle = createFetchDataConsumerHandleFromWebHandle(createWaitingDataConsumerHandle());
    FetchDataConsumerHandle* rawHandle = handle.get();
    BodyStreamBuffer* buffer = new BodyStreamBuffer(handle.release());

    EXPECT_FALSE(buffer->hasPendingActivity());
    EXPECT_FALSE(buffer->stream()->isLocked());
    EXPECT_FALSE(buffer->stream()->isDisturbed());
    EXPECT_EQ(ReadableStream::Readable, buffer->stream()->stateInternal());

    OwnPtr<FetchDataConsumerHandle> handle2 = buffer->releaseHandle(executionContext());

    ASSERT_EQ(rawHandle, handle2.get());
    EXPECT_TRUE(buffer->stream()->isLocked());
    EXPECT_TRUE(buffer->stream()->isDisturbed());
    EXPECT_EQ(ReadableStream::Closed, buffer->stream()->stateInternal());
}

TEST_F(BodyStreamBufferTest, LoadBodyStreamBufferAsArrayBuffer)
{
    Checkpoint checkpoint;
    MockFetchDataLoaderClient* client = MockFetchDataLoaderClient::create();
    RefPtr<DOMArrayBuffer> arrayBuffer;

    InSequence s;
    EXPECT_CALL(checkpoint, Call(1));
    EXPECT_CALL(*client, didFetchDataLoadedArrayBufferMock(_)).WillOnce(SaveArg<0>(&arrayBuffer));
    EXPECT_CALL(checkpoint, Call(2));

    OwnPtr<ReplayingHandle> handle = ReplayingHandle::create();
    handle->add(Command(Command::Data, "hello"));
    handle->add(Command(Command::Done));
    BodyStreamBuffer* buffer = new BodyStreamBuffer(createFetchDataConsumerHandleFromWebHandle(handle.release()));
    buffer->startLoading(executionContext(), FetchDataLoader::createLoaderAsArrayBuffer(), client);

    EXPECT_TRUE(buffer->stream()->isLocked());
    EXPECT_TRUE(buffer->stream()->isDisturbed());
    EXPECT_TRUE(buffer->hasPendingActivity());

    checkpoint.Call(1);
    testing::runPendingTasks();
    checkpoint.Call(2);

    EXPECT_TRUE(buffer->stream()->isLocked());
    EXPECT_TRUE(buffer->stream()->isDisturbed());
    EXPECT_FALSE(buffer->hasPendingActivity());
    ASSERT_TRUE(arrayBuffer);
    EXPECT_EQ("hello", String(static_cast<const char*>(arrayBuffer->data()), arrayBuffer->byteLength()));
}

TEST_F(BodyStreamBufferTest, LoadBodyStreamBufferAsBlob)
{
    Checkpoint checkpoint;
    MockFetchDataLoaderClient* client = MockFetchDataLoaderClient::create();
    RefPtr<BlobDataHandle> blobDataHandle;

    InSequence s;
    EXPECT_CALL(checkpoint, Call(1));
    EXPECT_CALL(*client, didFetchDataLoadedBlobHandleMock(_)).WillOnce(SaveArg<0>(&blobDataHandle));
    EXPECT_CALL(checkpoint, Call(2));

    OwnPtr<ReplayingHandle> handle = ReplayingHandle::create();
    handle->add(Command(Command::Data, "hello"));
    handle->add(Command(Command::Done));
    BodyStreamBuffer* buffer = new BodyStreamBuffer(createFetchDataConsumerHandleFromWebHandle(handle.release()));
    buffer->startLoading(executionContext(), FetchDataLoader::createLoaderAsBlobHandle("text/plain"), client);

    EXPECT_TRUE(buffer->stream()->isLocked());
    EXPECT_TRUE(buffer->stream()->isDisturbed());
    EXPECT_TRUE(buffer->hasPendingActivity());

    checkpoint.Call(1);
    testing::runPendingTasks();
    checkpoint.Call(2);

    EXPECT_TRUE(buffer->stream()->isLocked());
    EXPECT_TRUE(buffer->stream()->isDisturbed());
    EXPECT_FALSE(buffer->hasPendingActivity());
    EXPECT_EQ(5u, blobDataHandle->size());
}

TEST_F(BodyStreamBufferTest, LoadBodyStreamBufferAsString)
{
    Checkpoint checkpoint;
    MockFetchDataLoaderClient* client = MockFetchDataLoaderClient::create();

    InSequence s;
    EXPECT_CALL(checkpoint, Call(1));
    EXPECT_CALL(*client, didFetchDataLoadedString(String("hello")));
    EXPECT_CALL(checkpoint, Call(2));

    OwnPtr<ReplayingHandle> handle = ReplayingHandle::create();
    handle->add(Command(Command::Data, "hello"));
    handle->add(Command(Command::Done));
    BodyStreamBuffer* buffer = new BodyStreamBuffer(createFetchDataConsumerHandleFromWebHandle(handle.release()));
    buffer->startLoading(executionContext(), FetchDataLoader::createLoaderAsString(), client);

    EXPECT_TRUE(buffer->stream()->isLocked());
    EXPECT_TRUE(buffer->stream()->isDisturbed());
    EXPECT_TRUE(buffer->hasPendingActivity());

    checkpoint.Call(1);
    testing::runPendingTasks();
    checkpoint.Call(2);

    EXPECT_TRUE(buffer->stream()->isLocked());
    EXPECT_TRUE(buffer->stream()->isDisturbed());
    EXPECT_FALSE(buffer->hasPendingActivity());
}

TEST_F(BodyStreamBufferTest, ReleaseClosedHandle)
{
    BodyStreamBuffer* buffer = new BodyStreamBuffer(createFetchDataConsumerHandleFromWebHandle(createDoneDataConsumerHandle()));

    EXPECT_EQ(ReadableStream::Readable, buffer->stream()->stateInternal());
    testing::runPendingTasks();
    EXPECT_EQ(ReadableStream::Closed, buffer->stream()->stateInternal());

    EXPECT_FALSE(buffer->stream()->isLocked());
    EXPECT_FALSE(buffer->stream()->isDisturbed());
    EXPECT_FALSE(buffer->hasPendingActivity());
    OwnPtr<FetchDataConsumerHandle> handle = buffer->releaseHandle(executionContext());

    EXPECT_TRUE(handle);
    EXPECT_TRUE(buffer->stream()->isLocked());
    EXPECT_TRUE(buffer->stream()->isDisturbed());
    EXPECT_FALSE(buffer->hasPendingActivity());
}

TEST_F(BodyStreamBufferTest, LoadClosedHandle)
{
    Checkpoint checkpoint;
    MockFetchDataLoaderClient* client = MockFetchDataLoaderClient::create();

    InSequence s;
    EXPECT_CALL(checkpoint, Call(1));
    EXPECT_CALL(*client, didFetchDataLoadedString(String("")));
    EXPECT_CALL(checkpoint, Call(2));

    BodyStreamBuffer* buffer = new BodyStreamBuffer(createFetchDataConsumerHandleFromWebHandle(createDoneDataConsumerHandle()));

    EXPECT_EQ(ReadableStream::Readable, buffer->stream()->stateInternal());
    testing::runPendingTasks();
    EXPECT_EQ(ReadableStream::Closed, buffer->stream()->stateInternal());

    EXPECT_FALSE(buffer->stream()->isLocked());
    EXPECT_FALSE(buffer->stream()->isDisturbed());
    EXPECT_FALSE(buffer->hasPendingActivity());

    buffer->startLoading(executionContext(), FetchDataLoader::createLoaderAsString(), client);
    EXPECT_TRUE(buffer->stream()->isLocked());
    EXPECT_TRUE(buffer->stream()->isDisturbed());
    EXPECT_TRUE(buffer->hasPendingActivity());

    checkpoint.Call(1);
    testing::runPendingTasks();
    checkpoint.Call(2);

    EXPECT_TRUE(buffer->stream()->isLocked());
    EXPECT_TRUE(buffer->stream()->isDisturbed());
    EXPECT_FALSE(buffer->hasPendingActivity());
}

TEST_F(BodyStreamBufferTest, ReleaseErroredHandle)
{
    BodyStreamBuffer* buffer = new BodyStreamBuffer(createFetchDataConsumerHandleFromWebHandle(createUnexpectedErrorDataConsumerHandle()));

    EXPECT_EQ(ReadableStream::Readable, buffer->stream()->stateInternal());
    testing::runPendingTasks();
    EXPECT_EQ(ReadableStream::Errored, buffer->stream()->stateInternal());

    EXPECT_FALSE(buffer->stream()->isLocked());
    EXPECT_FALSE(buffer->stream()->isDisturbed());
    EXPECT_FALSE(buffer->hasPendingActivity());
    OwnPtr<FetchDataConsumerHandle> handle = buffer->releaseHandle(executionContext());
    EXPECT_TRUE(handle);
    EXPECT_TRUE(buffer->stream()->isLocked());
    EXPECT_TRUE(buffer->stream()->isDisturbed());
    EXPECT_FALSE(buffer->hasPendingActivity());
}

TEST_F(BodyStreamBufferTest, LoadErroredHandle)
{
    Checkpoint checkpoint;
    MockFetchDataLoaderClient* client = MockFetchDataLoaderClient::create();

    InSequence s;
    EXPECT_CALL(checkpoint, Call(1));
    EXPECT_CALL(*client, didFetchDataLoadFailed());
    EXPECT_CALL(checkpoint, Call(2));

    BodyStreamBuffer* buffer = new BodyStreamBuffer(createFetchDataConsumerHandleFromWebHandle(createUnexpectedErrorDataConsumerHandle()));

    EXPECT_EQ(ReadableStream::Readable, buffer->stream()->stateInternal());
    testing::runPendingTasks();
    EXPECT_EQ(ReadableStream::Errored, buffer->stream()->stateInternal());

    EXPECT_FALSE(buffer->stream()->isLocked());
    EXPECT_FALSE(buffer->stream()->isDisturbed());
    EXPECT_FALSE(buffer->hasPendingActivity());
    buffer->startLoading(executionContext(), FetchDataLoader::createLoaderAsString(), client);
    EXPECT_TRUE(buffer->stream()->isLocked());
    EXPECT_TRUE(buffer->stream()->isDisturbed());
    EXPECT_TRUE(buffer->hasPendingActivity());

    checkpoint.Call(1);
    testing::runPendingTasks();
    checkpoint.Call(2);

    EXPECT_TRUE(buffer->stream()->isLocked());
    EXPECT_TRUE(buffer->stream()->isDisturbed());
    EXPECT_FALSE(buffer->hasPendingActivity());
}

TEST_F(BodyStreamBufferTest, LoaderShouldBeKeptAliveByBodyStreamBuffer)
{
    Checkpoint checkpoint;
    MockFetchDataLoaderClient* client = MockFetchDataLoaderClient::create();

    InSequence s;
    EXPECT_CALL(checkpoint, Call(1));
    EXPECT_CALL(*client, didFetchDataLoadedString(String("hello")));
    EXPECT_CALL(checkpoint, Call(2));

    OwnPtr<ReplayingHandle> handle = ReplayingHandle::create();
    handle->add(Command(Command::Data, "hello"));
    handle->add(Command(Command::Done));
    Persistent<BodyStreamBuffer> buffer = new BodyStreamBuffer(createFetchDataConsumerHandleFromWebHandle(handle.release()));
    buffer->startLoading(executionContext(), FetchDataLoader::createLoaderAsString(), client);

    Heap::collectAllGarbage();
    checkpoint.Call(1);
    testing::runPendingTasks();
    checkpoint.Call(2);
}

// TODO(hiroshige): Merge this class into MockFetchDataConsumerHandle.
class MockFetchDataConsumerHandleWithMockDestructor : public DataConsumerHandleTestUtil::MockFetchDataConsumerHandle {
public:
    static PassOwnPtr<::testing::StrictMock<MockFetchDataConsumerHandleWithMockDestructor>> create() { return adoptPtr(new ::testing::StrictMock<MockFetchDataConsumerHandleWithMockDestructor>); }

    ~MockFetchDataConsumerHandleWithMockDestructor() override
    {
        destruct();
    }

    MOCK_METHOD0(destruct, void());
};

TEST_F(BodyStreamBufferTest, SourceHandleAndReaderShouldBeDestructedWhenCanceled)
{
    ScriptState::Scope scope(scriptState());
    using MockHandle = MockFetchDataConsumerHandleWithMockDestructor;
    using MockReader = DataConsumerHandleTestUtil::MockFetchDataConsumerReader;
    OwnPtr<MockHandle> handle = MockHandle::create();
    OwnPtr<MockReader> reader = MockReader::create();

    Checkpoint checkpoint;
    InSequence s;

    EXPECT_CALL(*handle, obtainReaderInternal(_)).WillOnce(::testing::Return(reader.get()));
    EXPECT_CALL(checkpoint, Call(1));
    EXPECT_CALL(*reader, destruct());
    EXPECT_CALL(*handle, destruct());
    EXPECT_CALL(checkpoint, Call(2));

    // |reader| is adopted by |obtainReader|.
    ASSERT_TRUE(reader.leakPtr());

    BodyStreamBuffer* buffer = new BodyStreamBuffer(handle.release());
    checkpoint.Call(1);
    ScriptValue reason(scriptState(), v8String(scriptState()->isolate(), "reason"));
    buffer->cancelSource(scriptState(), reason);
    checkpoint.Call(2);
}

} // namespace

} // namespace blink
