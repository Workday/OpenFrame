// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_util.h"
#include "base/memory/ref_counted_memory.h"
#include "base/run_loop.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/tracing_controller.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"

using base::trace_event::RECORD_CONTINUOUSLY;
using base::trace_event::RECORD_UNTIL_FULL;
using base::trace_event::TraceConfig;

namespace content {

class TracingControllerTestEndpoint
    : public TracingController::TraceDataEndpoint {
 public:
  TracingControllerTestEndpoint(
      base::Callback<void(scoped_ptr<const base::DictionaryValue>,
                          base::RefCountedString*)> done_callback)
      : done_callback_(done_callback) {}

  void ReceiveTraceChunk(const std::string& chunk) override {
    EXPECT_FALSE(chunk.empty());
    trace_ += chunk;
  }

  void ReceiveTraceFinalContents(
      scoped_ptr<const base::DictionaryValue> metadata,
      const std::string& contents) override {
    EXPECT_EQ(trace_, contents);

    std::string tmp = contents;
    scoped_refptr<base::RefCountedString> chunk_ptr =
        base::RefCountedString::TakeString(&tmp);

    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(done_callback_, base::Passed(metadata.Pass()), chunk_ptr));
  }

 protected:
  ~TracingControllerTestEndpoint() override {}

  std::string trace_;
  base::Callback<void(scoped_ptr<const base::DictionaryValue>,
                      base::RefCountedString*)> done_callback_;
};

class TracingControllerTest : public ContentBrowserTest {
 public:
  TracingControllerTest() {}

  void SetUp() override {
    get_categories_done_callback_count_ = 0;
    enable_recording_done_callback_count_ = 0;
    disable_recording_done_callback_count_ = 0;
    enable_monitoring_done_callback_count_ = 0;
    disable_monitoring_done_callback_count_ = 0;
    capture_monitoring_snapshot_done_callback_count_ = 0;
    ContentBrowserTest::SetUp();
  }

  void TearDown() override { ContentBrowserTest::TearDown(); }

  void Navigate(Shell* shell) {
    NavigateToURL(shell, GetTestUrl("", "title.html"));
  }

  void GetCategoriesDoneCallbackTest(base::Closure quit_callback,
                                     const std::set<std::string>& categories) {
    get_categories_done_callback_count_++;
    EXPECT_TRUE(categories.size() > 0);
    quit_callback.Run();
  }

  void StartTracingDoneCallbackTest(base::Closure quit_callback) {
    enable_recording_done_callback_count_++;
    quit_callback.Run();
  }

  void StopTracingStringDoneCallbackTest(
      base::Closure quit_callback,
      scoped_ptr<const base::DictionaryValue> metadata,
      base::RefCountedString* data) {
    disable_recording_done_callback_count_++;
    last_metadata_.reset(metadata.release());
    EXPECT_TRUE(data->size() > 0);
    quit_callback.Run();
  }

  void StopTracingFileDoneCallbackTest(base::Closure quit_callback,
                                            const base::FilePath& file_path) {
    disable_recording_done_callback_count_++;
    EXPECT_TRUE(PathExists(file_path));
    int64 file_size;
    base::GetFileSize(file_path, &file_size);
    EXPECT_TRUE(file_size > 0);
    quit_callback.Run();
    last_actual_recording_file_path_ = file_path;
  }

  void StartMonitoringDoneCallbackTest(base::Closure quit_callback) {
    enable_monitoring_done_callback_count_++;
    quit_callback.Run();
  }

  void StopMonitoringDoneCallbackTest(base::Closure quit_callback) {
    disable_monitoring_done_callback_count_++;
    quit_callback.Run();
  }

  void CaptureMonitoringSnapshotDoneCallbackTest(
      base::Closure quit_callback, const base::FilePath& file_path) {
    capture_monitoring_snapshot_done_callback_count_++;
    EXPECT_TRUE(PathExists(file_path));
    int64 file_size;
    base::GetFileSize(file_path, &file_size);
    EXPECT_TRUE(file_size > 0);
    quit_callback.Run();
    last_actual_monitoring_file_path_ = file_path;
  }

  int get_categories_done_callback_count() const {
    return get_categories_done_callback_count_;
  }

  int enable_recording_done_callback_count() const {
    return enable_recording_done_callback_count_;
  }

  int disable_recording_done_callback_count() const {
    return disable_recording_done_callback_count_;
  }

  int enable_monitoring_done_callback_count() const {
    return enable_monitoring_done_callback_count_;
  }

