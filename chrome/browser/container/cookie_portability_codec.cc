
#include "chrome/browser/container/cookie_portability_codec.h"

#include <memory>
#include <optional>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_partition_key.h"

namespace tab_container::cookie_portability_codec {

DecodeResult::DecodeResult() = default;
DecodeResult::DecodeResult(const DecodeResult&) = default;
DecodeResult::DecodeResult(DecodeResult&&) = default;
DecodeResult& DecodeResult::operator=(const DecodeResult&) = default;
DecodeResult& DecodeResult::operator=(DecodeResult&&) = default;
DecodeResult::~DecodeResult() = default;

namespace {

bool GetString(const base::DictValue& dict,
               std::string_view key,
               const std::string** out) {
  *out = dict.FindString(key);
  return *out != nullptr;
}

bool GetInt64(const base::DictValue& dict, std::string_view key, int64_t* out) {
  if (const std::string* value = dict.FindString(key)) {
    return base::StringToInt64(*value, out);
  }
  if (std::optional<int> value = dict.FindInt(key)) {
    *out = *value;
    return true;
  }
  return false;
}

DecodeResult MakeFieldMissing(std::string field) {
  DecodeResult result;
  result.error = DecodeError::kFieldMissing;
  result.rejected.emplace_back(std::move(field), "required field missing");
  return result;
}

DecodeResult MakeTypeWrong(std::string field) {
  DecodeResult result;
  result.error = DecodeError::kFieldTypeWrong;
  result.rejected.emplace_back(std::move(field), "field type is invalid");
  return result;
}

DecodeResult MakeRejected(std::string name, std::string reason) {
  DecodeResult result;
  result.error = DecodeError::kCookieFieldRejected;
  result.rejected.emplace_back(std::move(name), std::move(reason));
  return result;
}

base::Time TimeFromMs(int64_t value) {
  return base::Time::FromMillisecondsSinceUnixEpoch(value);
}

std::optional<net::CookiePartitionKey> PartitionKeyFromDict(
    const base::DictValue& dict) {
  const std::string* top_level_site = dict.FindString("partition_top_level_site");
  std::optional<bool> cross_site = dict.FindBool("partition_has_cross_site_ancestor");
  if (!top_level_site || !cross_site.has_value()) {
    return std::nullopt;
  }
  auto partition = net::CookiePartitionKey::FromStorage(*top_level_site, *cross_site);
  if (!partition.has_value()) {
    return std::nullopt;
  }
  return std::move(partition.value());
}

}  

DecodeResult DecodeSingleCookie(const base::DictValue& dict) {
  std::optional<int> schema = dict.FindInt("_schema");
  if (!schema.has_value() || *schema != kSchemaVersion) {
    DecodeResult result;
    result.error = DecodeError::kSchemaVersionMismatch;
    result.rejected.emplace_back("_schema", "schema version mismatch");
    return result;
  }

  const std::string* name = nullptr;
  const std::string* value = nullptr;
  const std::string* domain = nullptr;
  const std::string* path = nullptr;
  if (!GetString(dict, "name", &name)) {
    return MakeFieldMissing("name");
  }
  if (!GetString(dict, "value", &value)) {
    return MakeFieldMissing("value");
  }
  if (!GetString(dict, "domain", &domain)) {
    return MakeFieldMissing("domain");
  }
  if (!GetString(dict, "path", &path)) {
    return MakeFieldMissing("path");
  }
  if (name->find('\r') != std::string::npos || name->find('\n') != std::string::npos) {
    return MakeRejected(*name, "name contains CRLF");
  }

  int64_t creation_ms = 0;
  int64_t expiration_ms = 0;
  int64_t last_access_ms = 0;
  int64_t last_update_ms = 0;
  if (!GetInt64(dict, "creation_ms", &creation_ms) ||
      !GetInt64(dict, "expiration_ms", &expiration_ms) ||
      !GetInt64(dict, "last_access_ms", &last_access_ms)) {
    return MakeTypeWrong("time_fields");
  }
  if (!GetInt64(dict, "last_update_ms", &last_update_ms)) {
    last_update_ms = last_access_ms;
  }

  std::optional<bool> secure = dict.FindBool("secure");
  std::optional<bool> http_only = dict.FindBool("httponly");
  std::optional<int> same_site = dict.FindInt("samesite");
  std::optional<int> priority = dict.FindInt("priority");
  std::optional<int> source_scheme = dict.FindInt("source_scheme");
  std::optional<int> source_type = dict.FindInt("source_type");
  if (!secure.has_value() || !http_only.has_value()) {
    return MakeFieldMissing("secure_or_httponly");
  }
  if (!same_site.has_value() || !priority.has_value() ||
      !source_scheme.has_value() || !source_type.has_value()) {
    return MakeFieldMissing("enum_fields");
  }
  if (*same_site < static_cast<int>(net::CookieSameSite::UNSPECIFIED) ||
      *same_site > static_cast<int>(net::CookieSameSite::STRICT_MODE)) {
    return MakeTypeWrong("samesite");
  }

  int source_port = 443;
  if (std::optional<int> port = dict.FindInt("source_port")) {
    source_port = *port;
  }

  std::optional<net::CookiePartitionKey> partition = PartitionKeyFromDict(dict);
  std::unique_ptr<net::CanonicalCookie> cookie =
      net::CanonicalCookie::CreateUnsafeCookieForTesting(
          *name, *value, *domain, *path, TimeFromMs(creation_ms),
          TimeFromMs(expiration_ms), TimeFromMs(last_access_ms),
          TimeFromMs(last_update_ms), *secure, *http_only,
          static_cast<net::CookieSameSite>(*same_site),
          static_cast<net::CookiePriority>(*priority),
          static_cast<net::CookieSourceType>(*source_type), partition,
          static_cast<net::CookieSourceScheme>(*source_scheme), source_port);
  if (!cookie) {
    return MakeRejected(*name, "cookie failed canonical validation");
  }

  DecodeResult result;
  result.cookies.push_back(std::move(*cookie));
  return result;
}

DecodeResult DecodeCookieList(const base::ListValue& list) {
  DecodeResult aggregate;
  for (const base::Value& item : list) {
    if (!item.is_dict()) {
      if (aggregate.error == DecodeError::kOk) {
        aggregate.error = DecodeError::kFieldTypeWrong;
      }
      aggregate.rejected.emplace_back("<list>", "entry is not a dict");
      continue;
    }
    DecodeResult decoded = DecodeSingleCookie(item.GetDict());
    if (decoded.error != DecodeError::kOk && aggregate.error == DecodeError::kOk) {
      aggregate.error = decoded.error;
    }
    for (auto& cookie : decoded.cookies) {
      aggregate.cookies.push_back(std::move(cookie));
    }
    for (auto& rejected : decoded.rejected) {
      aggregate.rejected.push_back(std::move(rejected));
    }
  }
  return aggregate;
}

DecodeResult DecodeDomainBundle(const base::DictValue& dict,
                                std::string_view target_domain) {
  const base::ListValue* list = dict.FindList("cookies");
  if (!list) {
    return MakeFieldMissing("cookies");
  }
  DecodeResult decoded = DecodeCookieList(*list);
  std::vector<net::CanonicalCookie> filtered;
  for (const net::CanonicalCookie& cookie : decoded.cookies) {
    if (cookie.Domain() == target_domain) {
      filtered.push_back(cookie);
    }
  }
  decoded.cookies = std::move(filtered);
  return decoded;
}

const std::string* ReadOptionalHostFingerprint(const base::DictValue& dict) {
  return dict.FindString("host_fingerprint");
}

void WriteOptionalHostFingerprint(base::DictValue* dict,
                                  std::string_view host_fingerprint) {
  if (!dict || host_fingerprint.empty()) {
    return;
  }
  dict->Set("host_fingerprint", std::string(host_fingerprint));
}

base::DictValue EncodeSingleCookie(const net::CanonicalCookie& cookie) {
  base::DictValue dict;
  dict.Set("_schema", kSchemaVersion);
  dict.Set("name", cookie.Name());
  dict.Set("value", cookie.Value());
  dict.Set("domain", cookie.Domain());
  dict.Set("path", cookie.Path());
  dict.Set("creation_ms",
           base::NumberToString(cookie.CreationDate().InMillisecondsSinceUnixEpoch()));
  dict.Set("expiration_ms",
           base::NumberToString(cookie.ExpiryDate().InMillisecondsSinceUnixEpoch()));
  dict.Set("last_access_ms",
           base::NumberToString(cookie.LastAccessDate().InMillisecondsSinceUnixEpoch()));
  dict.Set("last_update_ms",
           base::NumberToString(cookie.LastUpdateDate().InMillisecondsSinceUnixEpoch()));
  dict.Set("secure", cookie.SecureAttribute());
  dict.Set("httponly", cookie.IsHttpOnly());
  dict.Set("samesite", static_cast<int>(cookie.SameSite()));
  dict.Set("priority", static_cast<int>(cookie.Priority()));
  dict.Set("source_scheme", static_cast<int>(cookie.SourceScheme()));
  dict.Set("source_port", cookie.SourcePort());
  dict.Set("source_type", static_cast<int>(cookie.SourceType()));
  if (cookie.PartitionKey().has_value()) {
    auto serialized = net::CookiePartitionKey::Serialize(*cookie.PartitionKey());
    if (serialized.has_value()) {
      dict.Set("partition_top_level_site", serialized->TopLevelSite());
      dict.Set("partition_has_cross_site_ancestor",
               serialized->has_cross_site_ancestor());
    }
  }
  return dict;
}

base::ListValue EncodeCookieList(const std::vector<net::CanonicalCookie>& cookies) {
  base::ListValue list;
  for (const net::CanonicalCookie& cookie : cookies) {
    list.Append(EncodeSingleCookie(cookie));
  }
  return list;
}

}  
