// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>

#include <ppapi/cpp/completion_callback.h>
#include <ppapi/cpp/instance.h>
#include <ppapi/cpp/module.h>
#include <ppapi/cpp/var.h>

#include "eightball.h"
#include "nacl_io/nacl_io.h"
#include "reverse.h"

#if defined(NACL_SDK_DEBUG)
#define CONFIG_NAME "Debug"
#else
#define CONFIG_NAME "Release"
#endif

#if defined __arm__
#define NACL_ARCH "arm"
#elif defined __i686__
#define NACL_ARCH "x86_32"
#elif defined __x86_64__
#define NACL_ARCH "x86_64"
#else
#error "Unknown arch"
#endif

class DlOpenInstance : public pp::Instance {
 public:
  explicit DlOpenInstance(PP_Instance instance)
      : pp::Instance(instance),
        eightball_so_(NULL),
        reverse_so_(NULL),
        eightball_(NULL),
        reverse_(NULL),
        tid_(NULL) {}

  virtual ~DlOpenInstance() {}

  // Helper function to post a message back to the JS and stdout functions.
  void logmsg(const char* pStr) {
    PostMessage(pp::Var(std::string("log:") + pStr));
    fprintf(stdout, pStr);
  }

  // Initialize the module, staring a worker thread to load the shared object.
  virtual bool Init(uint32_t argc, const char* argn[], const char* argv[]) {
    nacl_io_init_ppapi(pp_instance(),
                       pp::Module::Get()->get_browser_interface());
    // Mount a HTTP mount at /http. All reads from /http/* will read from the
    // server.
    mount("", "/http", "httpfs", 0, "");

    logmsg("Spawning thread to cache .so files...\n");
    if (pthread_create(&tid_, NULL, LoadLibrariesOnWorker, this)) {
      logmsg("ERROR; pthread_create() failed.\n");
      return false;
    }
    return true;
  }

  // This function is called on a worker thread, and will call dlopen to load
  // the shared object.  In addition, note that this function does NOT call
  // dlclose, which would close the shared object and unload it from memory.
  void LoadLibrary() {
    const char reverse_so_path[] =
        "/http/glibc/" CONFIG_NAME "/libreverse_" NACL_ARCH ".so";
    const int32_t IMMEDIATELY = 0;
    eightball_so_ = dlopen("libeightball.so", RTLD_LAZY);
    reverse_so_ = dlopen(reverse_so_path, RTLD_LAZY);
    pp::CompletionCallback cc(LoadDoneCB, this);
    pp::Module::Get()->core()->CallOnMainThread(IMMEDIATELY, cc, 0);
  }

  // This function will run on the main thread and use the handle it stored by
  // the worker thread, assuming it successfully loaded, to get a pointer to the
  // message function in the shared object.
  void UseLibrary() {
    if (eightball_so_ != NULL) {
      intptr_t offset = (intptr_t) dlsym(eightball_so_, "Magic8Ball");
      eightball_ = (TYPE_eightball) offset;
      if (NULL == eightball_) {
        std::string message = "dlsym() returned NULL: ";
        message += dlerror();
        message += "\n";
        logmsg(message.c_str());
        return;
      }

      logmsg("Loaded libeightball.so\n");
    } else {
      logmsg("libeightball.so did not load\n");
    }

    if (reverse_so_ != NULL) {
      intptr_t offset = (intptr_t) dlsym(reverse_so_, "Reverse");
      reverse_ = (TYPE_reverse) offset;
      if (NULL == reverse_) {
        std::string message = "dlsym() returned NULL: ";
        message += dlerror();
        message += "\n";
        logmsg(message.c_str());
        return;
      }
      logmsg("Loaded libreverse.so\n");
    } else {
      logmsg("libreverse.so did not load\n");
    }
  }

  // Called by the browser to handle the postMessage() call in Javascript.
  virtual void HandleMessage(const pp::Var& var_message) {
    if (!var_message.is_string()) {
      logmsg("Message is not a string.\n");
      return;
    }

    std::string message = var_message.AsString();
    if (message == "eightball") {
      if (NULL == eightball_) {
        logmsg("Eightball library not loaded\n");
        return;
      }

      std::string ballmessage = "The Magic 8-Ball says: ";
      ballmessage += eightball_();
      ballmessage += "!\n";

      logmsg(ballmessage.c_str());
    } else if (message.find("reverse:") == 0) {
      if (NULL == reverse_) {
        logmsg("Reverse library not loaded\n");
        return;
      }

      std::string s = message.substr(strlen("reverse:"));
      char* result = reverse_(s.c_str());

      std::string message = "Your string reversed: \"";
      message += result;
      message += "\"\n";

      free(result);

      logmsg(message.c_str());
    } else {
      std::string errormsg = "Unexpected message: ";
      errormsg += message + "\n";
      logmsg(errormsg.c_str());
    }
  }

  static void* LoadLibrariesOnWorker(void* pInst) {
    DlOpenInstance* inst = static_cast<DlOpenInstance*>(pInst);
    inst->LoadLibrary();
    return NULL;
  }

  static void LoadDoneCB(void* pInst, int32_t result) {
    DlOpenInstance* inst = static_cast<DlOpenInstance*>(pInst);
    inst->UseLibrary();
  }

 private:
  void* eightball_so_;
  void* reverse_so_;
  TYPE_eightball eightball_;
  TYPE_reverse reverse_;
  pthread_t tid_;
};

class DlOpenModule : public pp::Module {
 public:
  DlOpenModule() : pp::Module() {}
  virtual ~DlOpenModule() {}

  // Create and return a DlOpenInstance object.
  virtual pp::Instance* CreateInstance(PP_Instance instance) {
    return new DlOpenInstance(instance);
  }
};

namespace pp {
Module* CreateModule() { return new DlOpenModule(); }
}  // namespace pp