  int disable_monitoring_done_callback_count() const {
    return disable_monitoring_done_callback_count_;
  }

  int capture_monitoring_snapshot_done_callback_count() const {
    return capture_monitoring_snapshot_done_callback_count_;
  }

  base::FilePath last_actual_recording_file_path() const {
    return last_actual_recording_file_path_;
  }

  base::FilePath last_actual_monitoring_file_path() const {
    return last_actual_monitoring_file_path_;
  }

  const base::DictionaryValue* last_metadata() const {
    return last_metadata_.get();
  }

  void TestStartAndStopTracingString() {
    Navigate(shell());

    TracingController* controller = TracingController::GetInstance();

    {
      base::RunLoop run_loop;
      TracingController::StartTracingDoneCallback callback =
          base::Bind(&TracingControllerTest::StartTracingDoneCallbackTest,
                     base::Unretained(this),
                     run_loop.QuitClosure());
      bool result = controller->StartTracing(
          TraceConfig(), callback);
      ASSERT_TRUE(result);
      run_loop.Run();
      EXPECT_EQ(enable_recording_done_callback_count(), 1);
    }

    {
      base::RunLoop run_loop;
      base::Callback<void(scoped_ptr<const base::DictionaryValue>,
                          base::RefCountedString*)> callback = base::Bind(
          &TracingControllerTest::StopTracingStringDoneCallbackTest,
          base::Unretained(this),
          run_loop.QuitClosure());
      bool result = controller->StopTracing(
          TracingController::CreateStringSink(callback));
      ASSERT_TRUE(result);
      run_loop.Run();
      EXPECT_EQ(disable_recording_done_callback_count(), 1);
    }
  }

  void TestStartAndStopTracingCompressed() {
    Navigate(shell());

    TracingController* controller = TracingController::GetInstance();

    {
      base::RunLoop run_loop;
      TracingController::StartTracingDoneCallback callback =
          base::Bind(&TracingControllerTest::StartTracingDoneCallbackTest,
                     base::Unretained(this), run_loop.QuitClosure());
      bool result = controller->StartTracing(TraceConfig(), callback);
      ASSERT_TRUE(result);
      run_loop.Run();
      EXPECT_EQ(enable_recording_done_callback_count(), 1);
    }

    {
      base::RunLoop run_loop;
      base::Callback<void(scoped_ptr<const base::DictionaryValue>,
                          base::RefCountedString*)> callback = base::Bind(
          &TracingControllerTest::StopTracingStringDoneCallbackTest,
          base::Unretained(this), run_loop.QuitClosure());
      bool result = controller->StopTracing(
          TracingController::CreateCompressedStringSink(
              new TracingControllerTestEndpoint(callback)));
      ASSERT_TRUE(result);
      run_loop.Run();
      EXPECT_EQ(disable_recording_done_callback_count(), 1);
    }
  }

  void TestStartAndStopTracingCompressedFile(
      const base::FilePath& result_file_path) {
    Navigate(shell());

    TracingController* controller = TracingController::GetInstance();

    {
      base::RunLoop run_loop;
      TracingController::StartTracingDoneCallback callback =
          base::Bind(&TracingControllerTest::StartTracingDoneCallbackTest,
                     base::Unretained(this), run_loop.QuitClosure());
      bool result = controller->StartTracing(TraceConfig(), callback);
      ASSERT_TRUE(result);
      run_loop.Run();
      EXPECT_EQ(enable_recording_done_callback_count(), 1);
    }

    {
      base::RunLoop run_loop;
      base::Closure callback = base::Bind(
          &TracingControllerTest::StopTracingFileDoneCallbackTest,
          base::Unretained(this), run_loop.QuitClosure(), result_file_path);
      bool result = controller->StopTracing(
          TracingController::CreateCompressedStringSink(
              TracingController::CreateFileEndpoint(result_file_path,
                                                    callback)));
      ASSERT_TRUE(result);
      run_loop.Run();
      EXPECT_EQ(disable_recording_done_callback_count(), 1);
    }
  }

