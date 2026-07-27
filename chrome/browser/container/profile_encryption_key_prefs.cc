
#include "chrome/browser/container/profile_encryption_key_prefs.h"

#include <string>

#include "components/pref_registry/pref_registry_syncable.h"

namespace tab_container {

void RegisterProfileEncryptionKeyPrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kProfileEncryptionKeyPref, std::string());
}

}  
