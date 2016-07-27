// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "media/base/media_log.h"
#include "media/base/mock_filters.h"
#include "media/base/test_helpers.h"
#include "media/blink/buffered_data_source.h"
#include "media/blink/mock_webframeclient.h"
#include "media/blink/mock_weburlloader.h"
#include "media/blink/multibuffer_data_source.h"
#include "media/blink/multibuffer_reader.h"
#include "media/blink/resource_multibuffer_data_provider.h"
#include "media/blink/test_response_generator.h"
#include "third_party/WebKit/public/platform/WebURLResponse.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/web/WebView.h"

using ::testing::_;
using ::testing::Assign;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::StrictMock;

using blink::WebLocalFrame;
using blink::WebString;
using blink::WebURLLoader;
using blink::WebURLResponse;
using blink::WebView;

namespace media {

class TestResourceMultiBuffer;
class TestMultiBufferDataProvider;

std::set<TestMultiBufferDataProvider*> test_data_providers;

class TestMultiBufferDataProvider : public ResourceMultiBufferDataProvider {
 public:
  TestMultiBufferDataProvider(UrlData* url_data, MultiBuffer::BlockId pos)
      : ResourceMultiBufferDataProvider(url_data, pos), loading_(false) {
    CHECK(test_data_providers.insert(this).second);
  }
  ~TestMultiBufferDataProvider() {
    CHECK_EQ(static_cast<size_t>(1), test_data_providers.erase(this));
  }
  void Start() override {
    // Create a mock active loader.
    // Keep track of active loading state via loadAsynchronously() and cancel().
    NiceMock<MockWebURLLoader>* url_loader = new NiceMock<MockWebURLLoader>();
    ON_CALL(*url_loader, cancel()).WillByDefault(Assign(&loading_, false));
    loading_ = true;
    active_loader_.reset(
        new ActiveLoader(scoped_ptr<WebURLLoader>(url_loader)));
    if (!on_start_.is_null()) {
      on_start_.Run();
    }
  }

  bool loading() const { return loading_; }
  void RunOnStart(base::Closure cb) { on_start_ = cb; }

 private:
  bool loading_;
  base::Closure on_start_;
};

class TestUrlData;

class TestResourceMultiBuffer : public ResourceMultiBuffer {
 public:
  explicit TestResourceMultiBuffer(UrlData* url_data, int shift)
      : ResourceMultiBuffer(url_data, shift) {}

  scoped_ptr<MultiBuffer::DataProvider> CreateWriter(
      const BlockId& pos) override {
    TestMultiBufferDataProvider* ret =
        new TestMultiBufferDataProvider(url_data_, pos);
    ret->Start();
    return scoped_ptr<MultiBuffer::DataProvider>(ret);
  }

  // TODO: Make these global

  TestMultiBufferDataProvider* GetProvider() {
    EXPECT_EQ(test_data_providers.size(), 1U);
    if (test_data_providers.size() != 1)
      return nullptr;
    return *test_data_providers.begin();
  }
  TestMultiBufferDataProvider* GetProvider_allownull() {
    EXPECT_LE(test_data_providers.size(), 1U);
    if (test_data_providers.size() != 1U)
      return nullptr;
    return *test_data_providers.begin();
  }
  bool HasProvider() const { return test_data_providers.size() == 1U; }
  bool loading() {
    if (test_data_providers.empty())
      return false;
    return GetProvider()->loading();
  }
};

class TestUrlData : public UrlData {
 public:
  TestUrlData(const GURL& url,
              CORSMode cors_mode,
              const base::WeakPtr<UrlIndex>& url_index)
      : UrlData(url, cors_mode, url_index),
        block_shift_(url_index->block_shift()) {}

  ResourceMultiBuffer* multibuffer() override {
    if (!test_multibuffer_.get()) {
      test_multibuffer_.reset(new TestResourceMultiBuffer(this, block_shift_));
    }
    return test_multibuffer_.get();
  }

  TestResourceMultiBuffer* test_multibuffer() {
    if (!test_multibuffer_.get()) {
      test_multibuffer_.reset(new TestResourceMultiBuffer(this, block_shift_));
    }
    return test_multibuffer_.get();
  }

 protected:
  ~TestUrlData() override {}
  const int block_shift_;

  scoped_ptr<TestResourceMultiBuffer> test_multibuffer_;
};

class TestUrlIndex : public UrlIndex {
 public:
  explicit TestUrlIndex(blink::WebFrame* frame) : UrlIndex(frame) {}

  scoped_refptr<UrlData> NewUrlData(const GURL& url,
                                    UrlData::CORSMode cors_mode) override {
    last_url_data_ =
        new TestUrlData(url, cors_mode, weak_factory_.GetWeakPtr());
    return last_url_data_;
  }

