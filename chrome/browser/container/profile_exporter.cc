
#include "chrome/browser/container/profile_exporter.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/environment.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "build/build_config.h"
#include "crypto/sha2.h"
#if BUILDFLAG(IS_WIN)
#include "base/strings/utf_string_conversions.h"
#include "base/win/registry.h"
#endif

namespace tab_container::profile_exporter {

namespace {

base::DictValue EncodeEnvelopeToJson(const ProfileEnvelope& envelope) {
  base::DictValue root;
  root.Set("_schema", envelope.meta.schema_version);

  base::DictValue meta;

  meta.Set(
      "created_at",
      base::NumberToString(
          envelope.meta.created_at.ToDeltaSinceWindowsEpoch().InMicroseconds()));
  if (!envelope.meta.host_fingerprint.empty()) {
    cookie_portability_codec::WriteOptionalHostFingerprint(
        &meta, envelope.meta.host_fingerprint);
  }
  root.Set("_envelope", std::move(meta));

  base::ListValue tabs;
  for (const auto& tab : envelope.tabs) {
    base::DictValue tab_dict;
    tab_dict.Set("original_tab_label", tab.original_tab_label);
    tab_dict.Set("primary_url", tab.primary_url);
    tab_dict.Set("cookies",
                 cookie_portability_codec::EncodeCookieList(tab.cookies));
    tabs.Append(std::move(tab_dict));
  }
  root.Set("tabs", std::move(tabs));
  return root;
}

base::expected<ProfileEnvelope, EnvelopeError> DecodeEnvelopeFromJson(
    const base::DictValue& root) {
  ProfileEnvelope envelope;

  std::optional<int> schema = root.FindInt("_schema");
  if (!schema.has_value()) {
    return base::unexpected(EnvelopeError::kFieldMissing);
  }
  envelope.meta.schema_version = *schema;

  const base::DictValue* meta = root.FindDict("_envelope");
  if (!meta) {
    return base::unexpected(EnvelopeError::kFieldMissing);
  }
  if (const std::string* host =
          cookie_portability_codec::ReadOptionalHostFingerprint(*meta)) {
    envelope.meta.host_fingerprint = *host;
  }
  if (std::optional<int> created = meta->FindInt("created_at")) {
    envelope.meta.created_at =
        base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(*created));
  } else if (std::optional<double> created_double =
                 meta->FindDouble("created_at")) {

    envelope.meta.created_at = base::Time::FromDeltaSinceWindowsEpoch(
        base::Microseconds(static_cast<int64_t>(*created_double)));
  } else if (const std::string* created_string = meta->FindString("created_at")) {
    int64_t micros = 0;
    if (!base::StringToInt64(*created_string, &micros)) {
      return base::unexpected(EnvelopeError::kFieldTypeWrong);
    }
    envelope.meta.created_at =
        base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(micros));
  } else {
    return base::unexpected(EnvelopeError::kFieldMissing);
  }

  const base::ListValue* tabs = root.FindList("tabs");
  if (!tabs) {
    return base::unexpected(EnvelopeError::kFieldMissing);
  }

  for (const base::Value& entry : *tabs) {
    if (!entry.is_dict()) {
      return base::unexpected(EnvelopeError::kFieldTypeWrong);
    }
    const base::DictValue& tab_dict = entry.GetDict();
    const std::string* label = tab_dict.FindString("original_tab_label");
    if (!label) {
      return base::unexpected(EnvelopeError::kFieldMissing);
    }
    const base::ListValue* cookies = tab_dict.FindList("cookies");
    if (!cookies) {
      return base::unexpected(EnvelopeError::kFieldMissing);
    }

    cookie_portability_codec::DecodeResult decoded =
        cookie_portability_codec::DecodeCookieList(*cookies);
    if (decoded.error != cookie_portability_codec::DecodeError::kOk &&
        decoded.cookies.empty()) {
      return base::unexpected(EnvelopeError::kFieldTypeWrong);
    }

    ProfileEnvelope::TabRecord record;
    record.original_tab_label = *label;

    if (const std::string* url = tab_dict.FindString("primary_url")) {
      record.primary_url = *url;
    }
    record.cookies = std::move(decoded.cookies);
    envelope.tabs.push_back(std::move(record));
  }

  return envelope;
}

std::string ReadMachineGuid() {
#if BUILDFLAG(IS_WIN)
  base::win::RegKey key(HKEY_LOCAL_MACHINE,
                        L"SOFTWARE\\Microsoft\\Cryptography", KEY_READ);
  std::wstring machine_guid;
  if (key.Valid() &&
      key.ReadValue(L"MachineGuid", &machine_guid) == ERROR_SUCCESS) {
    return base::WideToUTF8(machine_guid);
  }
#endif
  return std::string();
}

}  

