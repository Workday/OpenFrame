// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/test/cpp_binding_example.h"

#include <stdio.h>

#include "base/bind.h"
#include "base/bind_helpers.h"

using webkit_glue::CppArgumentList;
using webkit_glue::CppBoundClass;
using webkit_glue::CppVariant;

namespace content {

namespace {

class PropertyCallbackExample : public CppBoundClass::PropertyCallback {
 public:
  virtual bool GetValue(CppVariant* value) OVERRIDE {
    value->Set(value_);
    return true;
  }

  virtual bool SetValue(const CppVariant& value) OVERRIDE {
    value_.Set(value);
    return true;
  }

 private:
  CppVariant value_;
};

}  // namespace

CppBindingExample::CppBindingExample() {
  // Map properties.  It's recommended, but not required, that the JavaScript
  // names (used as the keys in this map) match the names of the member
  // variables exposed through those names.
  BindProperty("my_value", &my_value);
  BindProperty("my_other_value", &my_other_value);

  // Bind property with a callback.
  BindProperty("my_value_with_callback", new PropertyCallbackExample());
  // Bind property with a getter callback.
  BindGetterCallback("same", base::Bind(&CppBindingExample::same,
                                        base::Unretained(this)));

  // Map methods.  See comment above about names.
  BindCallback("echoValue", base::Bind(&CppBindingExample::echoValue,
                                       base::Unretained(this)));
  BindCallback("echoType", base::Bind(&CppBindingExample::echoType,
                                      base::Unretained(this)));
  BindCallback("plus", base::Bind(&CppBindingExample::plus,
                                  base::Unretained(this)));

  // The fallback method is called when a nonexistent method is called on an
  // object. If none is specified, calling a nonexistent method causes an
  // exception to be thrown and the JavaScript execution is stopped.
  BindFallbackCallback(base::Bind(&CppBindingExample::fallbackMethod,
                                  base::Unretained(this)));

  my_value.Set(10);
  my_other_value.Set("Reinitialized!");
}

void CppBindingExample::echoValue(const CppArgumentList& args,
                                  CppVariant* result) {
  if (args.size() < 1) {
    result->SetNull();
    return;
  }
  result->Set(args[0]);
}

void CppBindingExample::echoType(const CppArgumentList& args,
                                 CppVariant* result) {
  if (args.size() < 1) {
    result->SetNull();
    return;
  }
  // Note that if args[0] is a string, the following assignment implicitly
  // makes a copy of that string, which may have an undesirable impact on
  // performance.
  CppVariant arg1 = args[0];
  if (arg1.isBool())
    result->Set(true);
  else if (arg1.isInt32())
    result->Set(7);
  else if (arg1.isDouble())
    result->Set(3.14159);
  else if (arg1.isString())
    result->Set("Success!");
}

void CppBindingExample::plus(const CppArgumentList& args,
                             CppVariant* result) {
  if (args.size() < 2) {
    result->SetNull();
    return;
  }

  CppVariant arg1 = args[0];
  CppVariant arg2 = args[1];

  if (!arg1.isNumber() || !arg2.isNumber()) {
    result->SetNull();
    return;
  }

  // The value of a CppVariant may be read directly from its NPVariant struct.
  // (However, it should only be set using one of the Set() functions.)
  double sum = 0.;
  if (arg1.isDouble())
    sum += arg1.value.doubleValue;
  else if (arg1.isInt32())
    sum += arg1.value.intValue;

  if (arg2.isDouble())
    sum += arg2.value.doubleValue;
  else if (arg2.isInt32())
    sum += arg2.value.intValue;

  result->Set(sum);
}

void CppBindingExample::same(CppVariant* result) {
  result->Set(42);
}

void CppBindingExample::fallbackMethod(const CppArgumentList& args,
                                       CppVariant* result) {
  printf("Error: unknown JavaScript method invoked.\n");
}

}  // namespace content
