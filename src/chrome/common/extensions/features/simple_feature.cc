// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/features/simple_feature.h"

#include <map>
#include <vector>

#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/sha1.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/features/feature_channel.h"

using chrome::VersionInfo;

namespace extensions {

namespace {

struct Mappings {
  Mappings() {
    extension_types["extension"] = Manifest::TYPE_EXTENSION;
    extension_types["theme"] = Manifest::TYPE_THEME;
    extension_types["packaged_app"] = Manifest::TYPE_LEGACY_PACKAGED_APP;
    extension_types["hosted_app"] = Manifest::TYPE_HOSTED_APP;
    extension_types["platform_app"] = Manifest::TYPE_PLATFORM_APP;
    extension_types["shared_module"] = Manifest::TYPE_SHARED_MODULE;

    contexts["blessed_extension"] = Feature::BLESSED_EXTENSION_CONTEXT;
    contexts["unblessed_extension"] = Feature::UNBLESSED_EXTENSION_CONTEXT;
    contexts["content_script"] = Feature::CONTENT_SCRIPT_CONTEXT;
    contexts["web_page"] = Feature::WEB_PAGE_CONTEXT;

    locations["component"] = Feature::COMPONENT_LOCATION;

    platforms["chromeos"] = Feature::CHROMEOS_PLATFORM;

    channels["trunk"] = VersionInfo::CHANNEL_UNKNOWN;
    channels["canary"] = VersionInfo::CHANNEL_CANARY;
    channels["dev"] = VersionInfo::CHANNEL_DEV;
    channels["beta"] = VersionInfo::CHANNEL_BETA;
    channels["stable"] = VersionInfo::CHANNEL_STABLE;
  }

