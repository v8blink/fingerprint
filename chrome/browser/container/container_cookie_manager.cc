
#include "chrome/browser/container/container_cookie_manager.h"

#include <algorithm>
#include <random>
#include <sstream>
#include <string_view>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "content/public/browser/browser_context.h"
#include "url/gurl.h"

namespace tab_container {

namespace {

std::string IsolationModeToString(CookieIsolationMode mode) {
  switch (mode) {
    case CookieIsolationMode::kNone: return "None";
    case CookieIsolationMode::kFull: return "Full";
    case CookieIsolationMode::kDomainBased: return "DomainBased";
    case CookieIsolationMode::kFirstPartyIsolation: return "FirstPartyIsolation";
    case CookieIsolationMode::kDynamic: return "Dynamic";
  }
  return "Unknown";
}

std::string PolicyToString(CookiePolicyType policy) {
  switch (policy) {
    case CookiePolicyType::kAllowAll: return "AllowAll";
    case CookiePolicyType::kBlockThirdParty: return "BlockThirdParty";
    case CookiePolicyType::kBlockAll: return "BlockAll";
    case CookiePolicyType::kSessionOnly: return "SessionOnly";
    case CookiePolicyType::kFirstPartyOnly: return "FirstPartyOnly";
    case CookiePolicyType::kWhitelistOnly: return "WhitelistOnly";
  }
  return "Unknown";
}

}  

bool ContainerCookie::IsExpired() const {
  if (expiry_time.is_null()) {
    return false;  
  }
  return base::Time::Now() > expiry_time;
}

bool ContainerCookie::IsSession() const {
  return expiry_time.is_null();
}

size_t ContainerCookie::GetSizeBytes() const {
  return name.size() + value.size() + domain.size() + path.size() + 
         same_site.size() + source_scheme.size();
}

std::string ContainerCookie::GetFullDomain() const {
  return domain.empty() ? "" : (domain[0] == '.' ? domain : "." + domain);
}

size_t ContainerCookieJar::GetTotalSize() const {
  size_t total = 0;
  for (const auto& cookie : cookies) {
    total += cookie.GetSizeBytes();
  }
  return total;
}

size_t ContainerCookieJar::GetCookieCount() const {
  return cookies.size();
}

std::vector<ContainerCookie> ContainerCookieJar::GetCookiesForDomain(
    const std::string& domain) const {
  std::vector<ContainerCookie> result;
  for (const auto& cookie : cookies) {
    if (cookie.domain == domain || 
        cookie.GetFullDomain() == domain ||
        base::EndsWith(domain, cookie.GetFullDomain(), 
                       base::CompareCase::INSENSITIVE_ASCII)) {
      result.push_back(cookie);
    }
  }
  return result;
}

ContainerCookieManager::ContainerCookieManager(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  default_config_.isolation_mode = CookieIsolationMode::kFull;
  default_config_.policy = CookiePolicyType::kBlockThirdParty;
  default_config_.block_third_party_cookies = true;

  ;
}

ContainerCookieManager::~ContainerCookieManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ;
}

void ContainerCookieManager::AddObserver(ContainerCookieObserver* observer) {
  observers_.AddObserver(observer);
}

void ContainerCookieManager::RemoveObserver(ContainerCookieObserver* observer) {
  observers_.RemoveObserver(observer);
}

bool ContainerCookieManager::CreateCookieJar(
    const std::string& container_id,
    const ContainerCookieConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (cookie_jars_.find(container_id) != cookie_jars_.end()) {
    ;
    return false;
  }

  auto jar = std::make_unique<ContainerCookieJar>();
  jar->container_id = container_id;
  jar->config = config;
  jar->config.container_id = container_id;
  jar->created_at = base::Time::Now();
  jar->last_modified = base::Time::Now();

  cookie_jars_[container_id] = std::move(jar);

  ;

  return true;
}

ContainerCookieJar* ContainerCookieManager::GetOrCreateCookieJar(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = cookie_jars_.find(container_id);
  if (it != cookie_jars_.end()) {
    return it->second.get();
  }

  CreateCookieJar(container_id, default_config_);
  return cookie_jars_[container_id].get();
}

