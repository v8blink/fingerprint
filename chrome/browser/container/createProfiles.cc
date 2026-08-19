
#include "chrome/browser/container/createProfiles.h"

#include <optional>
#include <string>

#include "chrome/browser/container/cookie_portability_codec.h"
#include "chrome/browser/container/encryption_util.h"
#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_WIN)
#include "base/base_paths_win.h"
#endif
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_partition_key.h"

namespace tab_container {

base::FilePath GetTanyaProfilesStorageRoot() {
  base::FilePath path;
#if BUILDFLAG(IS_WIN)
  if (base::PathService::Get(base::DIR_ROAMING_APP_DATA, &path)) {
    return path.AppendASCII("tanyaProfiles");
  }
#elif BUILDFLAG(IS_POSIX)
  if (base::PathService::Get(base::DIR_HOME, &path)) {
    return path.AppendASCII(".tanyaProfiles");
  }
#endif
  return base::FilePath();
}

base::FilePath GetTanyaProfilesBasePath(const base::FilePath& profile_path) {
  base::FilePath root = GetTanyaProfilesStorageRoot();
  return root.empty() ? profile_path : root;
}

PersistedProfile::PersistedProfile() = default;
PersistedProfile::~PersistedProfile() = default;

namespace internal {

std::string SanitizeProfileFileComponent(const std::string& input) {
  std::string value = input;
  base::TrimWhitespaceASCII(value, base::TrimPositions::TRIM_ALL, &value);

  for (char& c : value) {
    if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' ||
        c == '|' || c == '?' || c == '*') {
      c = '_';
    }
  }

  if (value.empty()) {
    return "profile";
  }

  return value;
}

}  

base::FilePath GetEncryptedProfileFilePath(const base::FilePath& base_path,
                                           const std::string& profile_name,
                                           const std::string& profile_id) {
  const base::FilePath profiles_dir = base_path.AppendASCII("profiles");
  return profiles_dir.AppendASCII(
      internal::SanitizeProfileFileComponent(profile_name) + "_" +
      internal::SanitizeProfileFileComponent(profile_id) + ".json");
}

namespace {

std::string TimeMsToString(base::Time t) {
  return base::NumberToString(t.InMillisecondsSinceUnixEpoch());
}

void AppendCanonicalCookieJson(base::ListValue* list,
                               const net::CanonicalCookie& c,
                               const CookieJsonSerializeOptions& options) {
  base::DictValue d;
  d.Set("_schema", cookie_portability_codec::kSchemaVersion);
  d.Set("name", c.Name());
  d.Set("value", c.Value());
  d.Set("domain", c.Domain());
  d.Set("path", c.Path());
  d.Set("creation_ms", TimeMsToString(c.CreationDate()));
  d.Set("expiration_ms", TimeMsToString(c.ExpiryDate()));
  d.Set("last_access_ms", TimeMsToString(c.LastAccessDate()));
  d.Set("last_update_ms", TimeMsToString(c.LastUpdateDate()));
  d.Set("secure", c.SecureAttribute());
  d.Set("httponly", c.IsHttpOnly());
  d.Set("samesite", static_cast<int>(c.SameSite()));
  d.Set("priority", static_cast<int>(c.Priority()));
  d.Set("source_scheme", static_cast<int>(c.SourceScheme()));
  d.Set("source_port", c.SourcePort());
  d.Set("source_type", static_cast<int>(c.SourceType()));
  d.Set("is_partitioned", c.IsPartitioned());
  d.Set("is_domain_cookie", c.IsDomainCookie());
  d.Set("is_persistent", c.IsPersistent());
  if (options.include_partition_key && c.PartitionKey().has_value()) {
    auto ser = net::CookiePartitionKey::Serialize(*c.PartitionKey());
    if (ser.has_value()) {
      d.Set("partition_top_level_site", ser->TopLevelSite());
      d.Set("partition_has_cross_site_ancestor", ser->has_cross_site_ancestor());
      d.Set("partition_key", ser->GetDebugString());
    }
  }
  list->Append(std::move(d));
}

}  