  scoped_refptr<TestUrlData> last_url_data() {
    EXPECT_TRUE(last_url_data_);
    return last_url_data_;
  }

 private:
  scoped_refptr<TestUrlData> last_url_data_;
};

class MockBufferedDataSourceHost : public BufferedDataSourceHost {
 public:
  MockBufferedDataSourceHost() {}
  virtual ~MockBufferedDataSourceHost() {}

  MOCK_METHOD1(SetTotalBytes, void(int64 total_bytes));
  MOCK_METHOD2(AddBufferedByteRange, void(int64 start, int64 end));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockBufferedDataSourceHost);
};

class MockMultibufferDataSource : public MultibufferDataSource {
 public:
  MockMultibufferDataSource(
      const GURL& url,
      const scoped_refptr<base::SingleThreadTaskRunner>& task_runner,
      linked_ptr<UrlIndex> url_index,
      WebLocalFrame* frame,
      BufferedDataSourceHost* host)
      : MultibufferDataSource(
            url,
            UrlData::CORS_UNSPECIFIED,
            task_runner,
            url_index,
            frame,
            new media::MediaLog(),
            host,
            base::Bind(&MockMultibufferDataSource::set_downloading,
                       base::Unretained(this))),
        downloading_(false) {}

  bool downloading() { return downloading_; }
  void set_downloading(bool downloading) { downloading_ = downloading; }
  bool range_supported() { return url_data_->range_supported(); }

 private:
  // Whether the resource is downloading or deferred.
  bool downloading_;

  // Whether the resource load has starting loading but yet to been cancelled.
  bool loading_;

  DISALLOW_COPY_AND_ASSIGN(MockMultibufferDataSource);
};

static const int64 kFileSize = 5000000;
static const int64 kFarReadPosition = 3997696;
static const int kDataSize = 32 << 10;

static const char kHttpUrl[] = "http://localhost/foo.webm";
static const char kFileUrl[] = "file:///tmp/bar.webm";
static const char kHttpDifferentPathUrl[] = "http://localhost/bar.webm";
static const char kHttpDifferentOriginUrl[] = "http://127.0.0.1/foo.webm";

class MultibufferDataSourceTest : public testing::Test {
 public:
  MultibufferDataSourceTest()
      : view_(WebView::create(NULL)),
        frame_(
            WebLocalFrame::create(blink::WebTreeScopeType::Document, &client_)),
        preload_(MultibufferDataSource::AUTO),
        url_index_(make_linked_ptr(new TestUrlIndex(frame_))) {
    view_->setMainFrame(frame_);
  }

  virtual ~MultibufferDataSourceTest() {
    view_->close();
    frame_->close();
  }

  MOCK_METHOD1(OnInitialize, void(bool));

  void Initialize(const char* url, bool expected) {
    GURL gurl(url);
    data_source_.reset(new MockMultibufferDataSource(
        gurl, message_loop_.task_runner(), url_index_,
        view_->mainFrame()->toWebLocalFrame(), &host_));
    data_source_->SetPreload(preload_);

    response_generator_.reset(new TestResponseGenerator(gurl, kFileSize));
    EXPECT_CALL(*this, OnInitialize(expected));
    data_source_->Initialize(base::Bind(
        &MultibufferDataSourceTest::OnInitialize, base::Unretained(this)));
    message_loop_.RunUntilIdle();

    // Always loading after initialize.
    EXPECT_EQ(data_source_->downloading(), true);
  }