bool ContainerCookieManager::DestroyCookieJar(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = cookie_jars_.find(container_id);
  if (it == cookie_jars_.end()) {
    return false;
  }

  cookie_jars_.erase(it);
  cookie_events_.erase(container_id);

  ;

  return true;
}

ContainerCookieJar* ContainerCookieManager::GetCookieJar(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = cookie_jars_.find(container_id);
  if (it == cookie_jars_.end()) {
    return nullptr;
  }
  return it->second.get();
}

const ContainerCookieJar* ContainerCookieManager::GetCookieJar(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = cookie_jars_.find(container_id);
  if (it == cookie_jars_.end()) {
    return nullptr;
  }
  return it->second.get();
}

std::vector<std::string> 
ContainerCookieManager::GetAllCookieJarContainerIds() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<std::string> result;
  for (const auto& pair : cookie_jars_) {
    result.push_back(pair.first);
  }
  return result;
}

ContainerCookieManager::SetCookieResult
ContainerCookieManager::SetCookie(
    const std::string& container_id,
    const ContainerCookie& cookie,
    const std::string& top_frame_origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  SetCookieResult result;
  result.success = false;
  result.was_blocked = false;

  ContainerCookieJar* jar = GetOrCreateCookieJar(container_id);
  if (!jar) {
    result.error = "Failed to get or create cookie jar";
    return result;
  }

  if (!ValidateCookie(cookie)) {
    result.error = "Invalid cookie";
    return result;
  }

  CookieAllowedResult allowed = IsCookieAllowed(container_id, cookie, 
                                                 top_frame_origin);
  if (!allowed.allowed) {
    result.was_blocked = true;
    result.block_reason = allowed.reason;
    result.error = "Cookie blocked: " + allowed.reason;

    jar->cookies_blocked++;
    if (allowed.is_third_party) {
      jar->third_party_blocked++;
    }

    RecordEvent(container_id, CookieEventType::kBlocked, cookie.domain,
                cookie.name, allowed.reason, allowed.is_third_party, 
                top_frame_origin);
    NotifyCookieBlocked(container_id, cookie.domain, allowed.reason, 
                        allowed.is_third_party);

    if (debug_logging_enabled_) {
      ;
    }

    return result;
  }

  if (cookie.GetSizeBytes() > jar->config.max_cookie_size_bytes) {
    result.error = "Cookie exceeds size limit";
    result.was_blocked = true;
    result.block_reason = "size_exceeded";
    return result;
  }

  auto domain_cookies = jar->GetCookiesForDomain(cookie.domain);
  if (domain_cookies.size() >= jar->config.max_cookies_per_domain) {

    auto oldest_it = std::min_element(
        jar->cookies.begin(), jar->cookies.end(),
        [&cookie](const ContainerCookie& a, const ContainerCookie& b) {
          if (a.domain != cookie.domain) return false;
          if (b.domain != cookie.domain) return true;
          return a.last_access_time < b.last_access_time;
        });
    if (oldest_it != jar->cookies.end() && oldest_it->domain == cookie.domain) {
      jar->cookies.erase(oldest_it);
    }
  }

  if (jar->cookies.size() >= jar->config.max_total_cookies) {

    auto lra_it = std::min_element(
        jar->cookies.begin(), jar->cookies.end(),
        [](const ContainerCookie& a, const ContainerCookie& b) {
          return a.last_access_time < b.last_access_time;
        });
    if (lra_it != jar->cookies.end()) {
      jar->cookies.erase(lra_it);
    }
  }

  ContainerCookie new_cookie = cookie;
  new_cookie.creation_time = base::Time::Now();
  new_cookie.last_access_time = base::Time::Now();

  if (jar->config.same_site_override != SameSitePolicyOverride::kNone) {
    switch (jar->config.same_site_override) {
      case SameSitePolicyOverride::kStrict:
        new_cookie.same_site = "Strict";
        break;
      case SameSitePolicyOverride::kLax:
        new_cookie.same_site = "Lax";
        break;
      case SameSitePolicyOverride::kUnspecified:
        new_cookie.same_site = "";
        break;
      default:
        break;
    }
  }

  if (!jar->config.honor_cookie_expiry && !new_cookie.expiry_time.is_null()) {
    base::Time max_expiry = base::Time::Now() + jar->config.max_cookie_age;
    if (new_cookie.expiry_time > max_expiry) {
      new_cookie.expiry_time = max_expiry;
    }
  }

  if (jar->config.policy == CookiePolicyType::kSessionOnly) {
    new_cookie.expiry_time = base::Time();  
  }

  auto existing_it = std::find_if(
      jar->cookies.begin(), jar->cookies.end(),
      [&new_cookie](const ContainerCookie& c) {
        return c.name == new_cookie.name && 
               c.domain == new_cookie.domain &&
               c.path == new_cookie.path;
      });

  if (existing_it != jar->cookies.end()) {
    new_cookie.creation_time = existing_it->creation_time;
    *existing_it = new_cookie;

    RecordEvent(container_id, CookieEventType::kModified, new_cookie.domain,
                new_cookie.name, "", allowed.is_third_party, top_frame_origin);
  } else {
    jar->cookies.push_back(new_cookie);

    RecordEvent(container_id, CookieEventType::kSet, new_cookie.domain,
                new_cookie.name, "", allowed.is_third_party, top_frame_origin);
  }

  jar->cookies_set++;
  jar->last_modified = base::Time::Now();

  NotifyCookieSet(container_id, new_cookie, allowed.is_third_party);

  if (debug_logging_enabled_) {
    ;
  }

  result.success = true;
  return result;
}

