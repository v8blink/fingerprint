
#include "chrome/browser/container/encryption_util.h"

#include <string>
#include <string_view>

#include "base/base64.h"
#include "base/check.h"
#include "base/containers/span.h"
#include "base/rand_util.h"
#include "crypto/aead.h"

namespace tab_container {

namespace {

constexpr char kTanyaProfileV2Magic[] = "TANYA2";
constexpr size_t kTanyaProfileV2MagicLen = sizeof(kTanyaProfileV2Magic) - 1;

std::string XorWithKey(std::string_view input, std::string_view key) {
  CHECK(!key.empty());

  std::string output(input.size(), '\0');
  for (size_t i = 0; i < input.size(); ++i) {
    output[i] = static_cast<char>(input[i] ^ key[i % key.size()]);
  }
  return output;
}

bool LooksLikeV2Payload(std::string_view decoded) {
  return decoded.size() >= kTanyaProfileV2MagicLen &&
         decoded.substr(0, kTanyaProfileV2MagicLen) == kTanyaProfileV2Magic;
}

std::string EncryptWithAes256Gcm(std::string_view plaintext,
                                 std::string_view encryption_key) {
  crypto::Aead aead(crypto::Aead::AES_256_GCM);
  if (encryption_key.size() != aead.KeyLength()) {
    return std::string();
  }
  aead.Init(base::as_bytes(base::span(encryption_key)));

  const size_t nonce_len = aead.NonceLength();
  const std::string nonce = base::RandBytesAsString(nonce_len);

  std::string ciphertext;
  if (!aead.Seal(plaintext, nonce, "", &ciphertext)) {
    return std::string();
  }

  std::string wire;
  wire.reserve(kTanyaProfileV2MagicLen + nonce_len + ciphertext.size());
  wire.append(kTanyaProfileV2Magic);
  wire.append(nonce);
  wire.append(ciphertext);
  return base::Base64Encode(wire);
}

std::string DecryptWithAes256Gcm(std::string_view decoded,
                                 std::string_view encryption_key) {
  crypto::Aead aead(crypto::Aead::AES_256_GCM);
  if (encryption_key.size() != aead.KeyLength()) {
    return std::string();
  }
  if (!LooksLikeV2Payload(decoded)) {
    return std::string();
  }

  const size_t nonce_len = aead.NonceLength();
  if (decoded.size() < kTanyaProfileV2MagicLen + nonce_len) {
    return std::string();
  }

  aead.Init(base::as_bytes(base::span(encryption_key)));

  const std::string_view nonce =
      decoded.substr(kTanyaProfileV2MagicLen, nonce_len);
  const std::string_view ciphertext =
      decoded.substr(kTanyaProfileV2MagicLen + nonce_len);

  std::string plaintext;
  if (!aead.Open(ciphertext, nonce, "", &plaintext)) {
    return std::string();
  }
  return plaintext;
}

}  

std::string EncryptProfilePayload(std::string_view plaintext,
                                  std::string_view encryption_key) {
  if (encryption_key.empty()) {
    return std::string();
  }

  std::string v2 = EncryptWithAes256Gcm(plaintext, encryption_key);
  if (!v2.empty()) {
    return v2;
  }

  std::string encrypted_bytes = XorWithKey(plaintext, encryption_key);
  return base::Base64Encode(encrypted_bytes);
}

std::string DecryptProfilePayload(std::string_view encrypted_payload,
                                  std::string_view encryption_key) {
  if (encryption_key.empty()) {
    return std::string();
  }

  std::string decoded;
  if (!base::Base64Decode(encrypted_payload, &decoded)) {
    return std::string();
  }

  std::string aes = DecryptWithAes256Gcm(decoded, encryption_key);
  if (!aes.empty()) {
    return aes;
  }

  if (LooksLikeV2Payload(decoded)) {
    return std::string();
  }

  return XorWithKey(decoded, encryption_key);
}

}  
