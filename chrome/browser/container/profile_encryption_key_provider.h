
#ifndef CHROME_BROWSER_CONTAINER_PROFILE_ENCRYPTION_KEY_PROVIDER_H_
#define CHROME_BROWSER_CONTAINER_PROFILE_ENCRYPTION_KEY_PROVIDER_H_

#include <string>

class Profile;

namespace tab_container {

std::string GetOrCreateProfileEncryptionKey(Profile* profile);

}  

#endif  
