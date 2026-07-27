
#ifndef CHROME_BROWSER_CONTAINER_ENCRYPTION_UTIL_H_
#define CHROME_BROWSER_CONTAINER_ENCRYPTION_UTIL_H_

#include <string>
#include <string_view>

namespace tab_container {

std::string EncryptProfilePayload(std::string_view plaintext,
                                  std::string_view encryption_key);

std::string DecryptProfilePayload(std::string_view encrypted_payload,
                                  std::string_view encryption_key);

}  

#endif  
