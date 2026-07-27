
#ifndef CHROME_BROWSER_CONTAINER_PROFILE_ENCRYPTION_KEY_PREFS_H_
#define CHROME_BROWSER_CONTAINER_PROFILE_ENCRYPTION_KEY_PREFS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}  

namespace tab_container {

inline constexpr char kProfileEncryptionKeyPref[] =
    "tab_container.profile_encryption_key_base64";

void RegisterProfileEncryptionKeyPrefs(user_prefs::PrefRegistrySyncable* registry);

}  

#endif  
