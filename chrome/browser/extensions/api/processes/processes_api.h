// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_PROCESSES_PROCESSES_API_H__
#define CHROME_BROWSER_EXTENSIONS_API_PROCESSES_PROCESSES_API_H__

#include <set>
#include <string>

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/extensions/api/profile_keyed_api_factory.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_function.h"
#include "chrome/browser/task_manager/task_manager.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_widget_host.h"

class Profile;

namespace base {
class ListValue;
}

namespace extensions {

// Observes the Task Manager and routes the notifications as events to the
// extension system.
class ProcessesEventRouter : public TaskManagerModelObserver,
                             public content::NotificationObserver {
 public:
  explicit ProcessesEventRouter(Profile* profile);
  virtual ~ProcessesEventRouter();

  // Called when an extension process wants to listen to process events.
  void ListenerAdded();

  // Called when an extension process with a listener exits or removes it.
  void ListenerRemoved();

  // Called on the first invocation of extension API function. This will call
  // out to the Task Manager to start listening for notifications. Returns
  // true if this was the first call and false if this has already been called.
  void StartTaskManagerListening();

  bool is_task_manager_listening() { return task_manager_listening_; }

 private:
  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // TaskManagerModelObserver methods.
  virtual void OnItemsAdded(int start, int length) OVERRIDE;
  virtual void OnModelChanged() OVERRIDE {}
  virtual void OnItemsChanged(int start, int length) OVERRIDE;
  virtual void OnItemsRemoved(int start, int length) OVERRIDE {}
  virtual void OnItemsToBeRemoved(int start, int length) OVERRIDE;

  // Internal helpers for processing notifications.
  void ProcessHangEvent(content::RenderWidgetHost* widget);
  void ProcessClosedEvent(
      content::RenderProcessHost* rph,
      content::RenderProcessHost::RendererClosedDetails* details);

  void DispatchEvent(const char* event_name,
                     scoped_ptr<base::ListValue> event_args);

  // Determines whether there is a registered listener for the specified event.
  // It helps to avoid collecing data if no one is interested in it.
  bool HasEventListeners(const std::string& event_name);

  // Used for tracking registrations to process related notifications.
  content::NotificationRegistrar registrar_;

  Profile* profile_;

  // TaskManager to observe for updates.
  TaskManagerModel* model_;

  // Count of listeners, so we avoid sending updates if no one is interested.
  int listeners_;

  // Indicator whether we've initialized the Task Manager listeners. This is
  // done once for the lifetime of this object.
  bool task_manager_listening_;

  DISALLOW_COPY_AND_ASSIGN(ProcessesEventRouter);
};

// The profile-keyed service that manages the processes extension API.
class ProcessesAPI : public ProfileKeyedAPI,
                     public EventRouter::Observer {
 public:
  explicit ProcessesAPI(Profile* profile);
  virtual ~ProcessesAPI();

  // BrowserContextKeyedService implementation.
  virtual void Shutdown() OVERRIDE;

  // ProfileKeyedAPI implementation.
  static ProfileKeyedAPIFactory<ProcessesAPI>* GetFactoryInstance();

  // Convenience method to get the ProcessesAPI for a profile.
  static ProcessesAPI* Get(Profile* profile);

  ProcessesEventRouter* processes_event_router();

  // EventRouter::Observer implementation.
  virtual void OnListenerAdded(const EventListenerInfo& details) OVERRIDE;
  virtual void OnListenerRemoved(const EventListenerInfo& details) OVERRIDE;

 private:
  friend class ProfileKeyedAPIFactory<ProcessesAPI>;

  Profile* profile_;

  // ProfileKeyedAPI implementation.
  static const char* service_name() {
    return "ProcessesAPI";
  }
  static const bool kServiceRedirectedInIncognito = true;
  static const bool kServiceIsNULLWhileTesting = true;

  // Created lazily on first access.
  scoped_ptr<ProcessesEventRouter> processes_event_router_;
};

// This extension function returns the Process object for the renderer process
// currently in use by the specified Tab.
class GetProcessIdForTabFunction : public AsyncExtensionFunction,
                                   public content::NotificationObserver {
 public:
  GetProcessIdForTabFunction();

 private:
  virtual ~GetProcessIdForTabFunction() {}
  virtual bool RunImpl() OVERRIDE;

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  void GetProcessIdForTab();

  content::NotificationRegistrar registrar_;

  // Storage for the tab ID parameter.
  int tab_id_;

  DECLARE_EXTENSION_FUNCTION("experimental.processes.getProcessIdForTab",
                             EXPERIMENTAL_PROCESSES_GETPROCESSIDFORTAB)
};

// Extension function that allows terminating Chrome subprocesses, by supplying
// the unique ID for the process coming from the ChildProcess ID pool.
// Using unique IDs instead of OS process IDs allows two advantages:
// * guaranteed uniqueness, since OS process IDs can be reused
// * guards against killing non-Chrome processes
class TerminateFunction : public AsyncExtensionFunction,
                          public content::NotificationObserver {
 public:
  TerminateFunction();

 private:
  virtual ~TerminateFunction() {}
  virtual bool RunImpl() OVERRIDE;

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  void TerminateProcess();

  content::NotificationRegistrar registrar_;

  // Storage for the process ID parameter.
  int process_id_;

  DECLARE_EXTENSION_FUNCTION("experimental.processes.terminate",
                             EXPERIMENTAL_PROCESSES_TERMINATE)
};

// Extension function which returns a set of Process objects, containing the
// details corresponding to the process IDs supplied as input.
class GetProcessInfoFunction : public AsyncExtensionFunction,
                               public content::NotificationObserver {
 public:
  GetProcessInfoFunction();

 private:
  virtual ~GetProcessInfoFunction();
  virtual bool RunImpl() OVERRIDE;

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  void GatherProcessInfo();

  content::NotificationRegistrar registrar_;

  // Member variables to store the function parameters
  std::vector<int> process_ids_;
#if defined(ENABLE_TASK_MANAGER)
  bool memory_;
#endif

  DECLARE_EXTENSION_FUNCTION("experimental.processes.getProcessInfo",
                             EXPERIMENTAL_PROCESSES_GETPROCESSINFO)
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_PROCESSES_PROCESSES_API_H__
