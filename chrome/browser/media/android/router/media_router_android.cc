// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/android/router/media_router_android.h"

#include "base/android/context_utils.h"
#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/guid.h"
#include "base/logging.h"
#include "base/memory/scoped_vector.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/android/tab_android.h"
#include "chrome/browser/media/router/media_routes_observer.h"
#include "chrome/browser/media/router/media_sinks_observer.h"
#include "chrome/browser/media/router/presentation_session_messages_observer.h"
#include "jni/ChromeMediaRouter_jni.h"
#include "url/gurl.h"

using base::android::ConvertUTF8ToJavaString;
using base::android::ConvertJavaStringToUTF8;
using base::android::ScopedJavaLocalRef;
using base::android::AttachCurrentThread;

namespace media_router {

MediaRouterAndroid::MediaRouteRequest::MediaRouteRequest(
    const MediaSource& source,
    const std::string& presentation_id,
    const std::vector<MediaRouteResponseCallback>& callbacks)
    : media_source(source),
      presentation_id(presentation_id),
      callbacks(callbacks) {}

MediaRouterAndroid::MediaRouteRequest::~MediaRouteRequest() {}


MediaRouterAndroid::MediaRouterAndroid(content::BrowserContext*) {
  JNIEnv* env = base::android::AttachCurrentThread();
  java_media_router_.Reset(Java_ChromeMediaRouter_create(
      env,
      reinterpret_cast<jlong>(this),
      base::android::GetApplicationContext()));
}

MediaRouterAndroid::~MediaRouterAndroid() {
}

// static
bool MediaRouterAndroid::Register(JNIEnv* env) {
  bool ret = RegisterNativesImpl(env);
  DCHECK(g_ChromeMediaRouter_clazz);
  return ret;
}

void MediaRouterAndroid::CreateRoute(
    const MediaSource::Id& source_id,
    const MediaSink::Id& sink_id,
    const GURL& origin,
    content::WebContents* web_contents,
    const std::vector<MediaRouteResponseCallback>& callbacks) {
  if (!origin.is_valid()) {
    for (const MediaRouteResponseCallback& callback : callbacks)
      callback.Run(nullptr, "", "Invalid origin");
    return;
  }

  // TODO(avayvod): unify presentation id generation code between platforms.
  // https://crbug.com/522239
  std::string presentation_id("mr_");
  presentation_id += base::GenerateGUID();

  int tab_id = -1;
  TabAndroid* tab = web_contents
      ? TabAndroid::FromWebContents(web_contents) : nullptr;
  if (tab)
    tab_id = tab->GetAndroidId();

  MediaRouteRequest* request = new MediaRouteRequest(
      MediaSource(source_id),
      presentation_id,
      callbacks);
  int route_request_id = route_requests_.Add(request);

  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jstring> jsource_id =
          base::android::ConvertUTF8ToJavaString(env, source_id);
  ScopedJavaLocalRef<jstring> jsink_id =
          base::android::ConvertUTF8ToJavaString(env, sink_id);
  ScopedJavaLocalRef<jstring> jpresentation_id =
          base::android::ConvertUTF8ToJavaString(env, presentation_id);
  ScopedJavaLocalRef<jstring> jorigin =
          base::android::ConvertUTF8ToJavaString(env, origin.spec());

  Java_ChromeMediaRouter_createRoute(
      env,
      java_media_router_.obj(),
      jsource_id.obj(),
      jsink_id.obj(),
      jpresentation_id.obj(),
      jorigin.obj(),
      tab_id,
      route_request_id);
}

void MediaRouterAndroid::JoinRoute(
    const MediaSource::Id& source_id,
    const std::string& presentation_id,
    const GURL& origin,
    content::WebContents* web_contents,
    const std::vector<MediaRouteResponseCallback>& callbacks) {
  if (!origin.is_valid()) {
    for (const MediaRouteResponseCallback& callback : callbacks)
      callback.Run(nullptr, "", "Invalid origin");
    return;
  }

  int tab_id = -1;
  TabAndroid* tab = web_contents
      ? TabAndroid::FromWebContents(web_contents) : nullptr;
  if (tab)
    tab_id = tab->GetAndroidId();

  DVLOG(2) << "JoinRoute: " << source_id << ", " << presentation_id << ", "
           << origin.spec() << ", " << tab_id;

  MediaRouteRequest* request = new MediaRouteRequest(
      MediaSource(source_id),
      presentation_id,
      callbacks);
  int request_id = route_requests_.Add(request);

  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jstring> jsource_id =
          base::android::ConvertUTF8ToJavaString(env, source_id);
  ScopedJavaLocalRef<jstring> jpresentation_id =
          base::android::ConvertUTF8ToJavaString(env, presentation_id);
  ScopedJavaLocalRef<jstring> jorigin =
          base::android::ConvertUTF8ToJavaString(env, origin.spec());

  Java_ChromeMediaRouter_joinRoute(
      env,
      java_media_router_.obj(),
      jsource_id.obj(),
      jpresentation_id.obj(),
      jorigin.obj(),
      tab_id,
      request_id);
}

void MediaRouterAndroid::CloseRoute(const MediaRoute::Id& route_id) {
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jstring> jroute_id =
          base::android::ConvertUTF8ToJavaString(env, route_id);
  Java_ChromeMediaRouter_closeRoute(
      env, java_media_router_.obj(), jroute_id.obj());
}

void MediaRouterAndroid::SendRouteMessage(
    const MediaRoute::Id& route_id,
    const std::string& message,
    const SendRouteMessageCallback& callback) {
  int callback_id = message_callbacks_.Add(
      new SendRouteMessageCallback(callback));
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jstring> jroute_id =
          base::android::ConvertUTF8ToJavaString(env, route_id);
  ScopedJavaLocalRef<jstring> jmessage =
          base::android::ConvertUTF8ToJavaString(env, message);
  Java_ChromeMediaRouter_sendStringMessage(
      env,
      java_media_router_.obj(),
      jroute_id.obj(),
      jmessage.obj(),
      callback_id);
}

void MediaRouterAndroid::SendRouteBinaryMessage(
    const MediaRoute::Id& route_id,
    scoped_ptr<std::vector<uint8>> data,
    const SendRouteMessageCallback& callback) {
  int callback_id =
      message_callbacks_.Add(new SendRouteMessageCallback(callback));
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jstring> jroute_id =
      base::android::ConvertUTF8ToJavaString(env, route_id);
  ScopedJavaLocalRef<jbyteArray> jbyte_array =
      base::android::ToJavaByteArray(env, &((*data)[0]), data->size());
  Java_ChromeMediaRouter_sendBinaryMessage(env, java_media_router_.obj(),
                                           jroute_id.obj(), jbyte_array.obj(),
                                           callback_id);
}

void MediaRouterAndroid::AddIssue(const Issue& issue) {
  NOTIMPLEMENTED();
}

void MediaRouterAndroid::ClearIssue(const Issue::Id& issue_id) {
  NOTIMPLEMENTED();
}

void MediaRouterAndroid::OnPresentationSessionDetached(
    const MediaRoute::Id& route_id) {
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jstring> jroute_id =
          base::android::ConvertUTF8ToJavaString(env, route_id);
  Java_ChromeMediaRouter_detachRoute(
      env, java_media_router_.obj(), jroute_id.obj());
}

bool MediaRouterAndroid::HasLocalDisplayRoute() const {
  NOTIMPLEMENTED();
  return false;
}

bool MediaRouterAndroid::RegisterMediaSinksObserver(
    MediaSinksObserver* observer) {
  const std::string& source_id = observer->source().id();
  base::ObserverList<MediaSinksObserver>* observer_list =
      sinks_observers_.get(source_id);
  if (!observer_list) {
    observer_list = new base::ObserverList<MediaSinksObserver>;
    sinks_observers_.add(source_id, make_scoped_ptr(observer_list));
  } else {
    DCHECK(!observer_list->HasObserver(observer));
  }

  observer_list->AddObserver(observer);
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jstring> jsource_id =
      base::android::ConvertUTF8ToJavaString(env, source_id);
  return Java_ChromeMediaRouter_startObservingMediaSinks(
      env, java_media_router_.obj(), jsource_id.obj());
}

void MediaRouterAndroid::UnregisterMediaSinksObserver(
    MediaSinksObserver* observer) {
  const std::string& source_id = observer->source().id();
  auto* observer_list = sinks_observers_.get(source_id);
  if (!observer_list || !observer_list->HasObserver(observer))
    return;

  // If we are removing the final observer for the source, then stop
  // observing sinks for it.
  // might_have_observers() is reliable here on the assumption that this call
  // is not inside the ObserverList iteration.
  observer_list->RemoveObserver(observer);
  if (!observer_list->might_have_observers()) {
    sinks_observers_.erase(source_id);
    JNIEnv* env = base::android::AttachCurrentThread();
    ScopedJavaLocalRef<jstring> jsource_id =
        base::android::ConvertUTF8ToJavaString(env, source_id);
    Java_ChromeMediaRouter_stopObservingMediaSinks(
        env, java_media_router_.obj(), jsource_id.obj());
  }
}

void MediaRouterAndroid::RegisterMediaRoutesObserver(
    MediaRoutesObserver* observer) {
  DVLOG(2) << "Added MediaRoutesObserver: " << observer;
  routes_observers_.AddObserver(observer);
}

void MediaRouterAndroid::UnregisterMediaRoutesObserver(
    MediaRoutesObserver* observer) {
  if (!routes_observers_.HasObserver(observer))
    return;
  routes_observers_.RemoveObserver(observer);
}

void MediaRouterAndroid::RegisterIssuesObserver(IssuesObserver* observer) {
  NOTIMPLEMENTED();
}

void MediaRouterAndroid::UnregisterIssuesObserver(IssuesObserver* observer) {
  NOTIMPLEMENTED();
}

void MediaRouterAndroid::RegisterPresentationSessionMessagesObserver(
    PresentationSessionMessagesObserver* observer) {
  const MediaRoute::Id& route_id = observer->route_id();
  auto* observer_list = messages_observers_.get(route_id);
  if (!observer_list) {
    observer_list = new base::ObserverList<PresentationSessionMessagesObserver>;
    messages_observers_.add(route_id, make_scoped_ptr(observer_list));
  } else {
    DCHECK(!observer_list->HasObserver(observer));
  }

  observer_list->AddObserver(observer);
}

void MediaRouterAndroid::UnregisterPresentationSessionMessagesObserver(
    PresentationSessionMessagesObserver* observer) {
  const MediaRoute::Id& route_id = observer->route_id();
  auto* observer_list = messages_observers_.get(route_id);
  DCHECK(observer_list->HasObserver(observer));

  observer_list->RemoveObserver(observer);
  if (!observer_list->might_have_observers())
    messages_observers_.erase(route_id);
}

void MediaRouterAndroid::RegisterLocalMediaRoutesObserver(
    LocalMediaRoutesObserver* observer) {
  NOTIMPLEMENTED();
}

void MediaRouterAndroid::UnregisterLocalMediaRoutesObserver(
    LocalMediaRoutesObserver* observer) {
  NOTIMPLEMENTED();
}

void MediaRouterAndroid::OnSinksReceived(
    JNIEnv* env,
    jobject obj,
    jstring jsource_urn,
    jint jcount) {
  std::vector<MediaSink> sinks_converted;
  sinks_converted.reserve(jcount);
  for (int i = 0; i < jcount; ++i) {
    ScopedJavaLocalRef<jstring> jsink_urn =
        Java_ChromeMediaRouter_getSinkUrn(
            env, java_media_router_.obj(), jsource_urn, i);
    ScopedJavaLocalRef<jstring> jsink_name =
        Java_ChromeMediaRouter_getSinkName(
            env, java_media_router_.obj(), jsource_urn, i);
    sinks_converted.push_back(
        MediaSink(ConvertJavaStringToUTF8(env, jsink_urn.obj()),
        ConvertJavaStringToUTF8(env, jsink_name.obj()),
        MediaSink::GENERIC));
  }

  std::string source_urn = ConvertJavaStringToUTF8(env, jsource_urn);
  auto it = sinks_observers_.find(source_urn);
  if (it != sinks_observers_.end()) {
    FOR_EACH_OBSERVER(MediaSinksObserver, *it->second,
                      OnSinksReceived(sinks_converted));
  }
}

void MediaRouterAndroid::OnRouteCreated(
    JNIEnv* env,
    jobject obj,
    jstring jmedia_route_id,
    jstring jsink_id,
    jint jroute_request_id,
    jboolean jis_local) {
  MediaRouteRequest* request = route_requests_.Lookup(jroute_request_id);
  if (!request)
    return;

  MediaRoute route(ConvertJavaStringToUTF8(env, jmedia_route_id),
                   request->media_source,
                   ConvertJavaStringToUTF8(env, jsink_id), std::string(),
                   jis_local, std::string(),
                   true);  // TODO(avayvod): Populate for_display.

  for (const MediaRouteResponseCallback& callback : request->callbacks)
    callback.Run(&route, request->presentation_id, std::string());

  route_requests_.Remove(jroute_request_id);

  active_routes_.push_back(route);
  FOR_EACH_OBSERVER(MediaRoutesObserver, routes_observers_,
                    OnRoutesUpdated(active_routes_));
}

void MediaRouterAndroid::OnRouteRequestError(
    JNIEnv* env,
    jobject obj,
    jstring jerror_text,
    jint jroute_request_id) {
  MediaRouteRequest* request = route_requests_.Lookup(jroute_request_id);
  if (!request)
    return;

  std::string error_text = ConvertJavaStringToUTF8(env, jerror_text);

  for (const MediaRouteResponseCallback& callback : request->callbacks)
    callback.Run(nullptr, std::string(), error_text);

  route_requests_.Remove(jroute_request_id);
}

void MediaRouterAndroid::OnRouteClosed(JNIEnv* env,
                                       jobject obj,
                                       jstring jmedia_route_id) {
  MediaRoute::Id route_id = ConvertJavaStringToUTF8(env, jmedia_route_id);
  for (auto it = active_routes_.begin(); it != active_routes_.end(); ++it)
    if (it->media_route_id() == route_id) {
      active_routes_.erase(it);
      break;
    }

  FOR_EACH_OBSERVER(MediaRoutesObserver, routes_observers_,
                    OnRoutesUpdated(active_routes_));
}

void MediaRouterAndroid::OnMessageSentResult(
    JNIEnv* env, jobject obj, jboolean jsuccess, jint jcallback_id) {
  SendRouteMessageCallback* callback = message_callbacks_.Lookup(jcallback_id);
  callback->Run(jsuccess);
  message_callbacks_.Remove(jcallback_id);
}

// Notifies the media router about a message received from the media route.
void MediaRouterAndroid::OnMessage(
    JNIEnv* env, jobject obj, jstring jmedia_route_id, jstring jmessage) {
  MediaRoute::Id route_id = ConvertJavaStringToUTF8(env, jmedia_route_id);
  auto* observer_list = messages_observers_.get(route_id);
  if (!observer_list)
    return;

  ScopedVector<content::PresentationSessionMessage> session_messages;
  scoped_ptr<content::PresentationSessionMessage> message(
      new content::PresentationSessionMessage(content::TEXT));
  message->message = ConvertJavaStringToUTF8(env, jmessage);
  session_messages.push_back(message.Pass());

  FOR_EACH_OBSERVER(PresentationSessionMessagesObserver, *observer_list,
                    OnMessagesReceived(session_messages, true));
}

}  // namespace media_router
