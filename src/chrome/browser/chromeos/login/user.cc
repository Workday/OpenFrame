// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/user.h"

#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chromeos/login/default_user_images.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "grit/theme_resources.h"
#include "ui/base/resource/resource_bundle.h"

namespace chromeos {

namespace {

// Returns account name portion of an email.
std::string GetUserName(const std::string& email) {
  std::string::size_type i = email.find('@');
  if (i == 0 || i == std::string::npos) {
    return email;
  }
  return email.substr(0, i);
}

}  // namespace

class RegularUser : public User {
 public:
  explicit RegularUser(const std::string& email);
  virtual ~RegularUser();

  // Overridden from User:
  virtual UserType GetType() const OVERRIDE;
  virtual bool can_lock() const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(RegularUser);
};

class GuestUser : public User {
 public:
  GuestUser();
  virtual ~GuestUser();

  // Overridden from User:
  virtual UserType GetType() const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(GuestUser);
};

class KioskAppUser : public User {
 public:
  explicit KioskAppUser(const std::string& app_id);
  virtual ~KioskAppUser();

  // Overridden from User:
  virtual UserType GetType() const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(KioskAppUser);
};

class LocallyManagedUser : public User {
 public:
  explicit LocallyManagedUser(const std::string& username);
  virtual ~LocallyManagedUser();

  // Overridden from User:
  virtual UserType GetType() const OVERRIDE;
  virtual bool can_lock() const OVERRIDE;
  virtual std::string display_email() const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(LocallyManagedUser);
};

class RetailModeUser : public User {
 public:
  RetailModeUser();
  virtual ~RetailModeUser();

  // Overridden from User:
  virtual UserType GetType() const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(RetailModeUser);
};

class PublicAccountUser : public User {
 public:
  explicit PublicAccountUser(const std::string& email);
  virtual ~PublicAccountUser();

  // Overridden from User:
  virtual UserType GetType() const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(PublicAccountUser);
};

UserContext::UserContext() {
}

UserContext::UserContext(const std::string& username,
                         const std::string& password,
                         const std::string& auth_code)
    : username(username),
      password(password),
      auth_code(auth_code) {
}

UserContext::UserContext(const std::string& username,
                         const std::string& password,
                         const std::string& auth_code,
                         const std::string& username_hash)
    : username(username),
      password(password),
      auth_code(auth_code),
      username_hash(username_hash) {
}

UserContext::~UserContext() {
}

bool UserContext::operator==(const UserContext& context) const {
  return context.username == username &&
         context.password == password &&
         context.auth_code == auth_code &&
         context.username_hash == username_hash;
}

string16 User::GetDisplayName() const {
  // Fallback to the email account name in case display name haven't been set.
  return display_name_.empty() ?
      UTF8ToUTF16(GetAccountName(true)) :
      display_name_;
}

std::string User::GetAccountName(bool use_display_email) const {
  if (use_display_email && !display_email_.empty())
    return GetUserName(display_email_);
  else
    return GetUserName(email_);
}

bool User::HasDefaultImage() const {
  return image_index_ >= 0 && image_index_ < kDefaultImagesCount;
}

std::string User::display_email() const {
  return display_email_;
}

bool User::can_lock() const {
  return false;
}

std::string User::username_hash() const {
  return username_hash_;
}

bool User::is_logged_in() const {
  return is_logged_in_;
}

bool User::is_active() const {
  return is_active_;
}

User* User::CreateRegularUser(const std::string& email) {
  return new RegularUser(email);
}

User* User::CreateGuestUser() {
  return new GuestUser;
}

User* User::CreateKioskAppUser(const std::string& kiosk_app_username) {
  return new KioskAppUser(kiosk_app_username);
}

User* User::CreateLocallyManagedUser(const std::string& username) {
  return new LocallyManagedUser(username);
}

User* User::CreateRetailModeUser() {
  return new RetailModeUser;
}

User* User::CreatePublicAccountUser(const std::string& email) {
  return new PublicAccountUser(email);
}

User::User(const std::string& email)
    : email_(email),
      oauth_token_status_(OAUTH_TOKEN_STATUS_UNKNOWN),
      image_index_(kInvalidImageIndex),
      image_is_stub_(false),
      image_is_loading_(false),
      is_logged_in_(false),
      is_active_(false) {
}

User::~User() {}

void User::SetImage(const UserImage& user_image, int image_index) {
  user_image_ = user_image;
  image_index_ = image_index;
  image_is_stub_ = false;
  image_is_loading_ = false;
  DCHECK(HasDefaultImage() || user_image.has_raw_image());
}

void User::SetImageURL(const GURL& image_url) {
  user_image_.set_url(image_url);
}

void User::SetStubImage(int image_index, bool is_loading) {
  user_image_ = UserImage(
      *ResourceBundle::GetSharedInstance().
          GetImageSkiaNamed(IDR_PROFILE_PICTURE_LOADING));
  image_index_ = image_index;
  image_is_stub_ = true;
  image_is_loading_ = is_loading;
}

RegularUser::RegularUser(const std::string& email) : User(email) {
  set_display_email(email);
}

RegularUser::~RegularUser() {}

User::UserType RegularUser::GetType() const {
  return USER_TYPE_REGULAR;
}

bool RegularUser::can_lock() const {
  return true;
}

GuestUser::GuestUser() : User(UserManager::kGuestUserName) {
  set_display_email(std::string());
}

GuestUser::~GuestUser() {}

User::UserType GuestUser::GetType() const {
  return USER_TYPE_GUEST;
}

KioskAppUser::KioskAppUser(const std::string& kiosk_app_username)
    : User(kiosk_app_username) {
  set_display_email(std::string());
}

KioskAppUser::~KioskAppUser() {}

User::UserType KioskAppUser::GetType() const {
  return USER_TYPE_KIOSK_APP;
}

LocallyManagedUser::LocallyManagedUser(const std::string& username)
    : User(username) {
}

LocallyManagedUser::~LocallyManagedUser() {}

User::UserType LocallyManagedUser::GetType() const {
  return USER_TYPE_LOCALLY_MANAGED;
}

bool LocallyManagedUser::can_lock() const {
  return true;
}

std::string LocallyManagedUser::display_email() const {
  return UTF16ToUTF8(display_name());
}

RetailModeUser::RetailModeUser() : User(UserManager::kRetailModeUserName) {
  set_display_email(std::string());
}

RetailModeUser::~RetailModeUser() {}

User::UserType RetailModeUser::GetType() const {
  return USER_TYPE_RETAIL_MODE;
}

PublicAccountUser::PublicAccountUser(const std::string& email) : User(email) {
}

PublicAccountUser::~PublicAccountUser() {}

User::UserType PublicAccountUser::GetType() const {
  return USER_TYPE_PUBLIC_ACCOUNT;
}

}  // namespace chromeos
