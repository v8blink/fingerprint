
#ifndef CHROME_BROWSER_CONTAINER_PROFILE_EXPORTER_H_
#define CHROME_BROWSER_CONTAINER_PROFILE_EXPORTER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "chrome/browser/container/cookie_portability_codec.h"
#include "net/cookies/canonical_cookie.h"

namespace tab_container::profile_exporter {

inline constexpr int kEnvelopeSchemaVersion =
    cookie_portability_codec::kSchemaVersion;

enum class HostFingerprintMatch {
  kSameHost,
  kDifferentHost,
  kUnknown,
  kMaxValue = kUnknown,
};

enum class EnvelopeError {
  kOk,
  kFileIoFailed,
  kDecryptFailed,
  kJsonParseFailed,
  kSchemaVersionMismatch,
  kFieldMissing,
  kFieldTypeWrong,
};

struct ProfileEnvelopeMeta {
  ProfileEnvelopeMeta();
  ProfileEnvelopeMeta(const ProfileEnvelopeMeta&);
  ProfileEnvelopeMeta(ProfileEnvelopeMeta&&);
  ProfileEnvelopeMeta& operator=(const ProfileEnvelopeMeta&);
  ProfileEnvelopeMeta& operator=(ProfileEnvelopeMeta&&);
  ~ProfileEnvelopeMeta();

  int schema_version = kEnvelopeSchemaVersion;
  base::Time created_at;
  std::string host_fingerprint;
};

struct ProfileEnvelope {
  struct TabRecord {
    TabRecord();
    TabRecord(const TabRecord&);
    TabRecord(TabRecord&&);
    TabRecord& operator=(const TabRecord&);
    TabRecord& operator=(TabRecord&&);
    ~TabRecord();

    std::string original_tab_label;

    std::string primary_url;
    std::vector<net::CanonicalCookie> cookies;
  };

  ProfileEnvelope();
  ProfileEnvelope(const ProfileEnvelope&);
  ProfileEnvelope(ProfileEnvelope&&);
  ProfileEnvelope& operator=(const ProfileEnvelope&);
  ProfileEnvelope& operator=(ProfileEnvelope&&);
  ~ProfileEnvelope();

  ProfileEnvelopeMeta meta;
  std::vector<TabRecord> tabs;
};

struct DecodedSummary {
  DecodedSummary();
  DecodedSummary(const DecodedSummary&);
  DecodedSummary(DecodedSummary&&);
  DecodedSummary& operator=(const DecodedSummary&);
  DecodedSummary& operator=(DecodedSummary&&);
  ~DecodedSummary();

  uint32_t tab_count = 0;
  uint32_t cookie_total = 0;
  std::vector<std::string> affected_domains;
};

base::expected<std::vector<uint8_t>, EnvelopeError> EncryptEnvelope(
    const ProfileEnvelope& envelope);

base::expected<ProfileEnvelope, EnvelopeError> DecryptEnvelope(
    base::span<const uint8_t> blob);

base::expected<void, EnvelopeError> ValidateSchemaVersion(
    const ProfileEnvelopeMeta& meta);

HostFingerprintMatch CompareHostFingerprint(const ProfileEnvelopeMeta& meta,
                                            std::string_view local_fingerprint);

std::string ComputeLocalHostFingerprint();

DecodedSummary SummarizeEnvelope(const ProfileEnvelope& envelope);

}  

#endif  