std::vector<ContainerCookie> ContainerCookieManager::GetCookiesForUrl(
    const std::string& container_id,
    const std::string& url,
    const std::string& top_frame_origin) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return {};
  }

  std::string domain = ExtractDomain(url);
  GURL parsed_url(url);
  const std::string_view path_view =
      parsed_url.has_path() ? parsed_url.path() : std::string_view("/");
  std::string path(path_view.data(), path_view.size());
  bool is_secure = parsed_url.SchemeIsCryptographic();

  std::vector<ContainerCookie> result;

  for (const auto& cookie : jar->cookies) {

    if (cookie.IsExpired()) {
      continue;
    }

    if (!DomainMatches(cookie.domain, domain)) {
      continue;
    }

    if (!base::StartsWith(path, cookie.path, base::CompareCase::SENSITIVE)) {
      continue;
    }

    if (cookie.secure && !is_secure) {
      continue;
    }

    bool is_third_party = IsThirdPartyContext(domain, 
                                               ExtractDomain(top_frame_origin));
    if (is_third_party) {
      if (cookie.same_site == "Strict") {
        continue;
      }
      if (cookie.same_site == "Lax") {

        continue;
      }
    }

    result.push_back(cookie);
  }

  if (jar->config.randomize_cookie_order) {
    std::mt19937 rng(std::random_device{}());
    std::shuffle(result.begin(), result.end(), rng);
  }

  return result;
}

std::vector<ContainerCookie> ContainerCookieManager::GetAllCookies(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return {};
  }

  return jar->cookies;
}

std::vector<ContainerCookie> ContainerCookieManager::GetCookiesForDomain(
    const std::string& container_id,
    const std::string& domain) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return {};
  }

  return jar->GetCookiesForDomain(domain);
}

bool ContainerCookieManager::DeleteCookie(
    const std::string& container_id,
    const std::string& domain,
    const std::string& name,
    const std::string& path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return false;
  }

  auto it = std::find_if(
      jar->cookies.begin(), jar->cookies.end(),
      [&](const ContainerCookie& c) {
        return c.name == name && c.domain == domain && c.path == path;
      });

  if (it == jar->cookies.end()) {
    return false;
  }

  ContainerCookie deleted = *it;
  jar->cookies.erase(it);
  jar->last_modified = base::Time::Now();

  RecordEvent(container_id, CookieEventType::kDeleted, domain, name, 
              "user_request", false, "");
  NotifyCookieDeleted(container_id, deleted);

  return true;
}

size_t ContainerCookieManager::DeleteCookiesForDomain(
    const std::string& container_id,
    const std::string& domain) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return 0;
  }

  size_t initial_count = jar->cookies.size();

  jar->cookies.erase(
      std::remove_if(jar->cookies.begin(), jar->cookies.end(),
                     [&domain, this](const ContainerCookie& c) {
                       return DomainMatches(c.domain, domain);
                     }),
      jar->cookies.end());

  size_t deleted = initial_count - jar->cookies.size();

  if (deleted > 0) {
    jar->last_modified = base::Time::Now();
    RecordEvent(container_id, CookieEventType::kCleared, domain, 
                std::to_string(deleted) + " cookies", "domain_clear", false, "");
  }

  return deleted;
}

