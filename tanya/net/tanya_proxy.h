#ifndef TANYA_PROXY_H_
#define TANYA_PROXY_H_

#include <string>

namespace net {

struct TanyaProxy {
  std::string pac_config;
  std::string username;
  std::string password;
  std::string proxy_name;

  TanyaProxy() = default;
  TanyaProxy(const TanyaProxy&) = default;
  TanyaProxy& operator=(const TanyaProxy&) = default;
  ~TanyaProxy() = default;

  TanyaProxy(const std::string& in_pac_config,
             const std::string& in_username,
             const std::string& in_password,
             const std::string& in_proxy_name)
      : pac_config(in_pac_config),
        username(in_username),
        password(in_password),
        proxy_name(in_proxy_name) {}

  bool operator==(const TanyaProxy& other) const {
    return pac_config == other.pac_config && username == other.username &&
           password == other.password && proxy_name == other.proxy_name;
  }
};

}  // namespace net

#endif