  std::map<std::string, Manifest::Type> extension_types;
  std::map<std::string, Feature::Context> contexts;
  std::map<std::string, Feature::Location> locations;
  std::map<std::string, Feature::Platform> platforms;
  std::map<std::string, VersionInfo::Channel> channels;
};

base::LazyInstance<Mappings> g_mappings = LAZY_INSTANCE_INITIALIZER;

std::string GetChannelName(VersionInfo::Channel channel) {
  typedef std::map<std::string, VersionInfo::Channel> ChannelsMap;
  ChannelsMap channels = g_mappings.Get().channels;
  for (ChannelsMap::iterator i = channels.begin(); i != channels.end(); ++i) {
    if (i->second == channel)
      return i->first;
  }
  NOTREACHED();
  return "unknown";
}

// TODO(aa): Can we replace all this manual parsing with JSON schema stuff?

void ParseSet(const base::DictionaryValue* value,
              const std::string& property,
              std::set<std::string>* set) {
  const base::ListValue* list_value = NULL;
  if (!value->GetList(property, &list_value))
    return;

  set->clear();
  for (size_t i = 0; i < list_value->GetSize(); ++i) {
    std::string str_val;
    CHECK(list_value->GetString(i, &str_val)) << property << " " << i;
    set->insert(str_val);
  }
}

template<typename T>
void ParseEnum(const std::string& string_value,
               T* enum_value,
               const std::map<std::string, T>& mapping) {
  typename std::map<std::string, T>::const_iterator iter =
      mapping.find(string_value);
  CHECK(iter != mapping.end()) << string_value;
  *enum_value = iter->second;
}

template<typename T>
void ParseEnum(const base::DictionaryValue* value,
               const std::string& property,
               T* enum_value,
               const std::map<std::string, T>& mapping) {
  std::string string_value;
  if (!value->GetString(property, &string_value))
    return;

  ParseEnum(string_value, enum_value, mapping);
}

template<typename T>
void ParseEnumSet(const base::DictionaryValue* value,
                  const std::string& property,
                  std::set<T>* enum_set,
                  const std::map<std::string, T>& mapping) {
  if (!value->HasKey(property))
    return;

  enum_set->clear();

  std::string property_string;
  if (value->GetString(property, &property_string)) {
    if (property_string == "all") {
      for (typename std::map<std::string, T>::const_iterator j =
               mapping.begin(); j != mapping.end(); ++j) {
        enum_set->insert(j->second);
      }
    }
    return;
  }

  std::set<std::string> string_set;
  ParseSet(value, property, &string_set);
  for (std::set<std::string>::iterator iter = string_set.begin();
       iter != string_set.end(); ++iter) {
    T enum_value = static_cast<T>(0);
    ParseEnum(*iter, &enum_value, mapping);
    enum_set->insert(enum_value);
  }
}

void ParseURLPatterns(const base::DictionaryValue* value,
                      const std::string& key,
                      URLPatternSet* set) {
  const base::ListValue* matches = NULL;
  if (value->GetList(key, &matches)) {
    set->ClearPatterns();
    for (size_t i = 0; i < matches->GetSize(); ++i) {
      std::string pattern;
      CHECK(matches->GetString(i, &pattern));
      set->AddPattern(URLPattern(URLPattern::SCHEME_ALL, pattern));
    }
  }
}

// Gets a human-readable name for the given extension type.
std::string GetDisplayTypeName(Manifest::Type type) {
  switch (type) {
    case Manifest::TYPE_UNKNOWN:
      return "unknown";
    case Manifest::TYPE_EXTENSION:
      return "extension";
    case Manifest::TYPE_HOSTED_APP:
      return "hosted app";
    case Manifest::TYPE_LEGACY_PACKAGED_APP:
      return "legacy packaged app";
    case Manifest::TYPE_PLATFORM_APP:
      return "packaged app";
    case Manifest::TYPE_THEME:
      return "theme";
    case Manifest::TYPE_USER_SCRIPT:
      return "user script";
    case Manifest::TYPE_SHARED_MODULE:
      return "shared module";
  }

  NOTREACHED();
  return std::string();
}

std::string HashExtensionId(const std::string& extension_id) {
  const std::string id_hash = base::SHA1HashString(extension_id);
  DCHECK(id_hash.length() == base::kSHA1Length);
  return base::HexEncode(id_hash.c_str(), id_hash.length());
}

}  // namespace

SimpleFeature::SimpleFeature()
  : location_(UNSPECIFIED_LOCATION),
    platform_(UNSPECIFIED_PLATFORM),
    min_manifest_version_(0),
    max_manifest_version_(0),
    channel_(VersionInfo::CHANNEL_UNKNOWN),
    has_parent_(false),
    channel_has_been_set_(false) {
}

SimpleFeature::SimpleFeature(const SimpleFeature& other)
    : whitelist_(other.whitelist_),
      extension_types_(other.extension_types_),
      contexts_(other.contexts_),
      matches_(other.matches_),
      location_(other.location_),
      platform_(other.platform_),
      min_manifest_version_(other.min_manifest_version_),
      max_manifest_version_(other.max_manifest_version_),
      channel_(other.channel_),
      has_parent_(other.has_parent_),
      channel_has_been_set_(other.channel_has_been_set_) {
}

SimpleFeature::~SimpleFeature() {
}

bool SimpleFeature::Equals(const SimpleFeature& other) const {
  return whitelist_ == other.whitelist_ &&
      extension_types_ == other.extension_types_ &&
      contexts_ == other.contexts_ &&
      matches_ == other.matches_ &&
      location_ == other.location_ &&
      platform_ == other.platform_ &&
      min_manifest_version_ == other.min_manifest_version_ &&
      max_manifest_version_ == other.max_manifest_version_ &&
      channel_ == other.channel_ &&
      has_parent_ == other.has_parent_ &&
      channel_has_been_set_ == other.channel_has_been_set_;
}

std::string SimpleFeature::Parse(const base::DictionaryValue* value) {
  ParseURLPatterns(value, "matches", &matches_);
  ParseSet(value, "whitelist", &whitelist_);
  ParseSet(value, "dependencies", &dependencies_);
  ParseEnumSet<Manifest::Type>(value, "extension_types", &extension_types_,
                                g_mappings.Get().extension_types);
  ParseEnumSet<Context>(value, "contexts", &contexts_,
                        g_mappings.Get().contexts);
  ParseEnum<Location>(value, "location", &location_,
                      g_mappings.Get().locations);
  ParseEnum<Platform>(value, "platform", &platform_,
                      g_mappings.Get().platforms);
  value->GetInteger("min_manifest_version", &min_manifest_version_);
  value->GetInteger("max_manifest_version", &max_manifest_version_);
  ParseEnum<VersionInfo::Channel>(
      value, "channel", &channel_,
      g_mappings.Get().channels);

  no_parent_ = false;
  value->GetBoolean("noparent", &no_parent_);

  // The "trunk" channel uses VersionInfo::CHANNEL_UNKNOWN, so we need to keep
  // track of whether the channel has been set or not separately.
  channel_has_been_set_ |= value->HasKey("channel");
  if (!channel_has_been_set_ && dependencies_.empty())
    return name() + ": Must supply a value for channel or dependencies.";

  if (matches_.is_empty() && contexts_.count(WEB_PAGE_CONTEXT) != 0) {
    return name() + ": Allowing web_page contexts requires supplying a value " +
        "for matches.";
  }

  return std::string();
}

Feature::Availability SimpleFeature::IsAvailableToManifest(
    const std::string& extension_id,
    Manifest::Type type,
    Location location,
    int manifest_version,
    Platform platform) const {
  // Component extensions can access any feature.
  if (location == COMPONENT_LOCATION)
    return CreateAvailability(IS_AVAILABLE, type);

  if (!whitelist_.empty()) {
    if (!IsIdInWhitelist(extension_id)) {
      // TODO(aa): This is gross. There should be a better way to test the
      // whitelist.
      CommandLine* command_line = CommandLine::ForCurrentProcess();
      if (!command_line->HasSwitch(switches::kWhitelistedExtensionID))
        return CreateAvailability(NOT_FOUND_IN_WHITELIST, type);

      std::string whitelist_switch_value =
          CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
              switches::kWhitelistedExtensionID);
      if (extension_id != whitelist_switch_value)
        return CreateAvailability(NOT_FOUND_IN_WHITELIST, type);
    }
  }

