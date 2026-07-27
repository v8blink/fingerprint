
#ifndef CHROME_BROWSER_CONTAINER_CREATEPROFILES_H_
#define CHROME_BROWSER_CONTAINER_CREATEPROFILES_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "net/cookies/canonical_cookie.h"

namespace tab_container {

base::FilePath GetTanyaProfilesStorageRoot();

base::FilePath GetTanyaProfilesBasePath(const base::FilePath& profile_path);

struct PersistedProfile {
  PersistedProfile();
  ~PersistedProfile();

  std::string profile_name;
  std::string fingerprint_id;
  std::string proxy_json;
  std::string cookies_json;

  std::vector<std::string> tags;
};

base::FilePath GetEncryptedProfileFilePath(const base::FilePath& base_path,
                                           const std::string& profile_name,
                                           const std::string& profile_id);

bool CreateEncryptedProfileJson(const base::FilePath& base_path,
                                const std::string& profile_name,
                                const std::string& profile_id,
                                const std::string& cookies,
                                const std::string& proxy,
                                const std::string& fingerprint_id,
                                const std::string& encryption_key,
                                std::string* error_out);

bool UpdateEncryptedTanyaProfileAtPath(const base::FilePath& encrypted_file,
                                      const std::string& encryption_key,
                                      const std::string& cookies_json,
                                      const std::string& proxy_json,
                                      std::string* error_out);

bool UpdateEncryptedTanyaProfileTagsAtPath(
    const base::FilePath& encrypted_file,
    const std::string& encryption_key,
    const std::vector<std::string>& tags,
    std::string* error_out);

bool ReadPersistedProfileFromEncryptedFile(
    const base::FilePath& encrypted_file,
    const std::string& encryption_key,
    PersistedProfile* out,
                                std::string* error_out);

bool ParsePersistedProfilePlaintext(const std::string& plain_json,
                                    PersistedProfile* out,
                                    std::string* error_out);

struct CookieJsonSerializeOptions {
  bool include_partition_key = true;
};

std::string SerializeCookieListToJson(
    const net::CookieList& cookies,
    const CookieJsonSerializeOptions& options = {});
net::CookieList DeserializeCookieListFromJson(const std::string& cookies_json);

}  

#endif  