  // Helper to initialize tests with a valid 200 response.
  void InitializeWith200Response() {
    Initialize(kHttpUrl, true);

    EXPECT_CALL(host_, SetTotalBytes(response_generator_->content_length()));
    Respond(response_generator_->Generate200());

    EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize));
    ReceiveData(kDataSize);
  }

  // Helper to initialize tests with a valid 206 response.
  void InitializeWith206Response() {
    Initialize(kHttpUrl, true);

    EXPECT_CALL(host_, SetTotalBytes(response_generator_->content_length()));
    Respond(response_generator_->Generate206(0));
    EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize));
    ReceiveData(kDataSize);
  }

  // Helper to initialize tests with a valid file:// response.
  void InitializeWithFileResponse() {
    Initialize(kFileUrl, true);

    EXPECT_CALL(host_, SetTotalBytes(kFileSize));
    EXPECT_CALL(host_, AddBufferedByteRange(0, kFileSize));
    Respond(response_generator_->GenerateFileResponse(0));

    ReceiveData(kDataSize);
  }

  // Stops any active loaders and shuts down the data source.
  //
  // This typically happens when the page is closed and for our purposes is
  // appropriate to do when tearing down a test.
  void Stop() {
    if (loading()) {
      data_provider()->didFail(url_loader(),
                               response_generator_->GenerateError());
      message_loop_.RunUntilIdle();
    }

    data_source_->Stop();
    message_loop_.RunUntilIdle();
  }

  void Respond(const WebURLResponse& response) {
    EXPECT_TRUE(url_loader());
    if (!active_loader())
      return;
    data_provider()->didReceiveResponse(url_loader(), response);
    message_loop_.RunUntilIdle();
  }

  void ReceiveData(int size) {
    EXPECT_TRUE(url_loader());
    if (!url_loader())
      return;
    scoped_ptr<char[]> data(new char[size]);
    memset(data.get(), 0xA5, size);  // Arbitrary non-zero value.

    data_provider()->didReceiveData(url_loader(), data.get(), size, size);
    message_loop_.RunUntilIdle();
  }

  void FinishLoading() {
    EXPECT_TRUE(url_loader());
    if (!url_loader())
      return;
    data_provider()->didFinishLoading(url_loader(), 0, -1);
    message_loop_.RunUntilIdle();
  }

  void Restart() {
    EXPECT_TRUE(data_provider());
    EXPECT_FALSE(active_loader_allownull());
    if (!data_provider())
      return;
    data_provider()->Start();
  }

  MOCK_METHOD1(ReadCallback, void(int size));

  void ReadAt(int64 position, int64 howmuch = kDataSize) {
    data_source_->Read(position, howmuch, buffer_,
                       base::Bind(&MultibufferDataSourceTest::ReadCallback,
                                  base::Unretained(this)));
    message_loop_.RunUntilIdle();
  }

  void ExecuteMixedResponseSuccessTest(const WebURLResponse& response1,
                                       const WebURLResponse& response2) {
    EXPECT_CALL(host_, SetTotalBytes(kFileSize));
    EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize * 2));
    EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize));
    EXPECT_CALL(*this, ReadCallback(kDataSize)).Times(2);

    Respond(response1);
    ReceiveData(kDataSize);
    ReadAt(0);
    EXPECT_TRUE(loading());

    FinishLoading();
    Restart();
    ReadAt(kDataSize);
    Respond(response2);
    ReceiveData(kDataSize);
    FinishLoading();
    Stop();
  }

  void ExecuteMixedResponseFailureTest(const WebURLResponse& response1,
                                       const WebURLResponse& response2) {
    EXPECT_CALL(host_, SetTotalBytes(kFileSize));
    EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize));
    EXPECT_CALL(*this, ReadCallback(kDataSize));
    EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));

    Respond(response1);
    ReceiveData(kDataSize);
    ReadAt(0);
    EXPECT_TRUE(loading());

    FinishLoading();
    Restart();
    ReadAt(kDataSize);
    Respond(response2);
    Stop();
  }

  void CheckCapacityDefer() {
    EXPECT_EQ(2 << 20, preload_low());
    EXPECT_EQ(3 << 20, preload_high());
  }

  void CheckReadThenDefer() {
    EXPECT_EQ(0, preload_low());
    EXPECT_EQ(0, preload_high());
  }

  void CheckNeverDefer() {
    EXPECT_EQ(1LL << 40, preload_low());
    EXPECT_EQ(1LL << 40, preload_high());
  }

  // Accessors for private variables on |data_source_|.
  MultiBufferReader* loader() { return data_source_->reader_.get(); }

  TestResourceMultiBuffer* multibuffer() {
    return url_index_->last_url_data()->test_multibuffer();
  }

  TestMultiBufferDataProvider* data_provider() {
    return multibuffer()->GetProvider();
  }
  ActiveLoader* active_loader() {
    EXPECT_TRUE(data_provider());
    if (!data_provider())
      return nullptr;
    return data_provider()->active_loader_.get();
  }
  ActiveLoader* active_loader_allownull() {
    TestMultiBufferDataProvider* data_provider =
        multibuffer()->GetProvider_allownull();
    if (!data_provider)
      return nullptr;
    return data_provider->active_loader_.get();
  }
  WebURLLoader* url_loader() {
    EXPECT_TRUE(active_loader());
    if (!active_loader())
      return nullptr;
    return active_loader()->loader_.get();
  }

  bool loading() { return multibuffer()->loading(); }

  MultibufferDataSource::Preload preload() { return data_source_->preload_; }
  void set_preload(MultibufferDataSource::Preload preload) {
    preload_ = preload;
  }
  int64 preload_high() {
    CHECK(loader());
    return loader()->preload_high();
  }
  int64 preload_low() {
    CHECK(loader());
    return loader()->preload_low();
  }
  int data_source_bitrate() { return data_source_->bitrate_; }
  double data_source_playback_rate() { return data_source_->playback_rate_; }
  bool is_local_source() { return data_source_->assume_fully_buffered(); }
  void set_might_be_reused_from_cache_in_future(bool value) {
    data_source_->url_data_->set_cacheable(value);
  }

 protected:
  MockWebFrameClient client_;
  WebView* view_;
  WebLocalFrame* frame_;
  MultibufferDataSource::Preload preload_;
  linked_ptr<TestUrlIndex> url_index_;

  scoped_ptr<MockMultibufferDataSource> data_source_;

  scoped_ptr<TestResponseGenerator> response_generator_;

  StrictMock<MockBufferedDataSourceHost> host_;
  base::MessageLoop message_loop_;

  // Used for calling MultibufferDataSource::Read().
  uint8 buffer_[kDataSize * 2];

  DISALLOW_COPY_AND_ASSIGN(MultibufferDataSourceTest);
};