  // HACK(kalman): user script -> extension. Solve this in a more generic way
  // when we compile feature files.
  Manifest::Type type_to_check = (type == Manifest::TYPE_USER_SCRIPT) ?
      Manifest::TYPE_EXTENSION : type;
  if (!extension_types_.empty() &&
      extension_types_.find(type_to_check) == extension_types_.end()) {
    return CreateAvailability(INVALID_TYPE, type);
  }

  if (location_ != UNSPECIFIED_LOCATION && location_ != location)
    return CreateAvailability(INVALID_LOCATION, type);

  if (platform_ != UNSPECIFIED_PLATFORM && platform_ != platform)
    return CreateAvailability(INVALID_PLATFORM, type);

  if (min_manifest_version_ != 0 && manifest_version < min_manifest_version_)
    return CreateAvailability(INVALID_MIN_MANIFEST_VERSION, type);

  if (max_manifest_version_ != 0 && manifest_version > max_manifest_version_)
    return CreateAvailability(INVALID_MAX_MANIFEST_VERSION, type);

  if (channel_has_been_set_ && channel_ < GetCurrentChannel())
    return CreateAvailability(UNSUPPORTED_CHANNEL, type);

  return CreateAvailability(IS_AVAILABLE, type);
}

Feature::Availability SimpleFeature::IsAvailableToContext(
    const Extension* extension,
    SimpleFeature::Context context,
    const GURL& url,
    SimpleFeature::Platform platform) const {
  if (extension) {
    Availability result = IsAvailableToManifest(
        extension->id(),
        extension->GetType(),
        ConvertLocation(extension->location()),
        extension->manifest_version(),
        platform);
    if (!result.is_available())
      return result;
  }

  if (!contexts_.empty() && contexts_.find(context) == contexts_.end()) {
    return extension ?
        CreateAvailability(INVALID_CONTEXT, extension->GetType()) :
        CreateAvailability(INVALID_CONTEXT);
  }

  if (!matches_.is_empty() && !matches_.MatchesURL(url))
    return CreateAvailability(INVALID_URL, url);

  return CreateAvailability(IS_AVAILABLE);
}