size_t ContainerCookieManager::ClearAllCookies(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return 0;
  }

  size_t count = jar->cookies.size();
  jar->cookies.clear();
  jar->last_modified = base::Time::Now();

  RecordEvent(container_id, CookieEventType::kCleared, "*", 
              std::to_string(count) + " cookies", "clear_all", false, "");
  NotifyCookiesCleared(container_id, count);

  ;

  return count;
}

ContainerCookieManager::CookieAllowedResult
ContainerCookieManager::IsCookieAllowed(
    const std::string& container_id,
    const ContainerCookie& cookie,
    const std::string& top_frame_origin) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  CookieAllowedResult result;
  result.allowed = true;
  result.is_third_party = false;
  result.would_be_partitioned = false;

  const ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {

    return result;
  }

  const ContainerCookieConfig& config = jar->config;

  if (config.policy == CookiePolicyType::kBlockAll) {
    result.allowed = false;
    result.reason = "all_cookies_blocked";
    return result;
  }

  if (IsDomainBlocked(container_id, cookie.domain)) {
    result.allowed = false;
    result.reason = "domain_blacklisted";
    return result;
  }

  if (config.policy == CookiePolicyType::kWhitelistOnly) {
    if (!IsDomainAllowed(container_id, cookie.domain)) {
      result.allowed = false;
      result.reason = "domain_not_whitelisted";
      return result;
    }
  }

  std::string request_domain = cookie.domain;
  if (request_domain[0] == '.') {
    request_domain = request_domain.substr(1);
  }

  std::string top_domain = ExtractDomain(top_frame_origin);
  result.is_third_party = IsThirdPartyContext(request_domain, top_domain);

  if (result.is_third_party) {
    if (config.policy == CookiePolicyType::kBlockThirdParty ||
        config.policy == CookiePolicyType::kFirstPartyOnly ||
        config.block_third_party_cookies) {

      if (config.third_party_exceptions.find(cookie.domain) ==
          config.third_party_exceptions.end()) {
        result.allowed = false;
        result.reason = "third_party_blocked";
        return result;
      }
    }
  }

  if (config.require_secure && !cookie.secure) {
    result.allowed = false;
    result.reason = "secure_required";
    return result;
  }

  if (config.require_same_site && cookie.same_site.empty()) {
    result.allowed = false;
    result.reason = "same_site_required";
    return result;
  }

  return result;
}

bool ContainerCookieManager::IsDomainBlocked(
    const std::string& container_id,
    const std::string& domain) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return false;
  }

  return jar->config.blocked_domains.find(domain) != 
         jar->config.blocked_domains.end();
}

bool ContainerCookieManager::IsDomainAllowed(
    const std::string& container_id,
    const std::string& domain) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar || jar->config.allowed_domains.empty()) {
    return true;  
  }

  return jar->config.allowed_domains.find(domain) != 
         jar->config.allowed_domains.end();
}

bool ContainerCookieManager::IsThirdPartyContext(
    const std::string& request_origin,
    const std::string& top_frame_origin) const {
  if (request_origin.empty() || top_frame_origin.empty()) {
    return false;
  }

  std::string request_etld1 = GetEffectiveTLDPlusOne(request_origin);
  std::string top_etld1 = GetEffectiveTLDPlusOne(top_frame_origin);

  return !base::EqualsCaseInsensitiveASCII(request_etld1, top_etld1);
}

bool ContainerCookieManager::UpdateCookieConfig(
    const std::string& container_id,
    const ContainerCookieConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return false;
  }

  jar->config = config;
  jar->config.container_id = container_id;

  NotifyConfigChanged(container_id, config);

  return true;
}

std::optional<ContainerCookieConfig>
ContainerCookieManager::GetCookieConfig(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return std::nullopt;
  }

  return jar->config;
}

void ContainerCookieManager::SetDefaultConfig(
    const ContainerCookieConfig& config) {
  default_config_ = config;
}

