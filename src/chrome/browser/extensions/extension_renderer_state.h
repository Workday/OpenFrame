// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_RENDERER_STATE_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_RENDERER_STATE_H_

#include <map>
#include <set>
#include <utility>

#include "base/basictypes.h"
#include "base/memory/singleton.h"

class WebViewGuest;

// This class keeps track of renderer state for use on the IO thread. All
// methods should be called on the IO thread except for Init and Shutdown.
class ExtensionRendererState {
 public:
  struct WebViewInfo {
    int embedder_process_id;
    int embedder_routing_id;
    int instance_id;
  };

  static ExtensionRendererState* GetInstance();

  // These are called on the UI thread to start and stop listening to tab
  // notifications.
  void Init();
  void Shutdown();

  // Looks up the information for the embedder <webview> for a given render
  // view, if one exists. Called on the IO thread.
  bool GetWebViewInfo(int guest_process_id, int guest_routing_id,
                      WebViewInfo* webview_info);

  // Looks up the tab and window ID for a given render view. Returns true
  // if we have the IDs in our map. Called on the IO thread.
  bool GetTabAndWindowId(
      int render_process_host_id, int routing_id, int* tab_id, int* window_id);

 private:
  class RenderViewHostObserver;
  class TabObserver;
  friend class TabObserver;
  friend class WebViewGuest;
  friend struct DefaultSingletonTraits<ExtensionRendererState>;

  typedef std::pair<int, int> RenderId;
  typedef std::pair<int, int> TabAndWindowId;
  typedef std::map<RenderId, TabAndWindowId> TabAndWindowIdMap;
  typedef std::map<RenderId, WebViewInfo> WebViewInfoMap;

  ExtensionRendererState();
  ~ExtensionRendererState();

  // Adds or removes a render view from our map.
  void SetTabAndWindowId(
      int render_process_host_id, int routing_id, int tab_id, int window_id);
  void ClearTabAndWindowId(
      int render_process_host_id, int routing_id);

  // Adds or removes a <webview> guest render process from the set.
  void AddWebView(int render_process_host_id, int routing_id,
                  const WebViewInfo& webview_info);
  void RemoveWebView(int render_process_host_id, int routing_id);

  TabObserver* observer_;
  TabAndWindowIdMap map_;
  WebViewInfoMap webview_info_map_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionRendererState);
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_RENDERER_STATE_H_
