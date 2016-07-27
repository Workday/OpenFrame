// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/shell/application_manager.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/trace_event/trace_event.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/shell/application_instance.h"
#include "mojo/shell/fetcher.h"
#include "mojo/shell/package_manager.h"
#include "mojo/shell/query_util.h"
#include "mojo/shell/shell_application_loader.h"
#include "mojo/shell/switches.h"

namespace mojo {
namespace shell {

namespace {

// Used by TestAPI.
bool has_created_instance = false;

void OnEmptyOnConnectCallback(uint32_t content_handler_id) {}

}  // namespace

// static
ApplicationManager::TestAPI::TestAPI(ApplicationManager* manager)
    : manager_(manager) {
}

ApplicationManager::TestAPI::~TestAPI() {
}

bool ApplicationManager::TestAPI::HasCreatedInstance() {
  return has_created_instance;
}

bool ApplicationManager::TestAPI::HasRunningInstanceForURL(
    const GURL& url) const {
  return manager_->identity_to_instance_.find(Identity(url)) !=
         manager_->identity_to_instance_.end();
}

ApplicationManager::ApplicationManager(
    scoped_ptr<PackageManager> package_manager)
    : ApplicationManager(package_manager.Pass(), nullptr, nullptr) {}

ApplicationManager::ApplicationManager(
    scoped_ptr<PackageManager> package_manager,
    scoped_ptr<NativeRunnerFactory> native_runner_factory,
    base::TaskRunner* task_runner)
    : package_manager_(package_manager.Pass()),
      task_runner_(task_runner),
      native_runner_factory_(native_runner_factory.Pass()),
      weak_ptr_factory_(this) {
  package_manager_->SetApplicationManager(this);
  SetLoaderForURL(make_scoped_ptr(new ShellApplicationLoader(this)),
                  GURL("mojo:shell"));
}

ApplicationManager::~ApplicationManager() {
  TerminateShellConnections();
  STLDeleteValues(&url_to_loader_);
}

void ApplicationManager::TerminateShellConnections() {
  STLDeleteValues(&identity_to_instance_);
}

void ApplicationManager::ConnectToApplication(
    scoped_ptr<ConnectToApplicationParams> params) {
  TRACE_EVENT_INSTANT1("mojo_shell", "ApplicationManager::ConnectToApplication",
                       TRACE_EVENT_SCOPE_THREAD, "original_url",
                       params->target().url().spec());
  DCHECK(params->target().url().is_valid());

  // Connect to an existing matching instance, if possible.
  if (ConnectToRunningApplication(&params))
    return;

  ApplicationLoader* loader = GetLoaderForURL(params->target().url());
  if (loader) {
    GURL url = params->target().url();
    loader->Load(url, CreateAndConnectToInstance(params.Pass(), nullptr));
    return;
  }

  URLRequestPtr original_url_request = params->TakeTargetURLRequest();
  auto callback =
      base::Bind(&ApplicationManager::HandleFetchCallback,
                 weak_ptr_factory_.GetWeakPtr(), base::Passed(&params));
  package_manager_->FetchRequest(original_url_request.Pass(), callback);
}

bool ApplicationManager::ConnectToRunningApplication(
    scoped_ptr<ConnectToApplicationParams>* params) {
  ApplicationInstance* instance = GetApplicationInstance((*params)->target());
  if (!instance)
    return false;

  instance->ConnectToClient(params->Pass());
  return true;
}

ApplicationInstance* ApplicationManager::GetApplicationInstance(
    const Identity& identity) const {
  const auto& it = identity_to_instance_.find(identity);
  return it != identity_to_instance_.end() ? it->second : nullptr;
}

void ApplicationManager::CreateInstanceForHandle(ScopedHandle channel,
                                                 const GURL& url,
                                                 CapabilityFilterPtr filter) {
  // Instances created by others are considered unique, and thus have no
  // identity. As such they cannot be connected to by anyone else, and so we
  // never call ConnectToClient().
  // TODO(beng): GetPermissiveCapabilityFilter() here obviously cannot make it
  //             to production. See note in application_manager.mojom.
  //             http://crbug.com/555392
  CapabilityFilter local_filter = filter->filter.To<CapabilityFilter>();
  Identity target_id(url, std::string(), local_filter);
  InterfaceRequest<Application> application_request =
      CreateInstance(target_id, base::Closure(), nullptr);
  NativeRunner* runner =
      native_runner_factory_->Create(base::FilePath()).release();
  native_runners_.push_back(runner);
  runner->InitHost(channel.Pass(), application_request.Pass());
}

InterfaceRequest<Application> ApplicationManager::CreateAndConnectToInstance(
    scoped_ptr<ConnectToApplicationParams> params,
    ApplicationInstance** resulting_instance) {
  ApplicationInstance* instance = nullptr;
  InterfaceRequest<Application> application_request =
      CreateInstance(params->target(), params->on_application_end(), &instance);
  instance->ConnectToClient(params.Pass());
  if (resulting_instance)
    *resulting_instance = instance;
  return application_request.Pass();
}

InterfaceRequest<Application> ApplicationManager::CreateInstance(
    const Identity& target_id,
    const base::Closure& on_application_end,
    ApplicationInstance** resulting_instance) {
  ApplicationPtr application;
  InterfaceRequest<Application> application_request = GetProxy(&application);
  ApplicationInstance* instance = new ApplicationInstance(
      application.Pass(), this, target_id, Shell::kInvalidContentHandlerID,
      on_application_end);
  DCHECK(identity_to_instance_.find(target_id) ==
         identity_to_instance_.end());
  identity_to_instance_[target_id] = instance;
  instance->InitializeApplication();
  if (resulting_instance)
    *resulting_instance = instance;
  return application_request.Pass();
}

void ApplicationManager::HandleFetchCallback(
    scoped_ptr<ConnectToApplicationParams> params,
    scoped_ptr<Fetcher> fetcher) {
  if (!fetcher) {
    // Network error. Drop |params| to tell the requestor.
    params->connect_callback().Run(Shell::kInvalidContentHandlerID);
    return;
  }

  GURL redirect_url = fetcher->GetRedirectURL();
  if (!redirect_url.is_empty()) {
    // And around we go again... Whee!
    // TODO(sky): this loses the original URL info.
    URLRequestPtr new_request = URLRequest::New();
    new_request->url = redirect_url.spec();
    HttpHeaderPtr header = HttpHeader::New();
    header->name = "Referer";
    header->value = fetcher->GetRedirectReferer().spec();
    new_request->headers.push_back(header.Pass());
    params->SetTargetURLRequest(new_request.Pass());
    ConnectToApplication(params.Pass());
    return;
  }

  // We already checked if the application was running before we fetched it, but
  // it might have started while the fetch was outstanding. We don't want to
  // have two copies of the app running, so check again.
  if (ConnectToRunningApplication(&params))
    return;

  Identity source = params->source();
  Identity target = params->target();
  Shell::ConnectToApplicationCallback connect_callback =
      params->connect_callback();
  params->set_connect_callback(EmptyConnectCallback());
  ApplicationInstance* app = nullptr;
  InterfaceRequest<Application> request(
      CreateAndConnectToInstance(params.Pass(), &app));

  uint32_t content_handler_id = package_manager_->HandleWithContentHandler(
      fetcher.get(), source, target.url(), target.filter(), &request);
  if (content_handler_id != Shell::kInvalidContentHandlerID) {
    app->set_requesting_content_handler_id(content_handler_id);
    connect_callback.Run(content_handler_id);
    return;
  }

  // TODO(erg): Have a better way of switching the sandbox on. For now, switch
  // it on hard coded when we're using some of the sandboxable core services.
  bool start_sandboxed = false;
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kMojoNoSandbox)) {
    start_sandboxed = (target.url() == GURL("mojo://core_services/") &&
                          target.qualifier() == "Core") ||
                      target.url() == GURL("mojo://html_viewer/");
  }