  void TestStartAndStopTracingFile(
      const base::FilePath& result_file_path) {
    Navigate(shell());

    TracingController* controller = TracingController::GetInstance();

    {
      base::RunLoop run_loop;
      TracingController::StartTracingDoneCallback callback =
          base::Bind(&TracingControllerTest::StartTracingDoneCallbackTest,
                     base::Unretained(this),
                     run_loop.QuitClosure());
      bool result = controller->StartTracing(TraceConfig(), callback);
      ASSERT_TRUE(result);
      run_loop.Run();
      EXPECT_EQ(enable_recording_done_callback_count(), 1);
    }

    {
      base::RunLoop run_loop;
      base::Closure callback = base::Bind(
          &TracingControllerTest::StopTracingFileDoneCallbackTest,
          base::Unretained(this),
          run_loop.QuitClosure(),
          result_file_path);
      bool result = controller->StopTracing(
          TracingController::CreateFileSink(result_file_path, callback));
      ASSERT_TRUE(result);
      run_loop.Run();
      EXPECT_EQ(disable_recording_done_callback_count(), 1);
    }
  }

  void TestEnableCaptureAndStopMonitoring(
      const base::FilePath& result_file_path) {
    Navigate(shell());

    TracingController* controller = TracingController::GetInstance();

    {
      bool is_monitoring;
      TraceConfig trace_config("", "");
      controller->GetMonitoringStatus(
          &is_monitoring, &trace_config);
      EXPECT_FALSE(is_monitoring);
      EXPECT_EQ("-*Debug,-*Test", trace_config.ToCategoryFilterString());
      EXPECT_FALSE(trace_config.GetTraceRecordMode() == RECORD_CONTINUOUSLY);
      EXPECT_FALSE(trace_config.IsSamplingEnabled());
      EXPECT_FALSE(trace_config.IsSystraceEnabled());
    }

    {
      base::RunLoop run_loop;
      TracingController::StartMonitoringDoneCallback callback =
          base::Bind(&TracingControllerTest::StartMonitoringDoneCallbackTest,
                     base::Unretained(this),
                     run_loop.QuitClosure());

      TraceConfig trace_config("*", "");
      trace_config.EnableSampling();
      bool result = controller->StartMonitoring(trace_config, callback);
      ASSERT_TRUE(result);
      run_loop.Run();
      EXPECT_EQ(enable_monitoring_done_callback_count(), 1);
    }

    {
      bool is_monitoring;
      TraceConfig trace_config("", "");
      controller->GetMonitoringStatus(&is_monitoring, &trace_config);
      EXPECT_TRUE(is_monitoring);
      EXPECT_EQ("*", trace_config.ToCategoryFilterString());
      EXPECT_FALSE(trace_config.GetTraceRecordMode() == RECORD_CONTINUOUSLY);
      EXPECT_TRUE(trace_config.IsSamplingEnabled());
      EXPECT_FALSE(trace_config.IsSystraceEnabled());
    }

    {
      base::RunLoop run_loop;
      base::Closure callback = base::Bind(
          &TracingControllerTest::CaptureMonitoringSnapshotDoneCallbackTest,
          base::Unretained(this),
          run_loop.QuitClosure(),
          result_file_path);
      ASSERT_TRUE(controller->CaptureMonitoringSnapshot(
          TracingController::CreateFileSink(result_file_path, callback)));
      run_loop.Run();
      EXPECT_EQ(capture_monitoring_snapshot_done_callback_count(), 1);
    }

    {
      base::RunLoop run_loop;
      TracingController::StopMonitoringDoneCallback callback =
          base::Bind(&TracingControllerTest::StopMonitoringDoneCallbackTest,
                     base::Unretained(this),
                     run_loop.QuitClosure());
      bool result = controller->StopMonitoring(callback);
      ASSERT_TRUE(result);
      run_loop.Run();
      EXPECT_EQ(disable_monitoring_done_callback_count(), 1);
    }

    {
      bool is_monitoring;
      TraceConfig trace_config("", "");
      controller->GetMonitoringStatus(&is_monitoring, &trace_config);
      EXPECT_FALSE(is_monitoring);
      EXPECT_EQ("", trace_config.ToCategoryFilterString());
      EXPECT_FALSE(trace_config.GetTraceRecordMode() == RECORD_CONTINUOUSLY);
      EXPECT_FALSE(trace_config.IsSamplingEnabled());
      EXPECT_FALSE(trace_config.IsSystraceEnabled());
    }
  }

 private:
  int get_categories_done_callback_count_;
  int enable_recording_done_callback_count_;
  int disable_recording_done_callback_count_;
  int enable_monitoring_done_callback_count_;
  int disable_monitoring_done_callback_count_;
  int capture_monitoring_snapshot_done_callback_count_;
  base::FilePath last_actual_recording_file_path_;
  base::FilePath last_actual_monitoring_file_path_;
  scoped_ptr<const base::DictionaryValue> last_metadata_;
};

