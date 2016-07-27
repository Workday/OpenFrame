// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/render_widget_window_tree_client_factory.h"

#include "base/logging.h"
#include "base/macros.h"
#include "components/mus/public/interfaces/window_tree.mojom.h"
#include "content/common/render_widget_window_tree_client_factory.mojom.h"
#include "content/public/common/mojo_shell_connection.h"
#include "content/renderer/render_widget_mus_connection.h"
#include "mojo/application/public/cpp/application_connection.h"
#include "mojo/application/public/cpp/interface_factory.h"
#include "mojo/common/weak_binding_set.h"
#include "url/gurl.h"

namespace content {

namespace {

// This object's lifetime is managed by MojoShellConnection because it's a
// MojoShellConnection::Listener.
class RenderWidgetWindowTreeClientFactoryImpl
    : public MojoShellConnection::Listener,
      public mojo::InterfaceFactory<mojom::RenderWidgetWindowTreeClientFactory>,
      public mojom::RenderWidgetWindowTreeClientFactory {
 public:
  RenderWidgetWindowTreeClientFactoryImpl() {
    DCHECK(MojoShellConnection::Get());
    MojoShellConnection::Get()->AddListener(this);
  }

  ~RenderWidgetWindowTreeClientFactoryImpl() override {}

 private:
  // MojoShellConnection::Listener implementation:
  bool ConfigureIncomingConnection(
      mojo::ApplicationConnection* connection) override {
    connection->AddService<mojom::RenderWidgetWindowTreeClientFactory>(this);
    return true;
  }

  // mojo::InterfaceFactory<mojom::RenderWidgetWindowTreeClientFactory>:
  void Create(mojo::ApplicationConnection* connection,
              mojo::InterfaceRequest<mojom::RenderWidgetWindowTreeClientFactory>
                  request) override {
    bindings_.AddBinding(this, request.Pass());
  }

  // mojom::RenderWidgetWindowTreeClientFactory implementation.
  void CreateWindowTreeClientForRenderWidget(
      uint32_t routing_id,
      mojo::InterfaceRequest<mus::mojom::WindowTreeClient> request) override {
    RenderWidgetMusConnection* connection =
        RenderWidgetMusConnection::GetOrCreate(routing_id);
    connection->Bind(request.Pass());
  }

  mojo::WeakBindingSet<mojom::RenderWidgetWindowTreeClientFactory> bindings_;

  DISALLOW_COPY_AND_ASSIGN(RenderWidgetWindowTreeClientFactoryImpl);
};

}  // namespace

void CreateRenderWidgetWindowTreeClientFactory() {
  new RenderWidgetWindowTreeClientFactoryImpl;
}

}  // namespace content
