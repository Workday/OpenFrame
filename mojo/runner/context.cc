// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/runner/context.h"

#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/i18n/icu_util.h"
#include "base/lazy_instance.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/process/process_info.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "components/devtools_service/public/cpp/switches.h"
#include "components/devtools_service/public/interfaces/devtools_service.mojom.h"
#include "components/tracing/tracing_switches.h"
#include "mojo/application/public/cpp/application_connection.h"
#include "mojo/application/public/cpp/application_delegate.h"
#include "mojo/application/public/cpp/application_impl.h"
#include "mojo/package_manager/package_manager_impl.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/runner/host/in_process_native_runner.h"
#include "mojo/runner/host/out_of_process_native_runner.h"
#include "mojo/runner/register_local_aliases.h"
#include "mojo/runner/switches.h"
#include "mojo/runner/tracer.h"
#include "mojo/services/tracing/public/cpp/switches.h"
#include "mojo/services/tracing/public/cpp/trace_provider_impl.h"
#include "mojo/services/tracing/public/cpp/tracing_impl.h"
#include "mojo/services/tracing/public/interfaces/tracing.mojom.h"
#include "mojo/shell/application_loader.h"
#include "mojo/shell/connect_to_application_params.h"
#include "mojo/shell/query_util.h"
#include "mojo/shell/switches.h"
#include "mojo/util/filename_util.h"
#include "third_party/mojo/src/mojo/edk/embedder/embedder.h"
#include "url/gurl.h"

namespace mojo {
namespace runner {
namespace {

// Used to ensure we only init once.
class Setup {
 public:
  Setup() {
    embedder::PreInitializeParentProcess();
    embedder::Init();
  }

  ~Setup() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(Setup);
};

void InitContentHandlers(package_manager::PackageManagerImpl* manager,
                         const base::CommandLine& command_line) {
  // Default content handlers.
  manager->RegisterContentHandler("application/javascript",
                                  GURL("mojo:html_viewer"));
  manager->RegisterContentHandler("application/pdf", GURL("mojo:pdf_viewer"));
  manager->RegisterContentHandler("image/gif", GURL("mojo:html_viewer"));
  manager->RegisterContentHandler("image/jpeg", GURL("mojo:html_viewer"));
  manager->RegisterContentHandler("image/png", GURL("mojo:html_viewer"));
  manager->RegisterContentHandler("text/css", GURL("mojo:html_viewer"));
  manager->RegisterContentHandler("text/html", GURL("mojo:html_viewer"));
  manager->RegisterContentHandler("text/plain", GURL("mojo:html_viewer"));

  // Command-line-specified content handlers.
  std::string handlers_spec =
      command_line.GetSwitchValueASCII(switches::kContentHandlers);
  if (handlers_spec.empty())
    return;

#if defined(OS_ANDROID)
  // TODO(eseidel): On Android we pass command line arguments is via the
  // 'parameters' key on the intent, which we specify during 'am shell start'
  // via --esa, however that expects comma-separated values and says:
  //   am shell --help:
  //     [--esa <EXTRA_KEY> <EXTRA_STRING_VALUE>[,<EXTRA_STRING_VALUE...]]
  //     (to embed a comma into a string escape it using "\,")
  // Whatever takes 'parameters' and constructs a CommandLine is failing to
  // un-escape the commas, we need to move this fix to that file.
  base::ReplaceSubstringsAfterOffset(&handlers_spec, 0, "\\,", ",");
#endif

  std::vector<std::string> parts = base::SplitString(
      handlers_spec, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.size() % 2 != 0) {
    LOG(ERROR) << "Invalid value for switch " << switches::kContentHandlers
               << ": must be a comma-separated list of mimetype/url pairs."
               << handlers_spec;
    return;
  }

  for (size_t i = 0; i < parts.size(); i += 2) {
    GURL url(parts[i + 1]);
    if (!url.is_valid()) {
      LOG(ERROR) << "Invalid value for switch " << switches::kContentHandlers
                 << ": '" << parts[i + 1] << "' is not a valid URL.";
      return;
    }
    // TODO(eseidel): We should also validate that the mimetype is valid
    // net/base/mime_util.h could do this, but we don't want to depend on net.
    manager->RegisterContentHandler(parts[i], url);
  }
}

void InitDevToolsServiceIfNeeded(shell::ApplicationManager* manager,
                                 const base::CommandLine& command_line) {
  if (!command_line.HasSwitch(devtools_service::kRemoteDebuggingPort))
    return;

  std::string port_str =
      command_line.GetSwitchValueASCII(devtools_service::kRemoteDebuggingPort);
  unsigned port;
  if (!base::StringToUint(port_str, &port) || port > 65535) {
    LOG(ERROR) << "Invalid value for switch "
               << devtools_service::kRemoteDebuggingPort << ": '" << port_str
               << "' is not a valid port number.";
    return;
  }

  ServiceProviderPtr devtools_service_provider;
  scoped_ptr<shell::ConnectToApplicationParams> params(
      new shell::ConnectToApplicationParams);
  params->set_source(shell::Identity(GURL("mojo:shell"), std::string(),
                                     shell::GetPermissiveCapabilityFilter()));
  params->SetTarget(shell::Identity(GURL("mojo:devtools_service"),
                                    std::string(),
                                    shell::GetPermissiveCapabilityFilter()));
  params->set_services(GetProxy(&devtools_service_provider));
  manager->ConnectToApplication(params.Pass());

  devtools_service::DevToolsCoordinatorPtr devtools_coordinator;
  devtools_service_provider->ConnectToService(
      devtools_service::DevToolsCoordinator::Name_,
      GetProxy(&devtools_coordinator).PassMessagePipe());
  devtools_coordinator->Initialize(static_cast<uint16_t>(port));
}

class TracingServiceProvider : public ServiceProvider {
 public:
  TracingServiceProvider(Tracer* tracer,
                         InterfaceRequest<ServiceProvider> request)
      : tracer_(tracer), binding_(this, request.Pass()) {}
  ~TracingServiceProvider() override {}

