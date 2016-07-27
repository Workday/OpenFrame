// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/stringprintf.h"
#include "base/thread_task_runner_handle.h"
#include "components/mus/public/interfaces/gpu.mojom.h"
#include "content/browser/mojo/mojo_shell_client_host.h"
#include "content/common/mojo/mojo_messages.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/mojo_shell_connection.h"
#include "ipc/ipc_sender.h"
#include "mojo/application/public/cpp/application_impl.h"
#include "mojo/application/public/interfaces/application_manager.mojom.h"
#include "mojo/converters/network/network_type_converters.h"
#include "third_party/mojo/src/mojo/edk/embedder/embedder.h"
#include "third_party/mojo/src/mojo/edk/embedder/platform_channel_pair.h"
#include "third_party/mojo/src/mojo/edk/embedder/scoped_platform_handle.h"

namespace content {
namespace {

const char kMojoShellInstanceURL[] = "mojo_shell_instance_url";
const char kMojoPlatformFile[] = "mojo_platform_file";

void DidCreateChannel(mojo::embedder::ChannelInfo* info) {}

base::PlatformFile PlatformFileFromScopedPlatformHandle(
    mojo::embedder::ScopedPlatformHandle handle) {
#if defined(OS_POSIX)
  return handle.release().fd;
#elif defined(OS_WIN)
  return handle.release().handle;
#endif
}

class InstanceURL : public base::SupportsUserData::Data {
 public:
  InstanceURL(const std::string& instance_url) : instance_url_(instance_url) {}
  ~InstanceURL() override {}

  std::string get() const { return instance_url_; }

 private:
  std::string instance_url_;

  DISALLOW_COPY_AND_ASSIGN(InstanceURL);
};

class InstanceShellHandle : public base::SupportsUserData::Data {
 public:
  InstanceShellHandle(base::PlatformFile shell_handle)
      : shell_handle_(shell_handle) {}
  ~InstanceShellHandle() override {}

  base::PlatformFile get() const { return shell_handle_; }

 private:
  base::PlatformFile shell_handle_;

  DISALLOW_COPY_AND_ASSIGN(InstanceShellHandle);
};

void SetMojoApplicationInstanceURL(RenderProcessHost* render_process_host,
                                   const std::string& instance_url) {
  render_process_host->SetUserData(kMojoShellInstanceURL,
                                   new InstanceURL(instance_url));
}

void SetMojoPlatformFile(RenderProcessHost* render_process_host,
                         base::PlatformFile platform_file) {
  render_process_host->SetUserData(kMojoPlatformFile,
                                   new InstanceShellHandle(platform_file));
}

}  // namespace

void RegisterChildWithExternalShell(int child_process_id,
                                    RenderProcessHost* render_process_host) {
  // Some process types get created before the main message loop.
  if (!MojoShellConnection::Get())
    return;

  // Create the channel to be shared with the target process.
  mojo::embedder::HandlePassingInformation handle_passing_info;
  mojo::embedder::PlatformChannelPair platform_channel_pair;

  // Give one end to the shell so that it can create an instance.
  mojo::embedder::ScopedPlatformHandle platform_channel =
      platform_channel_pair.PassServerHandle();
  mojo::ScopedMessagePipeHandle handle(mojo::embedder::CreateChannel(
      platform_channel.Pass(), base::Bind(&DidCreateChannel),
      base::ThreadTaskRunnerHandle::Get()));
  mojo::shell::mojom::ApplicationManagerPtr application_manager;
  MojoShellConnection::Get()->GetApplication()->ConnectToService(
      "mojo:shell", &application_manager);
  // The content of the URL/qualifier we pass is actually meaningless, it's only
  // important that they're unique per process.
  // TODO(beng): We need to specify a restrictive CapabilityFilter here that
  //             matches the needs of the target process. Figure out where that
  //             specification is best determined (not here, this is a common
  //             chokepoint for all process types) and how to wire it through.
  //             http://crbug.com/555393
  std::string url =
      base::StringPrintf("exe:chrome_renderer%d", child_process_id);

  mojo::CapabilityFilterPtr filter(mojo::CapabilityFilter::New());
  mojo::Array<mojo::String> window_manager_interfaces;
  window_manager_interfaces.push_back(mus::mojom::Gpu::Name_);
  filter->filter.insert("mojo:mus", window_manager_interfaces.Pass());
  application_manager->CreateInstanceForHandle(
      mojo::ScopedHandle(mojo::Handle(handle.release().value())),
      url,
      filter.Pass());

  // Send the other end to the child via Chrome IPC.
  base::PlatformFile client_file = PlatformFileFromScopedPlatformHandle(
      platform_channel_pair.PassClientHandle());
  SetMojoPlatformFile(render_process_host, client_file);

  // Store the URL on the RPH so client code can access it later via
  // GetMojoApplicationInstanceURL().
  SetMojoApplicationInstanceURL(render_process_host, url);
}

std::string GetMojoApplicationInstanceURL(
    RenderProcessHost* render_process_host) {
  InstanceURL* instance_url = static_cast<InstanceURL*>(
      render_process_host->GetUserData(kMojoShellInstanceURL));
  return instance_url ? instance_url->get() : std::string();
}

void SendExternalMojoShellHandleToChild(
    base::ProcessHandle process_handle,
    RenderProcessHost* render_process_host) {
  InstanceShellHandle* client_file = static_cast<InstanceShellHandle*>(
      render_process_host->GetUserData(kMojoPlatformFile));
  if (!client_file)
    return;
  render_process_host->Send(new MojoMsg_BindExternalMojoShellHandle(
      IPC::GetFileHandleForProcess(client_file->get(), process_handle, true)));
}

}  // namespace content