bool CreateEncryptedProfileJson(const base::FilePath& base_path,
                                const std::string& profile_name,
                                const std::string& profile_id,
                                const std::string& cookies,
                                const std::string& proxy,
                                const std::string& fingerprint_id,
                                const std::string& encryption_key,
                                std::string* error_out) {
  if (profile_name.empty() || profile_id.empty()) {
    if (error_out) {
      *error_out = "profile_name and profile_id must not be empty";
    }
    return false;
  }

  if (encryption_key.empty()) {
    if (error_out) {
      *error_out = "encryption_key must not be empty";
    }
    return false;
  }

  const base::FilePath file_path =
      GetEncryptedProfileFilePath(base_path, profile_name, profile_id);

  base::ScopedAllowBlockingForTesting allow_blocking;

  if (!base::CreateDirectory(file_path.DirName())) {
    if (error_out) {
      *error_out = "failed to create profiles directory";
    }
    return false;
  }

  base::DictValue profile_data;
  profile_data.Set("cookies", cookies);
  profile_data.Set("profile_name", profile_name);
  profile_data.Set("proxy", proxy);
  profile_data.Set("fingerprint_id", fingerprint_id);

  profile_data.Set("tags", base::ListValue());

  std::string plain_json;
  if (!base::JSONWriter::Write(profile_data, &plain_json)) {
    if (error_out) {
      *error_out = "failed to serialize profile json";
    }
    return false;
  }

  const std::string encrypted_json =
      EncryptProfilePayload(plain_json, encryption_key);
  if (encrypted_json.empty()) {
    if (error_out) {
      *error_out = "failed to encrypt profile json";
    }
    return false;
  }

  if (!base::WriteFile(file_path, encrypted_json)) {
    if (error_out) {
      *error_out = "failed to write encrypted profile json file";
    }
    return false;
  }

  if (error_out) {
    error_out->clear();
  }
  return true;
}

bool UpdateEncryptedTanyaProfileAtPath(const base::FilePath& encrypted_file,
                                      const std::string& encryption_key,
                                      const std::string& cookies_json,
                                      const std::string& proxy_json,
                                      std::string* error_out) {
  PersistedProfile persisted;
  if (!ReadPersistedProfileFromEncryptedFile(encrypted_file, encryption_key,
                                              &persisted, error_out)) {
    return false;
  }

  base::DictValue profile_data;
  profile_data.Set("cookies", cookies_json);
  profile_data.Set("profile_name", persisted.profile_name);
  profile_data.Set("proxy", proxy_json);
  profile_data.Set("fingerprint_id", persisted.fingerprint_id);

  base::ListValue tag_list;
  for (const std::string& t : persisted.tags) {
    tag_list.Append(t);
  }
  profile_data.Set("tags", std::move(tag_list));

  std::string plain_json;
  if (!base::JSONWriter::Write(profile_data, &plain_json)) {
    if (error_out) {
      *error_out = "failed to serialize updated profile json";
    }
    return false;
  }

  const std::string encrypted_json =
      EncryptProfilePayload(plain_json, encryption_key);
  if (encrypted_json.empty()) {
    if (error_out) {
      *error_out = "failed to encrypt updated profile json";
    }
    return false;
  }

  base::ScopedAllowBlockingForTesting allow_blocking;
  if (!base::WriteFile(encrypted_file, encrypted_json)) {
    if (error_out) {
      *error_out = "failed to write updated encrypted profile file";
    }
    return false;
  }

  if (error_out) {
    error_out->clear();
  }
  return true;
}