TEST_F(MultibufferDataSourceTest, Range_Supported) {
  InitializeWith206Response();

  EXPECT_TRUE(loading());
  EXPECT_FALSE(data_source_->IsStreaming());
  Stop();
}

TEST_F(MultibufferDataSourceTest, Range_InstanceSizeUnknown) {
  Initialize(kHttpUrl, true);

  Respond(response_generator_->Generate206(
      0, TestResponseGenerator::kNoContentRangeInstanceSize));

  EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize));
  ReceiveData(kDataSize);

  EXPECT_TRUE(loading());
  EXPECT_TRUE(data_source_->IsStreaming());
  Stop();
}

TEST_F(MultibufferDataSourceTest, Range_NotFound) {
  Initialize(kHttpUrl, false);
  Respond(response_generator_->Generate404());

  EXPECT_FALSE(loading());
  Stop();
}

TEST_F(MultibufferDataSourceTest, Range_NotSupported) {
  InitializeWith200Response();

  EXPECT_TRUE(loading());
  EXPECT_TRUE(data_source_->IsStreaming());
  Stop();
}

// Special carve-out for Apache versions that choose to return a 200 for
// Range:0- ("because it's more efficient" than a 206)
TEST_F(MultibufferDataSourceTest, Range_SupportedButReturned200) {
  Initialize(kHttpUrl, true);
  EXPECT_CALL(host_, SetTotalBytes(response_generator_->content_length()));
  WebURLResponse response = response_generator_->Generate200();
  response.setHTTPHeaderField(WebString::fromUTF8("Accept-Ranges"),
                              WebString::fromUTF8("bytes"));
  Respond(response);

  EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize));
  ReceiveData(kDataSize);

  EXPECT_TRUE(loading());
  EXPECT_FALSE(data_source_->IsStreaming());
  Stop();
}

TEST_F(MultibufferDataSourceTest, Range_MissingContentRange) {
  Initialize(kHttpUrl, false);
  Respond(response_generator_->Generate206(
      0, TestResponseGenerator::kNoContentRange));

  EXPECT_FALSE(loading());
  Stop();
}

TEST_F(MultibufferDataSourceTest, Range_MissingContentLength) {
  Initialize(kHttpUrl, true);

  // It'll manage without a Content-Length response.
  EXPECT_CALL(host_, SetTotalBytes(response_generator_->content_length()));
  Respond(response_generator_->Generate206(
      0, TestResponseGenerator::kNoContentLength));

  EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize));
  ReceiveData(kDataSize);

  EXPECT_TRUE(loading());
  EXPECT_FALSE(data_source_->IsStreaming());
  Stop();
}

TEST_F(MultibufferDataSourceTest, Range_WrongContentRange) {
  Initialize(kHttpUrl, false);

  // Now it's done and will fail.
  Respond(response_generator_->Generate206(1337));

  EXPECT_FALSE(loading());
  Stop();
}

// Test the case where the initial response from the server indicates that
// Range requests are supported, but a later request prove otherwise.
TEST_F(MultibufferDataSourceTest, Range_ServerLied) {
  InitializeWith206Response();

  // Read causing a new request to be made -- we'll expect it to error.
  ReadAt(kFarReadPosition);

  // Return a 200 in response to a range request.
  EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
  Respond(response_generator_->Generate200());

  EXPECT_FALSE(loading());
  Stop();
}

TEST_F(MultibufferDataSourceTest, Http_AbortWhileReading) {
  InitializeWith206Response();

  // Make sure there's a pending read -- we'll expect it to error.
  ReadAt(kFileSize);

  // Abort!!!
  EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
  data_source_->Abort();
  message_loop_.RunUntilIdle();

  EXPECT_FALSE(loading());
  Stop();
}

TEST_F(MultibufferDataSourceTest, File_AbortWhileReading) {
  InitializeWithFileResponse();

  // Make sure there's a pending read -- we'll expect it to error.
  ReadAt(kFileSize);

  // Abort!!!
  EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
  data_source_->Abort();
  message_loop_.RunUntilIdle();

  EXPECT_FALSE(loading());
  Stop();
}