bool ContainerCookieManager::AddAllowedDomain(
    const std::string& container_id,
    const std::string& domain) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return false;
  }

  jar->config.allowed_domains.insert(domain);
  return true;
}

bool ContainerCookieManager::AddBlockedDomain(
    const std::string& container_id,
    const std::string& domain) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return false;
  }

  jar->config.blocked_domains.insert(domain);
  return true;
}

bool ContainerCookieManager::RemoveAllowedDomain(
    const std::string& container_id,
    const std::string& domain) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return false;
  }

  return jar->config.allowed_domains.erase(domain) > 0;
}

bool ContainerCookieManager::RemoveBlockedDomain(
    const std::string& container_id,
    const std::string& domain) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return false;
  }

  return jar->config.blocked_domains.erase(domain) > 0;
}

size_t ContainerCookieManager::CleanupExpiredCookies(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return 0;
  }

  size_t initial = jar->cookies.size();

  jar->cookies.erase(
      std::remove_if(jar->cookies.begin(), jar->cookies.end(),
                     [](const ContainerCookie& c) { return c.IsExpired(); }),
      jar->cookies.end());

  size_t expired = initial - jar->cookies.size();
  jar->cookies_expired += expired;

  if (expired > 0) {
    jar->last_modified = base::Time::Now();
    ;
  }

  return expired;
}

size_t ContainerCookieManager::CleanupAllExpiredCookies() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  size_t total = 0;
  for (auto& pair : cookie_jars_) {
    total += CleanupExpiredCookies(pair.first);
  }
  return total;
}

size_t ContainerCookieManager::EnforceCookieLimits(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return 0;
  }

  size_t removed = 0;

  while (jar->cookies.size() > jar->config.max_total_cookies) {
    auto lra_it = std::min_element(
        jar->cookies.begin(), jar->cookies.end(),
        [](const ContainerCookie& a, const ContainerCookie& b) {
          return a.last_access_time < b.last_access_time;
        });
    if (lra_it != jar->cookies.end()) {
      jar->cookies.erase(lra_it);
      removed++;
    }
  }

  return removed;
}

size_t ContainerCookieManager::ClearSessionCookies(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return 0;
  }

  size_t initial = jar->cookies.size();

  jar->cookies.erase(
      std::remove_if(jar->cookies.begin(), jar->cookies.end(),
                     [](const ContainerCookie& c) { return c.IsSession(); }),
      jar->cookies.end());

  return initial - jar->cookies.size();
}

std::string ContainerCookieManager::ExportCookies(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return "[]";
  }

  base::ListValue list;
  for (const auto& cookie : jar->cookies) {
    base::DictValue dict;
    dict.Set("name", cookie.name);
    dict.Set("value", cookie.value);
    dict.Set("domain", cookie.domain);
    dict.Set("path", cookie.path);
    dict.Set("secure", cookie.secure);
    dict.Set("http_only", cookie.http_only);
    dict.Set("same_site", cookie.same_site);
    dict.Set("expiry",
              static_cast<double>(cookie.expiry_time.ToInternalValue()));
    list.Append(std::move(dict));
  }

  std::string output;
  base::JSONWriter::Write(list, &output);
  return output;
}

size_t ContainerCookieManager::ImportCookies(
    const std::string& container_id,
    const std::string& data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto parsed = base::JSONReader::Read(
      data, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!parsed || !parsed->is_list()) {
    return 0;
  }

  size_t imported = 0;
  for (const auto& item : parsed->GetList()) {
    if (!item.is_dict()) {
      continue;
    }

    const base::DictValue& dict = item.GetDict();
    ContainerCookie cookie;

    const std::string* name = dict.FindString("name");
    if (name) cookie.name = *name;

    const std::string* value = dict.FindString("value");
    if (value) cookie.value = *value;

    const std::string* domain = dict.FindString("domain");
    if (domain) cookie.domain = *domain;

    const std::string* path = dict.FindString("path");
    if (path) cookie.path = *path;
    else cookie.path = "/";

    cookie.secure = dict.FindBool("secure").value_or(false);
    cookie.http_only = dict.FindBool("http_only").value_or(false);

    const std::string* same_site = dict.FindString("same_site");
    if (same_site) cookie.same_site = *same_site;

    SetCookieResult result = SetCookie(container_id, cookie, "");
    if (result.success) {
      imported++;
    }
  }

  return imported;
}