IN_PROC_BROWSER_TEST_F(TracingControllerTest, GetCategories) {
  Navigate(shell());

  TracingController* controller = TracingController::GetInstance();

  base::RunLoop run_loop;
  TracingController::GetCategoriesDoneCallback callback =
      base::Bind(&TracingControllerTest::GetCategoriesDoneCallbackTest,
                 base::Unretained(this),
                 run_loop.QuitClosure());
  ASSERT_TRUE(controller->GetCategories(callback));
  run_loop.Run();
  EXPECT_EQ(get_categories_done_callback_count(), 1);
}

IN_PROC_BROWSER_TEST_F(TracingControllerTest, EnableAndStopTracing) {
  TestStartAndStopTracingString();
}

IN_PROC_BROWSER_TEST_F(TracingControllerTest, DisableRecordingStoresMetadata) {
  TestStartAndStopTracingString();
  // Check that a number of important keys exist in the metadata dictionary. The
  // values are not checked to ensure the test is robust.
  EXPECT_TRUE(last_metadata() != NULL);
  std::string network_type;
  last_metadata()->GetString("network-type", &network_type);
  EXPECT_TRUE(network_type.length() > 0);
  std::string user_agent;
  last_metadata()->GetString("user-agent", &user_agent);
  EXPECT_TRUE(user_agent.length() > 0);
  std::string os_name;
  last_metadata()->GetString("os-name", &os_name);
  EXPECT_TRUE(os_name.length() > 0);
  std::string cpu_brand;
  last_metadata()->GetString("cpu-brand", &cpu_brand);
  EXPECT_TRUE(cpu_brand.length() > 0);
}

IN_PROC_BROWSER_TEST_F(TracingControllerTest,
                       EnableAndStopTracingWithFilePath) {
  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  TestStartAndStopTracingFile(file_path);
  EXPECT_EQ(file_path.value(), last_actual_recording_file_path().value());
}

IN_PROC_BROWSER_TEST_F(TracingControllerTest,
                       EnableAndStopTracingWithCompression) {
  TestStartAndStopTracingCompressed();
}

IN_PROC_BROWSER_TEST_F(TracingControllerTest,
                       EnableAndStopTracingToFileWithCompression) {
  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  TestStartAndStopTracingCompressedFile(file_path);
  EXPECT_EQ(file_path.value(), last_actual_recording_file_path().value());
}

IN_PROC_BROWSER_TEST_F(TracingControllerTest,
                       EnableAndStopTracingWithEmptyFileAndNullCallback) {
  Navigate(shell());

  TracingController* controller = TracingController::GetInstance();
  EXPECT_TRUE(controller->StartTracing(
      TraceConfig(),
      TracingController::StartTracingDoneCallback()));
  EXPECT_TRUE(controller->StopTracing(NULL));
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(TracingControllerTest,
                       EnableCaptureAndStopMonitoring) {
  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  TestEnableCaptureAndStopMonitoring(file_path);
}

IN_PROC_BROWSER_TEST_F(TracingControllerTest,
                       EnableCaptureAndStopMonitoringWithFilePath) {
  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  TestEnableCaptureAndStopMonitoring(file_path);
  EXPECT_EQ(file_path.value(), last_actual_monitoring_file_path().value());
}

// See http://crbug.com/392446
#if defined(OS_ANDROID)
#define MAYBE_EnableCaptureAndStopMonitoringWithEmptyFileAndNullCallback \
    DISABLED_EnableCaptureAndStopMonitoringWithEmptyFileAndNullCallback
#else
#define MAYBE_EnableCaptureAndStopMonitoringWithEmptyFileAndNullCallback \
    EnableCaptureAndStopMonitoringWithEmptyFileAndNullCallback
#endif
IN_PROC_BROWSER_TEST_F(
    TracingControllerTest,
    MAYBE_EnableCaptureAndStopMonitoringWithEmptyFileAndNullCallback) {
  Navigate(shell());

  TracingController* controller = TracingController::GetInstance();
  TraceConfig trace_config("*", "");
  trace_config.EnableSampling();
  EXPECT_TRUE(controller->StartMonitoring(
      trace_config,
      TracingController::StartMonitoringDoneCallback()));
  controller->CaptureMonitoringSnapshot(NULL);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(controller->StopMonitoring(
      TracingController::StopMonitoringDoneCallback()));
  base::RunLoop().RunUntilIdle();
}

}  // namespace content
