
#include "components/fingerprint/switches.h"

#include <string>

#include "base/command_line.h"
#include "base/strings/string_split.h"

namespace fingerprint {

const char kFingerprint[] = "fingerprint";
const char kFingerprintPlatform[] = "fingerprint-platform";
const char kFingerprintPlatformVersion[] = "fingerprint-platform-version";
const char kFingerprintBrand[] = "fingerprint-brand";
const char kFingerprintBrandVersion[] = "fingerprint-brand-version";
const char kFingerprintDeviceModel[] = "fingerprint-device-model";
const char kFingerprintGpuVendor[] = "fingerprint-gpu-vendor";
const char kFingerprintGpuRenderer[] = "fingerprint-gpu-renderer";
const char kFingerprintHardwareConcurrency[] = "fingerprint-hardware-concurrency";
const char kFingerprintTimezone[] = "fingerprint-timezone";
const char kFingerprintLanguages[] = "fingerprint-languages";
const char kFingerprintScreen[] = "fingerprint-screen";
const char kDisableSpoofing[] = "disable-spoofing";
const char kFingerprintWebrtcPublicIp[] = "fingerprint-webrtc-public-ip";
const char kFingerprintPrefersColorScheme[] =
    "fingerprint-prefers-color-scheme";
const char kFingerprintPrefersReducedMotion[] =
    "fingerprint-prefers-reduced-motion";
const char kFingerprintPrefersReducedTransparency[] =
    "fingerprint-prefers-reduced-transparency";
const char kTanyaSkipPreflightHosts[] = "tanya-skip-preflight-hosts";
const char kFingerprintStealth[] = "fingerprint-stealth";
const char kFingerprintAntiBotBypass[] = "fingerprint-anti-bot-bypass";
const char kFingerprintAudioSampleRate[] = "fingerprint-audio-sample-rate";
const char kFingerprintAudioMaxChannels[] = "fingerprint-audio-max-channels";
const char kFingerprintAudioBaseLatency[] = "fingerprint-audio-base-latency";
const char kFingerprintAudioOutputLatency[] =
    "fingerprint-audio-output-latency";
const char kFingerprintWebglExtensions[] = "fingerprint-webgl-extensions";
const char kFingerprintDeviceMemory[] = "fingerprint-device-memory";

bool IsTanyaBypassHost(std::string_view host) {
  const std::string csv =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          kTanyaSkipPreflightHosts);
  if (csv.empty() || host.empty()) {
    return false;
  }
  for (std::string_view entry :
       base::SplitStringPiece(csv, ",", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (entry.starts_with(".")) {

      std::string_view bare = entry.substr(1);
      if (host == bare ||
          (host.size() > bare.size() && host.ends_with(bare) &&
           host[host.size() - bare.size() - 1] == '.')) {
        return true;
      }
    } else {
      if (host == entry) {
        return true;
      }
    }
  }
  return false;
}

}  
