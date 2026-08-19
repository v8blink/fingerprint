
#ifndef COMPONENTS_FINGERPRINT_SWITCHES_H_
#define COMPONENTS_FINGERPRINT_SWITCHES_H_

#include <string_view>

#include "base/component_export.h"

namespace fingerprint {

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprint[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintPlatform[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintPlatformVersion[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintBrand[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintBrandVersion[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintDeviceModel[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintGpuVendor[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintGpuRenderer[];

COMPONENT_EXPORT(FINGERPRINT)
extern const char kFingerprintHardwareConcurrency[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintTimezone[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintLanguages[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintScreen[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kDisableSpoofing[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintWebrtcPublicIp[];

COMPONENT_EXPORT(FINGERPRINT)
extern const char kFingerprintPrefersColorScheme[];
COMPONENT_EXPORT(FINGERPRINT)
extern const char kFingerprintPrefersReducedMotion[];
COMPONENT_EXPORT(FINGERPRINT)
extern const char kFingerprintPrefersReducedTransparency[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kTanyaSkipPreflightHosts[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintStealth[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintAntiBotBypass[];

COMPONENT_EXPORT(FINGERPRINT)
bool IsTanyaBypassHost(std::string_view host);

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintAudioSampleRate[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintAudioMaxChannels[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintAudioBaseLatency[];

COMPONENT_EXPORT(FINGERPRINT)
extern const char kFingerprintAudioOutputLatency[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintWebglExtensions[];

COMPONENT_EXPORT(FINGERPRINT) extern const char kFingerprintDeviceMemory[];

}  

#endif  
