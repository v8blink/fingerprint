
#include "chrome/browser/container/profile_encryption_key_provider.h"

#include <string>

#include "base/base64.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "chrome/browser/container/profile_encryption_key_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"

namespace tab_container {

namespace {

}  

std::string GetOrCreateProfileEncryptionKey(Profile* profile) {

  ;

  if (!profile) {
    ;
    return std::string();
  }

  PrefService* prefs = profile->GetPrefs();
  if (!prefs) {
    ;
    return std::string();
  }

  const std::string stored = prefs->GetString(kProfileEncryptionKeyPref);

  ;

  if (!stored.empty()) {
    std::string decoded;
    if (base::Base64Decode(stored, &decoded) && !decoded.empty()) {

      ;
      return decoded;
    }
    ;
    prefs->ClearPref(kProfileEncryptionKeyPref);
  }

  ;
  const std::string raw = base::RandBytesAsString(32);
  const std::string encoded = base::Base64Encode(raw);
  prefs->SetString(kProfileEncryptionKeyPref, encoded);

  ;

  return raw;
}

}  
