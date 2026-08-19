
#ifndef CHROME_BROWSER_CONTAINER_COOKIE_PORTABILITY_CODEC_H_
#define CHROME_BROWSER_CONTAINER_COOKIE_PORTABILITY_CODEC_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/values.h"
#include "net/cookies/canonical_cookie.h"

namespace tab_container::cookie_portability_codec {

inline constexpr int kSchemaVersion = 3;

enum class DecodeError {
  kOk,
  kSchemaVersionMismatch,
  kFieldMissing,
  kFieldTypeWrong,
  kCookieFieldRejected,
};

struct DecodeResult {
  DecodeResult();
  DecodeResult(const DecodeResult&);
  DecodeResult(DecodeResult&&);
  DecodeResult& operator=(const DecodeResult&);
  DecodeResult& operator=(DecodeResult&&);
  ~DecodeResult();

  DecodeError error = DecodeError::kOk;
  std::vector<net::CanonicalCookie> cookies;
  std::vector<std::pair<std::string, std::string>> rejected;
};

DecodeResult DecodeSingleCookie(const base::DictValue& dict);
DecodeResult DecodeCookieList(const base::ListValue& list);
DecodeResult DecodeDomainBundle(const base::DictValue& dict,
                                std::string_view target_domain);

const std::string* ReadOptionalHostFingerprint(const base::DictValue& dict);
void WriteOptionalHostFingerprint(base::DictValue* dict,
                                  std::string_view host_fingerprint);

base::DictValue EncodeSingleCookie(const net::CanonicalCookie& cookie);
base::ListValue EncodeCookieList(
    const std::vector<net::CanonicalCookie>& cookies);

}  

#endif  