size_t ContainerCookieManager::CopyCookies(
    const std::string& source_container_id,
    const std::string& dest_container_id,
    const std::string& domain_filter) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const ContainerCookieJar* source = GetCookieJar(source_container_id);
  if (!source) {
    return 0;
  }

  size_t copied = 0;
  for (const auto& cookie : source->cookies) {
    if (!domain_filter.empty() && !DomainMatches(cookie.domain, domain_filter)) {
      continue;
    }

    SetCookieResult result = SetCookie(dest_container_id, cookie, "");
    if (result.success) {
      copied++;
    }
  }

  return copied;
}

ContainerCookieManager::CookieStatistics
ContainerCookieManager::GetStatistics(const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  CookieStatistics stats;

  const ContainerCookieJar* jar = GetCookieJar(container_id);
  if (!jar) {
    return stats;
  }

  stats.total_cookies = jar->cookies.size();
  stats.cookies_set = jar->cookies_set;
  stats.cookies_blocked = jar->cookies_blocked;
  stats.third_party_blocked = jar->third_party_blocked;

  for (const auto& cookie : jar->cookies) {
    if (cookie.IsSession()) {
      stats.session_cookies++;
    } else {
      stats.persistent_cookies++;
    }

    if (cookie.secure) stats.secure_cookies++;
    if (cookie.http_only) stats.http_only_cookies++;

    if (cookie.same_site == "Strict") stats.same_site_strict++;
    else if (cookie.same_site == "Lax") stats.same_site_lax++;
    else if (cookie.same_site == "None") stats.same_site_none++;

    stats.total_size_bytes += cookie.GetSizeBytes();
    stats.cookies_per_domain[cookie.domain]++;
  }

  return stats;
}

ContainerCookieManager::CookieStatistics
ContainerCookieManager::GetGlobalStatistics() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  CookieStatistics stats;

  for (const auto& pair : cookie_jars_) {
    CookieStatistics jar_stats = GetStatistics(pair.first);
    stats.total_cookies += jar_stats.total_cookies;
    stats.session_cookies += jar_stats.session_cookies;
    stats.persistent_cookies += jar_stats.persistent_cookies;
    stats.secure_cookies += jar_stats.secure_cookies;
    stats.http_only_cookies += jar_stats.http_only_cookies;
    stats.same_site_strict += jar_stats.same_site_strict;
    stats.same_site_lax += jar_stats.same_site_lax;
    stats.same_site_none += jar_stats.same_site_none;
    stats.cookies_set += jar_stats.cookies_set;
    stats.cookies_blocked += jar_stats.cookies_blocked;
    stats.third_party_blocked += jar_stats.third_party_blocked;
    stats.total_size_bytes += jar_stats.total_size_bytes;
  }

  return stats;
}

std::vector<CookieEvent> ContainerCookieManager::GetCookieEvents(
    const std::string& container_id,
    size_t count) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = cookie_events_.find(container_id);
  if (it == cookie_events_.end()) {
    return {};
  }

  const auto& events = it->second;
  if (events.size() <= count) {
    return events;
  }

  return std::vector<CookieEvent>(events.end() - count, events.end());
}

void ContainerCookieManager::SetDebugLoggingEnabled(bool enabled) {
  debug_logging_enabled_ = enabled;
}

std::string ContainerCookieManager::GetDiagnosticReport() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::stringstream report;
  report << "=== ContainerCookieManager Diagnostic Report ===\n\n";

  CookieStatistics global = GetGlobalStatistics();
  report << "Global Statistics:\n";
  report << "  Total Cookies: " << global.total_cookies << "\n";
  report << "  Session Cookies: " << global.session_cookies << "\n";
  report << "  Persistent Cookies: " << global.persistent_cookies << "\n";
  report << "  Secure Cookies: " << global.secure_cookies << "\n";
  report << "  HttpOnly Cookies: " << global.http_only_cookies << "\n";
  report << "  Total Size: " << global.total_size_bytes << " bytes\n";
  report << "  Cookies Set: " << global.cookies_set << "\n";
  report << "  Cookies Blocked: " << global.cookies_blocked << "\n";
  report << "  Third-Party Blocked: " << global.third_party_blocked << "\n\n";

  report << "Cookie Jars: " << cookie_jars_.size() << "\n";
  for (const auto& pair : cookie_jars_) {
    const auto* jar = pair.second.get();
    report << "  - " << pair.first << ":\n";
    report << "      Cookies: " << jar->cookies.size() << "\n";
    report << "      Isolation: " << IsolationModeToString(jar->config.isolation_mode) << "\n";
    report << "      Policy: " << PolicyToString(jar->config.policy) << "\n";
    report << "      Set/Blocked: " << jar->cookies_set << "/" << jar->cookies_blocked << "\n";
  }

  return report.str();
}