  void ConnectToService(const mojo::String& service_name,
                        ScopedMessagePipeHandle client_handle) override {
    if (tracer_ && service_name == tracing::TraceProvider::Name_) {
      tracer_->ConnectToProvider(
          MakeRequest<tracing::TraceProvider>(client_handle.Pass()));
    }
  }

 private:
  Tracer* tracer_;
  StrongBinding<ServiceProvider> binding_;

  DISALLOW_COPY_AND_ASSIGN(TracingServiceProvider);
};

}  // namespace

Context::Context()
    : package_manager_(nullptr), main_entry_time_(base::Time::Now()) {}

Context::~Context() {
  DCHECK(!base::MessageLoop::current());
}

// static
void Context::EnsureEmbedderIsInitialized() {
  static base::LazyInstance<Setup>::Leaky setup = LAZY_INSTANCE_INITIALIZER;
  setup.Get();
}

bool Context::Init(const base::FilePath& shell_file_root) {
  TRACE_EVENT0("mojo_shell", "Context::Init");
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  bool trace_startup = command_line.HasSwitch(switches::kTraceStartup);
  if (trace_startup) {
    tracer_.Start(
        command_line.GetSwitchValueASCII(switches::kTraceStartup),
        command_line.GetSwitchValueASCII(switches::kTraceStartupDuration),
        "mojo_runner.trace");
  }

  // ICU data is a thing every part of the system needs. This here warms
  // up the copy of ICU in the mojo runner.
  CHECK(base::i18n::InitializeICU());

  EnsureEmbedderIsInitialized();
  task_runners_.reset(
      new TaskRunners(base::MessageLoop::current()->task_runner()));

  // TODO(vtl): This should be MASTER, not NONE.
  embedder::InitIPCSupport(embedder::ProcessType::NONE, this,
                           task_runners_->io_runner(),
                           embedder::ScopedPlatformHandle());

  package_manager_ = new package_manager::PackageManagerImpl(
      shell_file_root, task_runners_->blocking_pool());
  InitContentHandlers(package_manager_, command_line);

  RegisterLocalAliases(package_manager_);

  scoped_ptr<shell::NativeRunnerFactory> runner_factory;
  if (command_line.HasSwitch(switches::kEnableMultiprocess)) {
    runner_factory.reset(
        new OutOfProcessNativeRunnerFactory(task_runners_->blocking_pool()));
  } else {
#if defined(COMPONENT_BUILD)
    LOG(ERROR) << "Running Mojo in single process component build, which isn't "
               << "supported because statics in apps interact. Use static build"
               << " or don't pass --single-process.";
#endif
    runner_factory.reset(
        new InProcessNativeRunnerFactory(task_runners_->blocking_pool()));
  }
  application_manager_.reset(new shell::ApplicationManager(
      make_scoped_ptr(package_manager_), runner_factory.Pass(),
      task_runners_->blocking_pool()));

  ServiceProviderPtr tracing_services;
  ServiceProviderPtr tracing_exposed_services;
  new TracingServiceProvider(&tracer_, GetProxy(&tracing_exposed_services));

  scoped_ptr<shell::ConnectToApplicationParams> params(
      new shell::ConnectToApplicationParams);
  params->set_source(shell::Identity(GURL("mojo:shell"), std::string(),
                                     shell::GetPermissiveCapabilityFilter()));
  params->SetTarget(shell::Identity(GURL("mojo:tracing"), std::string(),
                                    shell::GetPermissiveCapabilityFilter()));
  params->set_services(GetProxy(&tracing_services));
  params->set_exposed_services(tracing_exposed_services.Pass());
  application_manager_->ConnectToApplication(params.Pass());

  if (command_line.HasSwitch(tracing::kTraceStartup)) {
    tracing::TraceCollectorPtr coordinator;
    auto coordinator_request = GetProxy(&coordinator);
    tracing_services->ConnectToService(tracing::TraceCollector::Name_,
                                       coordinator_request.PassMessagePipe());
    tracer_.StartCollectingFromTracingService(coordinator.Pass());
  }

  // Record the shell startup metrics used for performance testing.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          tracing::kEnableStatsCollectionBindings)) {
    tracing::StartupPerformanceDataCollectorPtr collector;
    tracing_services->ConnectToService(
        tracing::StartupPerformanceDataCollector::Name_,
        GetProxy(&collector).PassMessagePipe());
#if defined(OS_MACOSX) || defined(OS_WIN) || defined(OS_LINUX)
    // CurrentProcessInfo::CreationTime is only defined on some platforms.
    const base::Time creation_time = base::CurrentProcessInfo::CreationTime();
    collector->SetShellProcessCreationTime(creation_time.ToInternalValue());
#endif
    collector->SetShellMainEntryPointTime(main_entry_time_.ToInternalValue());
  }

