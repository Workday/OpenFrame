// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_EXTENSIONS_LOGGING_NATIVE_HANDLER_H_
#define CHROME_RENDERER_EXTENSIONS_LOGGING_NATIVE_HANDLER_H_

#include <string>

#include "chrome/renderer/extensions/object_backed_native_handler.h"

namespace extensions {

// Exposes logging.h macros to JavaScript bindings.
class LoggingNativeHandler : public ObjectBackedNativeHandler {
 public:
  explicit LoggingNativeHandler(ChromeV8Context* context);
  virtual ~LoggingNativeHandler();

  // Equivalent to CHECK(predicate) << message.
  //
  // void(predicate, message?)
  void Check(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Equivalent to DCHECK(predicate) << message.
  //
  // void(predicate, message?)
  void Dcheck(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Equivalent to DCHECK_IS_ON().
  //
  // bool()
  void DcheckIsOn(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Equivalent to LOG(INFO) << message.
  //
  // void(message)
  void Log(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Equivalent to LOG(WARNING) << message.
  //
  // void(message)
  void Warning(const v8::FunctionCallbackInfo<v8::Value>& args);

  void ParseArgs(const v8::FunctionCallbackInfo<v8::Value>& args,
                 bool* check_value,
                 std::string* error_message);

  std::string ToStringOrDefault(const v8::Handle<v8::String>& v8_string,
                                const std::string& dflt);
};

}  // namespace extensions

#endif  // CHROME_RENDERER_EXTENSIONS_LOGGING_NATIVE_HANDLER_H_