void ContainerCookieManager::DumpStateToLog() const {
  ;
}

bool ContainerCookieManager::DomainMatches(
    const std::string& cookie_domain,
    const std::string& request_domain) const {
  if (cookie_domain == request_domain) {
    return true;
  }

  std::string domain = cookie_domain;
  if (!domain.empty() && domain[0] == '.') {
    domain = domain.substr(1);
  }

  if (domain == request_domain) {
    return true;
  }

  if (base::EndsWith(request_domain, "." + domain, 
                     base::CompareCase::INSENSITIVE_ASCII)) {
    return true;
  }

  return false;
}

std::string ContainerCookieManager::ExtractDomain(const std::string& url) const {
  GURL parsed(url);
  if (!parsed.is_valid()) {
    return url;
  }
  const auto host_view = parsed.host();
  return std::string(host_view.data(), host_view.size());
}

std::string ContainerCookieManager::GetEffectiveTLDPlusOne(
    const std::string& domain) const {

  std::vector<std::string> parts = base::SplitString(
      domain, ".", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  if (parts.size() <= 2) {
    return domain;
  }

  return parts[parts.size() - 2] + "." + parts[parts.size() - 1];
}

bool ContainerCookieManager::ValidateCookie(const ContainerCookie& cookie) const {
  if (cookie.name.empty()) {
    return false;
  }
  if (cookie.domain.empty()) {
    return false;
  }
  if (cookie.name.find(';') != std::string::npos ||
      cookie.name.find('=') != std::string::npos) {
    return false;
  }
  return true;
}

void ContainerCookieManager::RecordEvent(
    const std::string& container_id,
    CookieEventType type,
    const std::string& domain,
    const std::string& cookie_name,
    const std::string& reason,
    bool is_third_party,
    const std::string& top_frame_origin) {
  CookieEvent event;
  event.timestamp = base::TimeTicks::Now();
  event.container_id = container_id;
  event.type = type;
  event.domain = domain;
  event.cookie_name = cookie_name;
  event.reason = reason;
  event.is_third_party = is_third_party;
  event.top_frame_origin = top_frame_origin;

  cookie_events_[container_id].push_back(event);

  if (cookie_events_[container_id].size() > 1000) {
    cookie_events_[container_id].erase(
        cookie_events_[container_id].begin(),
        cookie_events_[container_id].begin() + 500);
  }
}

void ContainerCookieManager::NotifyCookieSet(
    const std::string& container_id,
    const ContainerCookie& cookie,
    bool is_third_party) {
  for (auto& observer : observers_) {
    observer.OnCookieSet(container_id, cookie, is_third_party);
  }
}

void ContainerCookieManager::NotifyCookieBlocked(
    const std::string& container_id,
    const std::string& domain,
    const std::string& reason,
    bool is_third_party) {
  for (auto& observer : observers_) {
    observer.OnCookieBlocked(container_id, domain, reason, is_third_party);
  }
}

void ContainerCookieManager::NotifyCookieDeleted(
    const std::string& container_id,
    const ContainerCookie& cookie) {
  for (auto& observer : observers_) {
    observer.OnCookieDeleted(container_id, cookie);
  }
}

void ContainerCookieManager::NotifyCookiesCleared(
    const std::string& container_id,
    size_t count) {
  for (auto& observer : observers_) {
    observer.OnCookiesCleared(container_id, count);
  }
}

void ContainerCookieManager::NotifyConfigChanged(
    const std::string& container_id,
    const ContainerCookieConfig& config) {
  for (auto& observer : observers_) {
    observer.OnCookieConfigChanged(container_id, config);
  }
}

}  