  connect_callback.Run(Shell::kInvalidContentHandlerID);

  fetcher->AsPath(task_runner_,
                  base::Bind(&ApplicationManager::RunNativeApplication,
                              weak_ptr_factory_.GetWeakPtr(),
                              base::Passed(request.Pass()), start_sandboxed,
                              base::Passed(fetcher.Pass())));
}

void ApplicationManager::RunNativeApplication(
    InterfaceRequest<Application> application_request,
    bool start_sandboxed,
    scoped_ptr<Fetcher> fetcher,
    const base::FilePath& path,
    bool path_exists) {
  // We only passed fetcher to keep it alive. Done with it now.
  fetcher.reset();

  DCHECK(application_request.is_pending());

  if (!path_exists) {
    LOG(ERROR) << "Library not started because library path '" << path.value()
               << "' does not exist.";
    return;
  }

  TRACE_EVENT1("mojo_shell", "ApplicationManager::RunNativeApplication", "path",
               path.AsUTF8Unsafe());
  NativeRunner* runner = native_runner_factory_->Create(path).release();
  native_runners_.push_back(runner);
  runner->Start(path, start_sandboxed, application_request.Pass(),
                base::Bind(&ApplicationManager::CleanupRunner,
                           weak_ptr_factory_.GetWeakPtr(), runner));
}

void ApplicationManager::SetLoaderForURL(scoped_ptr<ApplicationLoader> loader,
                                         const GURL& url) {
  URLToLoaderMap::iterator it = url_to_loader_.find(url);
  if (it != url_to_loader_.end())
    delete it->second;
  url_to_loader_[url] = loader.release();
}

ApplicationLoader* ApplicationManager::GetLoaderForURL(const GURL& url) {
  auto url_it = url_to_loader_.find(GetBaseURLAndQuery(url, nullptr));
  if (url_it != url_to_loader_.end())
    return url_it->second;
  return default_loader_.get();
}

void ApplicationManager::OnApplicationInstanceError(
    ApplicationInstance* instance) {
  // Called from ~ApplicationInstance, so we do not need to call Destroy here.
  const Identity identity = instance->identity();
  base::Closure on_application_end = instance->on_application_end();
  // Remove the shell.
  auto it = identity_to_instance_.find(identity);
  DCHECK(it != identity_to_instance_.end());
  delete it->second;
  identity_to_instance_.erase(it);
  if (!on_application_end.is_null())
    on_application_end.Run();
}

void ApplicationManager::CleanupRunner(NativeRunner* runner) {
  native_runners_.erase(
      std::find(native_runners_.begin(), native_runners_.end(), runner));
}

Shell::ConnectToApplicationCallback EmptyConnectCallback() {
  return base::Bind(&OnEmptyOnConnectCallback);
}

}  // namespace shell
}  // namespace mojo