TEST_F(MultibufferDataSourceTest, Http_Retry) {
  InitializeWith206Response();

  // Read to advance our position.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  ReadAt(0);

  // Issue a pending read but terminate the connection to force a retry.
  ReadAt(kDataSize);
  FinishLoading();
  Restart();
  Respond(response_generator_->Generate206(kDataSize));

  // Complete the read.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize * 2));
  ReceiveData(kDataSize);

  EXPECT_TRUE(loading());
  Stop();
}

TEST_F(MultibufferDataSourceTest, Http_RetryOnError) {
  InitializeWith206Response();

  // Read to advance our position.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  ReadAt(0);

  // Issue a pending read but trigger an error to force a retry.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize * 2));
  ReadAt(kDataSize);
  base::RunLoop run_loop;
  data_provider()->didFail(url_loader(), response_generator_->GenerateError());
  data_provider()->RunOnStart(run_loop.QuitClosure());
  run_loop.Run();
  Respond(response_generator_->Generate206(kDataSize));
  ReceiveData(kDataSize);
  FinishLoading();
  EXPECT_FALSE(loading());
  Stop();
}

// Make sure that we prefetch across partial responses. (crbug.com/516589)
TEST_F(MultibufferDataSourceTest, Http_PartialResponsePrefetch) {
  Initialize(kHttpUrl, true);
  WebURLResponse response1 =
      response_generator_->GeneratePartial206(0, kDataSize - 1);
  WebURLResponse response2 =
      response_generator_->GeneratePartial206(kDataSize, kDataSize * 3 - 1);

  EXPECT_CALL(host_, SetTotalBytes(kFileSize));
  EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize * 3));
  EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize * 2));
  EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize));
  EXPECT_CALL(*this, ReadCallback(kDataSize));

  Respond(response1);
  ReceiveData(kDataSize);
  ReadAt(0);
  EXPECT_TRUE(loading());

  FinishLoading();
  Restart();
  Respond(response2);
  ReceiveData(kDataSize);
  ReceiveData(kDataSize);
  FinishLoading();
  Stop();
}

TEST_F(MultibufferDataSourceTest, Http_PartialResponse) {
  Initialize(kHttpUrl, true);
  WebURLResponse response1 =
      response_generator_->GeneratePartial206(0, kDataSize - 1);
  WebURLResponse response2 =
      response_generator_->GeneratePartial206(kDataSize, kDataSize * 2 - 1);
  // The origin URL of response1 and response2 are same. So no error should
  // occur.
  ExecuteMixedResponseSuccessTest(response1, response2);
}

TEST_F(MultibufferDataSourceTest,
       Http_MixedResponse_RedirectedToDifferentPathResponse) {
  Initialize(kHttpUrl, true);
  WebURLResponse response1 =
      response_generator_->GeneratePartial206(0, kDataSize - 1);
  WebURLResponse response2 =
      response_generator_->GeneratePartial206(kDataSize, kDataSize * 2 - 1);
  response2.setURL(GURL(kHttpDifferentPathUrl));
  // The origin URL of response1 and response2 are same. So no error should
  // occur.
  ExecuteMixedResponseSuccessTest(response1, response2);
}

TEST_F(MultibufferDataSourceTest,
       Http_MixedResponse_RedirectedToDifferentOriginResponse) {
  Initialize(kHttpUrl, true);
  WebURLResponse response1 =
      response_generator_->GeneratePartial206(0, kDataSize - 1);
  WebURLResponse response2 =
      response_generator_->GeneratePartial206(kDataSize, kDataSize * 2 - 1);
  response2.setURL(GURL(kHttpDifferentOriginUrl));
  // The origin URL of response1 and response2 are different. So an error should
  // occur.
  ExecuteMixedResponseFailureTest(response1, response2);
}

TEST_F(MultibufferDataSourceTest,
       Http_MixedResponse_ServiceWorkerGeneratedResponseAndNormalResponse) {
  Initialize(kHttpUrl, true);
  WebURLResponse response1 =
      response_generator_->GeneratePartial206(0, kDataSize - 1);
  response1.setWasFetchedViaServiceWorker(true);
  WebURLResponse response2 =
      response_generator_->GeneratePartial206(kDataSize, kDataSize * 2 - 1);
  // response1 is generated in a Service Worker but response2 is from a native
  // server. So an error should occur.
  ExecuteMixedResponseFailureTest(response1, response2);
}

TEST_F(MultibufferDataSourceTest,
       Http_MixedResponse_ServiceWorkerProxiedAndSameURLResponse) {
  Initialize(kHttpUrl, true);
  WebURLResponse response1 =
      response_generator_->GeneratePartial206(0, kDataSize - 1);
  response1.setWasFetchedViaServiceWorker(true);
  response1.setOriginalURLViaServiceWorker(GURL(kHttpUrl));
  WebURLResponse response2 =
      response_generator_->GeneratePartial206(kDataSize, kDataSize * 2 - 1);
  // The origin URL of response1 and response2 are same. So no error should
  // occur.
  ExecuteMixedResponseSuccessTest(response1, response2);
}

