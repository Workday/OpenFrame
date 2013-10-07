// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/binding_generating_native_handler.h"

#include "chrome/renderer/extensions/module_system.h"

namespace extensions {

BindingGeneratingNativeHandler::BindingGeneratingNativeHandler(
    ModuleSystem* module_system,
    const std::string& api_name,
    const std::string& bind_to)
    : module_system_(module_system),
      api_name_(api_name),
      bind_to_(bind_to) {
}

v8::Handle<v8::Object> BindingGeneratingNativeHandler::NewInstance() {
  v8::HandleScope scope;
  v8::Handle<v8::Object> binding_module =
      module_system_->Require("binding")->ToObject();
  v8::Handle<v8::Object> binding  =
      binding_module->Get(v8::String::New("Binding"))->ToObject();
  v8::Handle<v8::Function> create_binding =
      binding->Get(v8::String::New("create")).As<v8::Function>();
  v8::Handle<v8::Value> argv[] = {
    v8::String::New(api_name_.c_str())
  };
  v8::Handle<v8::Object> binding_instance =
      create_binding->Call(binding, arraysize(argv), argv)->ToObject();
  v8::Handle<v8::Function> generate =
      binding_instance->Get(v8::String::New("generate")).As<v8::Function>();
  v8::Handle<v8::Object> object = v8::Object::New();
  v8::Handle<v8::Value> compiled_schema =
      generate->Call(binding_instance, 0, NULL);
  if (!compiled_schema.IsEmpty())
    object->Set(v8::String::New(bind_to_.c_str()), compiled_schema);
  return scope.Close(object);
}

} // namespace extensions