ProfileEnvelopeMeta::ProfileEnvelopeMeta() = default;
ProfileEnvelopeMeta::ProfileEnvelopeMeta(const ProfileEnvelopeMeta&) = default;
ProfileEnvelopeMeta::ProfileEnvelopeMeta(ProfileEnvelopeMeta&&) = default;
ProfileEnvelopeMeta& ProfileEnvelopeMeta::operator=(const ProfileEnvelopeMeta&) =
    default;
ProfileEnvelopeMeta& ProfileEnvelopeMeta::operator=(ProfileEnvelopeMeta&&) =
    default;
ProfileEnvelopeMeta::~ProfileEnvelopeMeta() = default;

ProfileEnvelope::TabRecord::TabRecord() = default;
ProfileEnvelope::TabRecord::TabRecord(const TabRecord&) = default;
ProfileEnvelope::TabRecord::TabRecord(TabRecord&&) = default;
ProfileEnvelope::TabRecord& ProfileEnvelope::TabRecord::operator=(
    const TabRecord&) = default;
ProfileEnvelope::TabRecord& ProfileEnvelope::TabRecord::operator=(
    TabRecord&&) = default;
ProfileEnvelope::TabRecord::~TabRecord() = default;

ProfileEnvelope::ProfileEnvelope() = default;
ProfileEnvelope::ProfileEnvelope(const ProfileEnvelope&) = default;
ProfileEnvelope::ProfileEnvelope(ProfileEnvelope&&) = default;
ProfileEnvelope& ProfileEnvelope::operator=(const ProfileEnvelope&) = default;
ProfileEnvelope& ProfileEnvelope::operator=(ProfileEnvelope&&) = default;
ProfileEnvelope::~ProfileEnvelope() = default;

DecodedSummary::DecodedSummary() = default;
DecodedSummary::DecodedSummary(const DecodedSummary&) = default;
DecodedSummary::DecodedSummary(DecodedSummary&&) = default;
DecodedSummary& DecodedSummary::operator=(const DecodedSummary&) = default;
DecodedSummary& DecodedSummary::operator=(DecodedSummary&&) = default;
DecodedSummary::~DecodedSummary() = default;

base::expected<std::vector<uint8_t>, EnvelopeError> EncryptEnvelope(
    const ProfileEnvelope& envelope) {
  static_assert(kEnvelopeSchemaVersion ==
                    cookie_portability_codec::kSchemaVersion,
                "Envelope and cookie codec schema versions must match");

  std::string json;
  if (!base::JSONWriter::WriteWithOptions(
          EncodeEnvelopeToJson(envelope),
          base::JSONWriter::OPTIONS_PRETTY_PRINT, &json)) {
    return base::unexpected(EnvelopeError::kJsonParseFailed);
  }
  return std::vector<uint8_t>(json.begin(), json.end());
}

base::expected<ProfileEnvelope, EnvelopeError> DecryptEnvelope(
    base::span<const uint8_t> blob) {
  std::string json(reinterpret_cast<const char*>(blob.data()), blob.size());
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return base::unexpected(EnvelopeError::kJsonParseFailed);
  }
  return DecodeEnvelopeFromJson(parsed->GetDict());
}

base::expected<void, EnvelopeError> ValidateSchemaVersion(
    const ProfileEnvelopeMeta& meta) {
  if (meta.schema_version != kEnvelopeSchemaVersion) {
    return base::unexpected(EnvelopeError::kSchemaVersionMismatch);
  }
  return {};
}

HostFingerprintMatch CompareHostFingerprint(const ProfileEnvelopeMeta& meta,
                                            std::string_view local_fingerprint) {
  if (meta.host_fingerprint.empty()) {
    return HostFingerprintMatch::kUnknown;
  }
  if (meta.host_fingerprint == local_fingerprint) {
    return HostFingerprintMatch::kSameHost;
  }
  return HostFingerprintMatch::kDifferentHost;
}

std::string ComputeLocalHostFingerprint() {
  std::string username;
  if (auto environment = base::Environment::Create()) {
    if (auto value = environment->GetVar("USERNAME")) {
      username = std::move(*value);
    }
    if (username.empty()) {
      if (auto value = environment->GetVar("USER")) {
        username = std::move(*value);
      }
    }
  }
  const std::string machine_guid = ReadMachineGuid();
  const std::string material = username + '\0' + machine_guid;
  return base::Base64Encode(crypto::SHA256HashString(material));
}

DecodedSummary SummarizeEnvelope(const ProfileEnvelope& envelope) {
  DecodedSummary summary;
  summary.tab_count = static_cast<uint32_t>(envelope.tabs.size());
  std::vector<std::string> domains;
  for (const auto& tab : envelope.tabs) {
    for (const auto& cookie : tab.cookies) {
      ++summary.cookie_total;
      if (!cookie.Domain().empty()) {
        domains.push_back(cookie.Domain());
      }
    }
  }
  std::sort(domains.begin(), domains.end());
  domains.erase(std::unique(domains.begin(), domains.end()), domains.end());
  summary.affected_domains = std::move(domains);
  return summary;
}

}  