TEST_F(MultibufferDataSourceTest,
       Http_MixedResponse_ServiceWorkerProxiedAndDifferentPathResponse) {
  Initialize(kHttpUrl, true);
  WebURLResponse response1 =
      response_generator_->GeneratePartial206(0, kDataSize - 1);
  response1.setWasFetchedViaServiceWorker(true);
  response1.setOriginalURLViaServiceWorker(GURL(kHttpDifferentPathUrl));
  WebURLResponse response2 =
      response_generator_->GeneratePartial206(kDataSize, kDataSize * 2 - 1);
  // The origin URL of response1 and response2 are same. So no error should
  // occur.
  ExecuteMixedResponseSuccessTest(response1, response2);
}

TEST_F(MultibufferDataSourceTest,
       Http_MixedResponse_ServiceWorkerProxiedAndDifferentOriginResponse) {
  Initialize(kHttpUrl, true);
  WebURLResponse response1 =
      response_generator_->GeneratePartial206(0, kDataSize - 1);
  response1.setWasFetchedViaServiceWorker(true);
  response1.setOriginalURLViaServiceWorker(GURL(kHttpDifferentOriginUrl));
  WebURLResponse response2 =
      response_generator_->GeneratePartial206(kDataSize, kDataSize * 2 - 1);
  // The origin URL of response1 and response2 are different. So an error should
  // occur.
  ExecuteMixedResponseFailureTest(response1, response2);
}

TEST_F(MultibufferDataSourceTest, File_Retry) {
  InitializeWithFileResponse();

  // Read to advance our position.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  ReadAt(0);

  // Issue a pending read but terminate the connection to force a retry.
  ReadAt(kDataSize);
  FinishLoading();
  Restart();
  Respond(response_generator_->GenerateFileResponse(kDataSize));

  // Complete the read.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  ReceiveData(kDataSize);

  EXPECT_TRUE(loading());
  Stop();
}

TEST_F(MultibufferDataSourceTest, Http_TooManyRetries) {
  InitializeWith206Response();

  // Make sure there's a pending read -- we'll expect it to error.
  ReadAt(kDataSize);

  // It'll try three times.
  FinishLoading();
  Restart();
  Respond(response_generator_->Generate206(kDataSize));

  FinishLoading();
  Restart();
  Respond(response_generator_->Generate206(kDataSize));

  FinishLoading();
  Restart();
  Respond(response_generator_->Generate206(kDataSize));

  // It'll error after this.
  EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
  FinishLoading();

  EXPECT_FALSE(loading());
  Stop();
}

TEST_F(MultibufferDataSourceTest, File_TooManyRetries) {
  InitializeWithFileResponse();

  // Make sure there's a pending read -- we'll expect it to error.
  ReadAt(kDataSize);

  // It'll try three times.
  FinishLoading();
  Restart();
  Respond(response_generator_->GenerateFileResponse(0));

  FinishLoading();
  Restart();
  Respond(response_generator_->GenerateFileResponse(0));

  FinishLoading();
  Restart();
  Respond(response_generator_->GenerateFileResponse(0));

  // It'll error after this.
  EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
  FinishLoading();

  EXPECT_FALSE(loading());
  Stop();
}

TEST_F(MultibufferDataSourceTest, File_InstanceSizeUnknown) {
  Initialize(kFileUrl, false);

  Respond(
      response_generator_->GenerateFileResponse(media::DataSource::kReadError));
  ReceiveData(kDataSize);

  EXPECT_FALSE(data_source_->downloading());
  EXPECT_FALSE(loading());
  Stop();
}

TEST_F(MultibufferDataSourceTest, File_Successful) {
  InitializeWithFileResponse();

  EXPECT_TRUE(loading());
  EXPECT_FALSE(data_source_->IsStreaming());
  Stop();
}

TEST_F(MultibufferDataSourceTest, StopDuringRead) {
  InitializeWith206Response();

  uint8 buffer[256];
  data_source_->Read(0, arraysize(buffer), buffer,
                     base::Bind(&MultibufferDataSourceTest::ReadCallback,
                                base::Unretained(this)));

  // The outstanding read should fail before the stop callback runs.
  {
    InSequence s;
    EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
    data_source_->Stop();
  }
  message_loop_.RunUntilIdle();
}

TEST_F(MultibufferDataSourceTest, DefaultValues) {
  InitializeWith206Response();

  // Ensure we have sane values for default loading scenario.
  EXPECT_EQ(MultibufferDataSource::AUTO, preload());
  EXPECT_EQ(2 << 20, preload_low());
  EXPECT_EQ(3 << 20, preload_high());

  EXPECT_EQ(0, data_source_bitrate());
  EXPECT_EQ(0.0, data_source_playback_rate());

  EXPECT_TRUE(loading());
  Stop();
}

