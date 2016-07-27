// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/font_service/public/cpp/font_loader.h"

#include "base/bind.h"
#include "base/trace_event/trace_event.h"
#include "components/font_service/public/cpp/font_service_thread.h"
#include "mojo/application/public/cpp/application_impl.h"
#include "mojo/application/public/cpp/connect.h"
#include "mojo/application/public/interfaces/shell.mojom.h"

namespace font_service {
namespace {
void OnGotContentHandlerID(uint32_t content_handler_id) {}
}  // namespace

FontLoader::FontLoader(mojo::Shell* shell) {
  mojo::ServiceProviderPtr font_service_provider;
  mojo::URLRequestPtr request(mojo::URLRequest::New());
  request->url = mojo::String::From("mojo:font_service");
  FontServicePtr font_service;
  shell->ConnectToApplication(request.Pass(), GetProxy(&font_service_provider),
                              nullptr, mojo::CreatePermissiveCapabilityFilter(),
                              base::Bind(&OnGotContentHandlerID));
  mojo::ConnectToService(font_service_provider.get(), &font_service);

  thread_ = new internal::FontServiceThread(font_service.Pass());
}

FontLoader::FontLoader(mojo::ApplicationImpl* application_impl) {
  FontServicePtr font_service;
  application_impl->ConnectToService("mojo:font_service", &font_service);

  thread_ = new internal::FontServiceThread(font_service.Pass());
}

FontLoader::~FontLoader() {}

void FontLoader::Shutdown() {
  thread_->Stop();
  thread_ = nullptr;
}

bool FontLoader::matchFamilyName(const char family_name[],
                                 SkTypeface::Style requested,
                                 FontIdentity* out_font_identifier,
                                 SkString* out_family_name,
                                 SkTypeface::Style* out_style) {
  TRACE_EVENT1("font_service", "FontServiceThread::MatchFamilyName",
               "family_name", family_name);
  return thread_->MatchFamilyName(family_name, requested, out_font_identifier,
                                  out_family_name, out_style);
}

SkStreamAsset* FontLoader::openStream(const FontIdentity& identity) {
  TRACE_EVENT2("font_loader", "FontLoader::openStream",
               "identity", identity.fID,
               "name", identity.fString.c_str());
  {
    base::AutoLock lock(lock_);
    auto mapped_font_files_it = mapped_font_files_.find(identity.fID);
    if (mapped_font_files_it != mapped_font_files_.end())
      return mapped_font_files_it->second->CreateMemoryStream();
  }

  scoped_refptr<internal::MappedFontFile> mapped_font_file =
      thread_->OpenStream(identity);
  if (!mapped_font_file)
    return nullptr;

  // Get notified with |mapped_font_file| is destroyed.
  mapped_font_file->set_observer(this);

  {
    base::AutoLock lock(lock_);
    auto mapped_font_files_it =
        mapped_font_files_.insert(std::make_pair(mapped_font_file->font_id(),
                                                 mapped_font_file.get()))
            .first;
    return mapped_font_files_it->second->CreateMemoryStream();
  }
}

void FontLoader::OnMappedFontFileDestroyed(internal::MappedFontFile* f) {
  TRACE_EVENT1("font_loader", "FontLoader::OnMappedFontFileDestroyed",
               "identity", f->font_id());
  base::AutoLock lock(lock_);
  mapped_font_files_.erase(f->font_id());
}

}  // namespace font_service