  InitDevToolsServiceIfNeeded(application_manager_.get(), command_line);

  return true;
}

void Context::Shutdown() {
  // Actions triggered by ApplicationManager's destructor may require a current
  // message loop, so we should destruct it explicitly now as ~Context() occurs
  // post message loop shutdown.
  application_manager_.reset();

  TRACE_EVENT0("mojo_shell", "Context::Shutdown");
  DCHECK_EQ(base::MessageLoop::current()->task_runner(),
            task_runners_->shell_runner());
  embedder::ShutdownIPCSupport();
  // We'll quit when we get OnShutdownComplete().
  base::MessageLoop::current()->Run();
}

void Context::OnShutdownComplete() {
  DCHECK_EQ(base::MessageLoop::current()->task_runner(),
            task_runners_->shell_runner());
  base::MessageLoop::current()->QuitWhenIdle();
}

void Context::Run(const GURL& url) {
  DCHECK(app_complete_callback_.is_null());
  ServiceProviderPtr services;
  ServiceProviderPtr exposed_services;

  app_urls_.insert(url);

  scoped_ptr<shell::ConnectToApplicationParams> params(
      new shell::ConnectToApplicationParams);
  params->SetTarget(shell::Identity(url, std::string(),
                                    shell::GetPermissiveCapabilityFilter()));
  params->set_services(GetProxy(&services));
  params->set_exposed_services(exposed_services.Pass());
  params->set_on_application_end(
      base::Bind(&Context::OnApplicationEnd, base::Unretained(this), url));
  application_manager_->ConnectToApplication(params.Pass());
}

void Context::RunCommandLineApplication(const base::Closure& callback) {
  DCHECK(app_urls_.empty());
  DCHECK(app_complete_callback_.is_null());
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  base::CommandLine::StringVector args = command_line->GetArgs();
  for (size_t i = 0; i < args.size(); ++i) {
    GURL possible_app(args[i]);
    if (possible_app.SchemeIs("mojo")) {
      Run(possible_app);
      app_complete_callback_ = callback;
      break;
    }
  }
}

void Context::OnApplicationEnd(const GURL& url) {
  if (app_urls_.find(url) != app_urls_.end()) {
    app_urls_.erase(url);
    if (app_urls_.empty() && base::MessageLoop::current()->is_running()) {
      DCHECK_EQ(base::MessageLoop::current()->task_runner(),
                task_runners_->shell_runner());
      if (app_complete_callback_.is_null()) {
        base::MessageLoop::current()->QuitWhenIdle();
      } else {
        app_complete_callback_.Run();
      }
    }
  }
}

}  // namespace runner
}  // namespace mojo