std::string SimpleFeature::GetAvailabilityMessage(
    AvailabilityResult result, Manifest::Type type, const GURL& url) const {
  switch (result) {
    case IS_AVAILABLE:
      return std::string();
    case NOT_FOUND_IN_WHITELIST:
      return base::StringPrintf(
          "'%s' is not allowed for specified extension ID.",
          name().c_str());
    case INVALID_URL:
      return base::StringPrintf("'%s' is not allowed on %s.",
                                name().c_str(), url.spec().c_str());
    case INVALID_TYPE: {
      std::string allowed_type_names;
      // Turn the set of allowed types into a vector so that it's easier to
      // inject the appropriate separator into the display string.
      std::vector<Manifest::Type> extension_types(
          extension_types_.begin(), extension_types_.end());
      for (size_t i = 0; i < extension_types.size(); i++) {
        // Pluralize type name.
        allowed_type_names += GetDisplayTypeName(extension_types[i]) + "s";
        if (i == extension_types_.size() - 2) {
          allowed_type_names += " and ";
        } else if (i != extension_types_.size() - 1) {
          allowed_type_names += ", ";
        }
      }

      return base::StringPrintf(
          "'%s' is only allowed for %s, and this is a %s.",
          name().c_str(),
          allowed_type_names.c_str(),
          GetDisplayTypeName(type).c_str());
    }
    case INVALID_CONTEXT:
      return base::StringPrintf(
          "'%s' is not allowed for specified context type content script, "
          " extension page, web page, etc.).",
          name().c_str());
    case INVALID_LOCATION:
      return base::StringPrintf(
          "'%s' is not allowed for specified install location.",
          name().c_str());
    case INVALID_PLATFORM:
      return base::StringPrintf(
          "'%s' is not allowed for specified platform.",
          name().c_str());
    case INVALID_MIN_MANIFEST_VERSION:
      return base::StringPrintf(
          "'%s' requires manifest version of at least %d.",
          name().c_str(),
          min_manifest_version_);
    case INVALID_MAX_MANIFEST_VERSION:
      return base::StringPrintf(
          "'%s' requires manifest version of %d or lower.",
          name().c_str(),
          max_manifest_version_);
    case NOT_PRESENT:
      return base::StringPrintf(
          "'%s' requires a different Feature that is not present.",
          name().c_str());
    case UNSUPPORTED_CHANNEL:
      return base::StringPrintf(
          "'%s' requires Google Chrome %s channel or newer, and this is the "
              "%s channel.",
          name().c_str(),
          GetChannelName(channel_).c_str(),
          GetChannelName(GetCurrentChannel()).c_str());
  }

  NOTREACHED();
  return std::string();
}

Feature::Availability SimpleFeature::CreateAvailability(
    AvailabilityResult result) const {
  return Availability(
      result, GetAvailabilityMessage(result, Manifest::TYPE_UNKNOWN, GURL()));
}

Feature::Availability SimpleFeature::CreateAvailability(
    AvailabilityResult result, Manifest::Type type) const {
  return Availability(result, GetAvailabilityMessage(result, type, GURL()));
}

Feature::Availability SimpleFeature::CreateAvailability(
    AvailabilityResult result,
    const GURL& url) const {
  return Availability(
      result, GetAvailabilityMessage(result, Manifest::TYPE_UNKNOWN, url));
}

std::set<Feature::Context>* SimpleFeature::GetContexts() {
  return &contexts_;
}

bool SimpleFeature::IsInternal() const {
  NOTREACHED();
  return false;
}

bool SimpleFeature::IsIdInWhitelist(const std::string& extension_id) const {
  // Belt-and-suspenders philosophy here. We should be pretty confident by this
  // point that we've validated the extension ID format, but in case something
  // slips through, we avoid a class of attack where creative ID manipulation
  // leads to hash collisions.
  if (extension_id.length() != 32)  // 128 bits / 4 = 32 mpdecimal characters
    return false;

  if (whitelist_.find(extension_id) != whitelist_.end() ||
      whitelist_.find(HashExtensionId(extension_id)) != whitelist_.end())
    return true;

  return false;
}

}  // namespace extensions