bool ParsePersistedProfilePlaintext(const std::string& plain_json,
                                    PersistedProfile* out,
                                    std::string* error_out) {
  auto parsed = base::JSONReader::ReadAndReturnValueWithError(
      plain_json, base::JSON_PARSE_RFC);
  if (!parsed.has_value()) {
    if (error_out) {
      *error_out = parsed.error().ToString();
    }
    return false;
  }
  if (!parsed->is_dict()) {
    if (error_out) {
      *error_out = "profile json is not an object";
    }
    return false;
  }
  const base::DictValue& root = parsed->GetDict();

  const std::string* name = root.FindString("profile_name");
  const std::string* fp = root.FindString("fingerprint_id");
  const std::string* proxy = root.FindString("proxy");
  if (!name || !fp || !proxy) {
    if (error_out) {
      *error_out = "profile json missing required string fields";
    }
    return false;
  }
  out->profile_name = *name;
  out->fingerprint_id = *fp;
  out->proxy_json = *proxy;

  if (const std::string* cj = root.FindString("cookies")) {
    out->cookies_json = cj->empty() ? "[]" : *cj;
  } else if (const base::Value* cv = root.Find("cookies")) {
    if (cv->is_list()) {
      if (!base::JSONWriter::Write(cv->GetList(), &out->cookies_json)) {
        out->cookies_json = "[]";
      }
    } else {
      out->cookies_json = "[]";
    }
  } else {
    out->cookies_json = "[]";
  }

  out->tags.clear();
  if (const base::ListValue* tlist = root.FindList("tags")) {
    for (const base::Value& t : *tlist) {
      if (t.is_string() && !t.GetString().empty()) {
        out->tags.push_back(t.GetString());
      }
    }
  }

  if (error_out) {
    error_out->clear();
  }
  return true;
}

bool UpdateEncryptedTanyaProfileTagsAtPath(
    const base::FilePath& encrypted_file,
    const std::string& encryption_key,
    const std::vector<std::string>& tags,
    std::string* error_out) {
  PersistedProfile persisted;
  if (!ReadPersistedProfileFromEncryptedFile(encrypted_file, encryption_key,
                                              &persisted, error_out)) {
    return false;
  }

  base::DictValue profile_data;
  profile_data.Set("cookies", persisted.cookies_json);
  profile_data.Set("profile_name", persisted.profile_name);
  profile_data.Set("proxy", persisted.proxy_json);
  profile_data.Set("fingerprint_id", persisted.fingerprint_id);
  base::ListValue tag_list;
  for (const std::string& t : tags) {
    if (!t.empty()) {
      tag_list.Append(t);
    }
  }
  profile_data.Set("tags", std::move(tag_list));

  std::string plain_json;
  if (!base::JSONWriter::Write(profile_data, &plain_json)) {
    if (error_out) {
      *error_out = "failed to serialize updated profile json";
    }
    return false;
  }

  const std::string encrypted_json =
      EncryptProfilePayload(plain_json, encryption_key);
  if (encrypted_json.empty()) {
    if (error_out) {
      *error_out = "failed to encrypt updated profile json";
    }
    return false;
  }

  base::ScopedAllowBlockingForTesting allow_blocking;
  if (!base::WriteFile(encrypted_file, encrypted_json)) {
    if (error_out) {
      *error_out = "failed to write updated encrypted profile file";
    }
    return false;
  }

  if (error_out) {
    error_out->clear();
  }
  return true;
}

bool ReadPersistedProfileFromEncryptedFile(
    const base::FilePath& encrypted_file,
    const std::string& encryption_key,
    PersistedProfile* out,
    std::string* error_out) {
  if (!out) {
    return false;
  }
  if (encryption_key.empty()) {
    if (error_out) {
      *error_out = "encryption_key must not be empty";
    }
    return false;
  }

  std::string file_contents;
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    if (!base::ReadFileToString(encrypted_file, &file_contents)) {
      if (error_out) {
        *error_out = "failed to read encrypted profile file";
      }
      return false;
    }
  }

  const std::string plain =
      DecryptProfilePayload(file_contents, encryption_key);
  if (plain.empty()) {
    if (error_out) {
      *error_out = "failed to decrypt profile (wrong key or corrupt file)";
    }
    return false;
  }

  return ParsePersistedProfilePlaintext(plain, out, error_out);
}

std::string SerializeCookieListToJson(
    const net::CookieList& cookies,
    const CookieJsonSerializeOptions& options) {
  base::ListValue list;
  for (const net::CanonicalCookie& c : cookies) {
    AppendCanonicalCookieJson(&list, c, options);
  }
  std::string out;
  if (!base::JSONWriter::Write(list, &out)) {
    return "[]";
  }
  return out;
}

net::CookieList DeserializeCookieListFromJson(const std::string& cookies_json) {
  net::CookieList result;
  if (cookies_json.empty()) {
    return result;
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(cookies_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return result;
  }

  cookie_portability_codec::DecodeResult decoded =
      cookie_portability_codec::DecodeCookieList(parsed->GetList());
  return decoded.cookies;
}

}  
