// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/net/onc_utils.h"

#include "base/logging.h"
#include "base/values.h"
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/ui_proxy_config.h"
#include "chrome/browser/prefs/proxy_config_dictionary.h"
#include "chromeos/network/onc/onc_utils.h"
#include "net/base/host_port_pair.h"
#include "net/proxy/proxy_bypass_rules.h"
#include "net/proxy/proxy_server.h"
#include "url/gurl.h"

namespace chromeos {
namespace onc {

namespace {

net::ProxyServer ConvertOncProxyLocationToHostPort(
    net::ProxyServer::Scheme default_proxy_scheme,
    const base::DictionaryValue& onc_proxy_location) {
  std::string host;
  onc_proxy_location.GetStringWithoutPathExpansion(onc::proxy::kHost, &host);
  // Parse |host| according to the format [<scheme>"://"]<server>[":"<port>].
  net::ProxyServer proxy_server =
      net::ProxyServer::FromURI(host, default_proxy_scheme);
  int port = 0;
  onc_proxy_location.GetIntegerWithoutPathExpansion(onc::proxy::kPort, &port);

  // Replace the port parsed from |host| by the provided |port|.
  return net::ProxyServer(
      proxy_server.scheme(),
      net::HostPortPair(proxy_server.host_port_pair().host(),
                        static_cast<uint16>(port)));
}

void AppendProxyServerForScheme(
    const base::DictionaryValue& onc_manual,
    const std::string& onc_scheme,
    std::string* spec) {
  const base::DictionaryValue* onc_proxy_location = NULL;
  if (!onc_manual.GetDictionaryWithoutPathExpansion(onc_scheme,
                                                    &onc_proxy_location)) {
    return;
  }

  net::ProxyServer::Scheme default_proxy_scheme = net::ProxyServer::SCHEME_HTTP;
  std::string url_scheme;
  if (onc_scheme == proxy::kFtp) {
    url_scheme = "ftp";
  } else if (onc_scheme == proxy::kHttp) {
    url_scheme = "http";
  } else if (onc_scheme == proxy::kHttps) {
    url_scheme = "https";
  } else if (onc_scheme == proxy::kSocks) {
    default_proxy_scheme = net::ProxyServer::SCHEME_SOCKS4;
    url_scheme = "socks";
  } else {
    NOTREACHED();
  }

  net::ProxyServer proxy_server = ConvertOncProxyLocationToHostPort(
      default_proxy_scheme, *onc_proxy_location);

  UIProxyConfig::EncodeAndAppendProxyServer(url_scheme, proxy_server, spec);
}

net::ProxyBypassRules ConvertOncExcludeDomainsToBypassRules(
    const base::ListValue& onc_exclude_domains) {
  net::ProxyBypassRules rules;
  for (base::ListValue::const_iterator it = onc_exclude_domains.begin();
       it != onc_exclude_domains.end(); ++it) {
    std::string rule;
    (*it)->GetAsString(&rule);
    rules.AddRuleFromString(rule);
  }
  return rules;
}

}  // namespace

scoped_ptr<base::DictionaryValue> ConvertOncProxySettingsToProxyConfig(
    const base::DictionaryValue& onc_proxy_settings) {
  std::string type;
  onc_proxy_settings.GetStringWithoutPathExpansion(proxy::kType, &type);
  scoped_ptr<DictionaryValue> proxy_dict;

  if (type == proxy::kDirect) {
    proxy_dict.reset(ProxyConfigDictionary::CreateDirect());
  } else if (type == proxy::kWPAD) {
    proxy_dict.reset(ProxyConfigDictionary::CreateAutoDetect());
  } else if (type == proxy::kPAC) {
    std::string pac_url;
    onc_proxy_settings.GetStringWithoutPathExpansion(proxy::kPAC, &pac_url);
    GURL url(pac_url);
    DCHECK(url.is_valid())
        << "PAC field is invalid for this ProxySettings.Type";
    proxy_dict.reset(ProxyConfigDictionary::CreatePacScript(url.spec(),
                                                            false));
  } else if (type == proxy::kManual) {
    const base::DictionaryValue* manual_dict = NULL;
    onc_proxy_settings.GetDictionaryWithoutPathExpansion(proxy::kManual,
                                                         &manual_dict);
    std::string manual_spec;
    AppendProxyServerForScheme(*manual_dict, proxy::kFtp, &manual_spec);
    AppendProxyServerForScheme(*manual_dict, proxy::kHttp, &manual_spec);
    AppendProxyServerForScheme(*manual_dict, proxy::kSocks, &manual_spec);
    AppendProxyServerForScheme(*manual_dict, proxy::kHttps, &manual_spec);

    const base::ListValue* exclude_domains = NULL;
    net::ProxyBypassRules bypass_rules;
    if (onc_proxy_settings.GetListWithoutPathExpansion(proxy::kExcludeDomains,
                                                       &exclude_domains)) {
      bypass_rules.AssignFrom(
          ConvertOncExcludeDomainsToBypassRules(*exclude_domains));
    }
    proxy_dict.reset(ProxyConfigDictionary::CreateFixedServers(
        manual_spec, bypass_rules.ToString()));
  } else {
    NOTREACHED();
  }
  return proxy_dict.Pass();
}

namespace {

// This class defines which string placeholders of ONC are replaced by which
// user attribute.
class UserStringSubstitution : public chromeos::onc::StringSubstitution {
 public:
  explicit UserStringSubstitution(const chromeos::User* user) : user_(user) {}
  virtual ~UserStringSubstitution() {}

  virtual bool GetSubstitute(const std::string& placeholder,
                             std::string* substitute) const OVERRIDE {
    if (placeholder == chromeos::onc::substitutes::kLoginIDField)
      *substitute = user_->GetAccountName(false);
    else if (placeholder == chromeos::onc::substitutes::kEmailField)
      *substitute = user_->email();
    else
      return false;
    return true;
  }

 private:
  const chromeos::User* user_;

  DISALLOW_COPY_AND_ASSIGN(UserStringSubstitution);
};

const chromeos::User* GetLoggedInUserByHash(const std::string& userhash) {
  const chromeos::UserList& users =
      chromeos::UserManager::Get()->GetLoggedInUsers();
  for (chromeos::UserList::const_iterator it = users.begin(); it != users.end();
       ++it) {
    if ((*it)->username_hash() == userhash)
      return *it;
  }
  return NULL;
}

}  // namespace

void ExpandStringPlaceholdersInNetworksForUser(
    const std::string& hashed_username,
    base::ListValue* network_configs) {
  const chromeos::User* user = GetLoggedInUserByHash(hashed_username);
  if (!user) {
    // In tests no user may be logged in. It's not harmful if we just don't
    // expand the strings.
    return;
  }
  UserStringSubstitution substitution(user);
  chromeos::onc::ExpandStringsInNetworks(substitution, network_configs);
}

}  // namespace onc
}  // namespace chromeos