TEST_F(MultibufferDataSourceTest, SetBitrate) {
  InitializeWith206Response();

  data_source_->SetBitrate(1234);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(1234, data_source_bitrate());

  // Read so far ahead to cause the loader to get recreated.
  TestMultiBufferDataProvider* old_loader = data_provider();
  ReadAt(kFarReadPosition);
  Respond(response_generator_->Generate206(kFarReadPosition));

  // Verify loader changed but still has same bitrate.
  EXPECT_NE(old_loader, data_provider());

  EXPECT_TRUE(loading());
  EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
  Stop();
}

TEST_F(MultibufferDataSourceTest, MediaPlaybackRateChanged) {
  InitializeWith206Response();

  data_source_->MediaPlaybackRateChanged(2.0);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(2.0, data_source_playback_rate());

  // Read so far ahead to cause the loader to get recreated.
  TestMultiBufferDataProvider* old_loader = data_provider();
  ReadAt(kFarReadPosition);
  Respond(response_generator_->Generate206(kFarReadPosition));

  // Verify loader changed but still has same playback rate.
  EXPECT_NE(old_loader, data_provider());

  EXPECT_TRUE(loading());
  EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
  Stop();
}

TEST_F(MultibufferDataSourceTest, Http_Read) {
  InitializeWith206Response();

  EXPECT_CALL(*this, ReadCallback(kDataSize));
  ReadAt(0, kDataSize * 2);

  ReadAt(kDataSize, kDataSize);
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize * 2));
  ReceiveData(kDataSize / 2);
  ReceiveData(kDataSize / 2);

  EXPECT_TRUE(data_source_->downloading());
  Stop();
}

TEST_F(MultibufferDataSourceTest, Http_ShareData) {
  InitializeWith206Response();

  EXPECT_CALL(*this, ReadCallback(kDataSize));
  ReadAt(0, kDataSize * 2);

  ReadAt(kDataSize, kDataSize);
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize * 2));
  ReceiveData(kDataSize / 2);
  ReceiveData(kDataSize / 2);

  EXPECT_TRUE(data_source_->downloading());

  StrictMock<MockBufferedDataSourceHost> host2;
  MockMultibufferDataSource source2(
      GURL(kHttpUrl), message_loop_.task_runner(), url_index_,
      view_->mainFrame()->toWebLocalFrame(), &host2);
  source2.SetPreload(preload_);

  EXPECT_CALL(*this, OnInitialize(true));

  // This call would not be expected if we were not sharing data.
  EXPECT_CALL(host2, SetTotalBytes(response_generator_->content_length()));
  source2.Initialize(base::Bind(&MultibufferDataSourceTest::OnInitialize,
                                base::Unretained(this)));
  message_loop_.RunUntilIdle();

  // Always loading after initialize.
  EXPECT_EQ(source2.downloading(), true);

  Stop();
}

TEST_F(MultibufferDataSourceTest, Http_Read_Seek) {
  InitializeWith206Response();

  // Read a bit from the beginning.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  ReadAt(0);

  // Simulate a seek by reading a bit beyond kDataSize.
  ReadAt(kDataSize * 2);

  // We receive data leading up to but not including our read.
  // No notification will happen, since it's progress outside
  // of our current range.
  ReceiveData(kDataSize);

  // We now receive the rest of the data for our read.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize * 3));
  ReceiveData(kDataSize);

  EXPECT_TRUE(data_source_->downloading());
  Stop();
}

TEST_F(MultibufferDataSourceTest, File_Read) {
  InitializeWithFileResponse();

  EXPECT_CALL(*this, ReadCallback(kDataSize));
  ReadAt(0, kDataSize * 2);

  ReadAt(kDataSize, kDataSize);
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  ReceiveData(kDataSize);

  Stop();
}

TEST_F(MultibufferDataSourceTest, Http_FinishLoading) {
  InitializeWith206Response();

  EXPECT_TRUE(data_source_->downloading());
  // premature didFinishLoading() will cause a retry.
  FinishLoading();
  EXPECT_TRUE(data_source_->downloading());

  Stop();
}

TEST_F(MultibufferDataSourceTest, File_FinishLoading) {
  InitializeWithFileResponse();

  ReceiveData(kDataSize);

  EXPECT_TRUE(data_source_->downloading());
  // premature didFinishLoading() will cause a retry.
  FinishLoading();
  EXPECT_TRUE(data_source_->downloading());

  Stop();
}

TEST_F(MultibufferDataSourceTest, LocalResource_DeferStrategy) {
  InitializeWithFileResponse();

  EXPECT_EQ(MultibufferDataSource::AUTO, preload());
  EXPECT_TRUE(is_local_source());
  CheckCapacityDefer();

  data_source_->MediaIsPlaying();
  CheckCapacityDefer();

  data_source_->MediaIsPaused();
  CheckCapacityDefer();

  Stop();
}

