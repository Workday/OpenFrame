// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/ibus/ibus_engine_factory_service.h"

#include <map>
#include <string>

#include "base/strings/string_number_conversions.h"
#include "chromeos/dbus/ibus/ibus_constants.h"
#include "chromeos/ime/ibus_bridge.h"
#include "dbus/bus.h"
#include "dbus/exported_object.h"
#include "dbus/message.h"

namespace chromeos {

namespace {
const char* kObjectPathPrefix = "/org/freedesktop/IBus/Engine/";
}  // namespace

class IBusEngineFactoryServiceImpl : public IBusEngineFactoryService {
 public:
  explicit IBusEngineFactoryServiceImpl(dbus::Bus* bus)
      : bus_(bus),
        object_path_id_(0),
        weak_ptr_factory_(this) {
    exported_object_ = bus_->GetExportedObject(
        dbus::ObjectPath(ibus::engine_factory::kServicePath));

    exported_object_->ExportMethod(
        ibus::engine_factory::kServiceInterface,
        ibus::engine_factory::kCreateEngineMethod,
        base::Bind(&IBusEngineFactoryServiceImpl::CreateEngine,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&IBusEngineFactoryServiceImpl::CreateEngineExported,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  virtual ~IBusEngineFactoryServiceImpl() {
    bus_->UnregisterExportedObject(dbus::ObjectPath(
        ibus::engine_factory::kServicePath));
  }

  // IBusEngineFactoryService override.
  virtual void SetCreateEngineHandler(
      const std::string& engine_id,
      const CreateEngineHandler& create_engine_handler) OVERRIDE {
    DCHECK(create_engine_callback_map_[engine_id].is_null());
    create_engine_callback_map_[engine_id] = create_engine_handler;
  }

  // IBusEngineFactoryService override.
  virtual void UnsetCreateEngineHandler(
      const std::string& engine_id) OVERRIDE {
    create_engine_callback_map_[engine_id].Reset();
  }

  // IBusEngineFactoryService override.
  virtual dbus::ObjectPath GenerateUniqueObjectPath() OVERRIDE {
    return dbus::ObjectPath(kObjectPathPrefix +
                            base::IntToString(object_path_id_++));
  }

 private:
  // Called when the ibus-daemon requires new engine instance.
  void CreateEngine(dbus::MethodCall* method_call,
                    dbus::ExportedObject::ResponseSender response_sender) {
    if (!method_call) {
      LOG(ERROR) << "method call does not have any arguments.";
      return;
    }
    dbus::MessageReader reader(method_call);
    std::string engine_name;

    if (!reader.PopString(&engine_name)) {
      LOG(ERROR) << "Expected argument is string.";
      return;
    }
    if (create_engine_callback_map_[engine_name].is_null()) {
      LOG(WARNING) << "The CreateEngine handler for " << engine_name
                   << " is NULL.";
    } else {
      create_engine_callback_map_[engine_name].Run(
          base::Bind(&IBusEngineFactoryServiceImpl::CreateEngineSendReply,
                     weak_ptr_factory_.GetWeakPtr(),
                     base::Passed(dbus::Response::FromMethodCall(method_call)),
                     response_sender));
    }
  }

  // Sends reply message for CreateEngine method call.
  void CreateEngineSendReply(
      scoped_ptr<dbus::Response> response,
      const dbus::ExportedObject::ResponseSender response_sender,
      const dbus::ObjectPath& path) {
    dbus::MessageWriter writer(response.get());
    writer.AppendObjectPath(path);
    response_sender.Run(response.Pass());
  }

  // Called when the CreateEngine method is exported.
  void CreateEngineExported(const std::string& interface_name,
                            const std::string& method_name,
                            bool success) {
    DCHECK(success) << "Failed to export: "
                    << interface_name << "." << method_name;
  }

  // CreateEngine method call handler.
  std::map<std::string, CreateEngineHandler> create_engine_callback_map_;

  dbus::Bus* bus_;
  scoped_refptr<dbus::ExportedObject> exported_object_;
  uint32 object_path_id_;
  base::WeakPtrFactory<IBusEngineFactoryServiceImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(IBusEngineFactoryServiceImpl);
};

// An implementation of IBusEngineFactoryService without ibus-daemon
// interaction. Currently this class is used only on linux desktop.
// TODO(nona): Use this on ChromeOS device once crbug.com/171351 is fixed.
class IBusEngineFactoryServiceDaemonlessImpl : public IBusEngineFactoryService {
 public:
  IBusEngineFactoryServiceDaemonlessImpl()
      : object_path_id_(0) {}
  virtual ~IBusEngineFactoryServiceDaemonlessImpl() {}

  // IBusEngineFactoryService override.
  virtual void SetCreateEngineHandler(
      const std::string& engine_id,
      const CreateEngineHandler& create_engine_handler) OVERRIDE {
    IBusBridge::Get()->SetCreateEngineHandler(engine_id, create_engine_handler);
  }

  // IBusEngineFactoryService override.
  virtual void UnsetCreateEngineHandler(
      const std::string& engine_id) OVERRIDE {
    IBusBridge::Get()->UnsetCreateEngineHandler(engine_id);
  }

  // IBusEngineFactoryService override.
  virtual dbus::ObjectPath GenerateUniqueObjectPath() OVERRIDE {
    // ObjectPath is not used in non-ibus-daemon implementation. Passing dummy
    // valid and unique dummy object path.
    return dbus::ObjectPath("/dummy/object/path" +
                            base::IntToString(object_path_id_++));
  }

 private:
  uint32 object_path_id_;
  DISALLOW_COPY_AND_ASSIGN(IBusEngineFactoryServiceDaemonlessImpl);
};

IBusEngineFactoryService::IBusEngineFactoryService() {
}

IBusEngineFactoryService::~IBusEngineFactoryService() {
}

// static
IBusEngineFactoryService* IBusEngineFactoryService::Create(
    dbus::Bus* bus,
    DBusClientImplementationType type) {
  if (type == REAL_DBUS_CLIENT_IMPLEMENTATION)
    return new IBusEngineFactoryServiceImpl(bus);
  else
    return new IBusEngineFactoryServiceDaemonlessImpl();
}

}  // namespace chromeos