TEST_F(MultibufferDataSourceTest, LocalResource_PreloadMetadata_DeferStrategy) {
  set_preload(MultibufferDataSource::METADATA);
  InitializeWithFileResponse();

  EXPECT_EQ(MultibufferDataSource::METADATA, preload());
  EXPECT_TRUE(is_local_source());
  CheckReadThenDefer();

  data_source_->MediaIsPlaying();
  CheckCapacityDefer();

  data_source_->MediaIsPaused();
  CheckCapacityDefer();

  Stop();
}

TEST_F(MultibufferDataSourceTest, ExternalResource_Reponse200_DeferStrategy) {
  InitializeWith200Response();

  EXPECT_EQ(MultibufferDataSource::AUTO, preload());
  EXPECT_FALSE(is_local_source());
  EXPECT_FALSE(data_source_->range_supported());
  CheckCapacityDefer();

  data_source_->MediaIsPlaying();
  CheckCapacityDefer();

  data_source_->MediaIsPaused();
  CheckCapacityDefer();

  Stop();
}

TEST_F(MultibufferDataSourceTest,
       ExternalResource_Response200_PreloadMetadata_DeferStrategy) {
  set_preload(MultibufferDataSource::METADATA);
  InitializeWith200Response();

  EXPECT_EQ(MultibufferDataSource::METADATA, preload());
  EXPECT_FALSE(is_local_source());
  EXPECT_FALSE(data_source_->range_supported());
  CheckReadThenDefer();

  data_source_->MediaIsPlaying();
  CheckCapacityDefer();

  data_source_->MediaIsPaused();
  CheckCapacityDefer();

  Stop();
}

TEST_F(MultibufferDataSourceTest, ExternalResource_Reponse206_DeferStrategy) {
  InitializeWith206Response();

  EXPECT_EQ(MultibufferDataSource::AUTO, preload());
  EXPECT_FALSE(is_local_source());
  EXPECT_TRUE(data_source_->range_supported());
  CheckCapacityDefer();

  data_source_->MediaIsPlaying();
  CheckCapacityDefer();
  set_might_be_reused_from_cache_in_future(true);
  data_source_->MediaIsPaused();
  CheckNeverDefer();

  data_source_->MediaIsPlaying();
  CheckCapacityDefer();

  set_might_be_reused_from_cache_in_future(false);
  data_source_->MediaIsPaused();
  CheckCapacityDefer();

  Stop();
}

TEST_F(MultibufferDataSourceTest,
       ExternalResource_Response206_PreloadMetadata_DeferStrategy) {
  set_preload(MultibufferDataSource::METADATA);
  InitializeWith206Response();

  EXPECT_EQ(MultibufferDataSource::METADATA, preload());
  EXPECT_FALSE(is_local_source());
  EXPECT_TRUE(data_source_->range_supported());
  CheckReadThenDefer();

  data_source_->MediaIsPlaying();
  CheckCapacityDefer();

  set_might_be_reused_from_cache_in_future(true);
  data_source_->MediaIsPaused();
  CheckNeverDefer();

  data_source_->MediaIsPlaying();
  CheckCapacityDefer();
  set_might_be_reused_from_cache_in_future(false);
  data_source_->MediaIsPaused();
  CheckCapacityDefer();

  Stop();
}

TEST_F(MultibufferDataSourceTest, ExternalResource_Response206_VerifyDefer) {
  set_preload(MultibufferDataSource::METADATA);
  InitializeWith206Response();

  EXPECT_EQ(MultibufferDataSource::METADATA, preload());
  EXPECT_FALSE(is_local_source());
  EXPECT_TRUE(data_source_->range_supported());
  CheckReadThenDefer();

  // Read a bit from the beginning.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  ReadAt(0);

  ASSERT_TRUE(active_loader());
  EXPECT_TRUE(active_loader()->deferred());
}

TEST_F(MultibufferDataSourceTest,
       ExternalResource_Response206_CancelAfterDefer) {
  set_preload(MultibufferDataSource::METADATA);
  InitializeWith206Response();

  EXPECT_EQ(MultibufferDataSource::METADATA, preload());
  EXPECT_FALSE(is_local_source());

  EXPECT_TRUE(data_source_->range_supported());
  CheckReadThenDefer();

  ReadAt(kDataSize);

  data_source_->OnBufferingHaveEnough();
  ASSERT_TRUE(active_loader());

  EXPECT_CALL(*this, ReadCallback(kDataSize));
  EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize * 2));
  ReceiveData(kDataSize);

  EXPECT_FALSE(active_loader_allownull());
}

}  // namespace media
