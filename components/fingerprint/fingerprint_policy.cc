
#include "components/fingerprint/fingerprint_policy.h"

#include <atomic>
#include <functional>
#include <utility>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "components/fingerprint/switches.h"

namespace fingerprint {

namespace {

std::atomic<const FingerprintPolicy*> g_process_default{nullptr};
}  

FingerprintPolicy::FingerprintPolicy() = default;
FingerprintPolicy::FingerprintPolicy(const FingerprintPolicy&) = default;
FingerprintPolicy::FingerprintPolicy(FingerprintPolicy&&) noexcept = default;
FingerprintPolicy& FingerprintPolicy::operator=(const FingerprintPolicy&) =
    default;
FingerprintPolicy& FingerprintPolicy::operator=(FingerprintPolicy&&) noexcept =
    default;
FingerprintPolicy::~FingerprintPolicy() = default;

FingerprintPolicy FingerprintPolicy::FromCommandLine() {
  FingerprintPolicy p;
  const base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();

  if (cmd->HasSwitch(kFingerprint)) {
    int64_t parsed = 0;
    if (base::StringToInt64(cmd->GetSwitchValueASCII(kFingerprint), &parsed)) {
      p.enabled_ = true;
    }
  }

  p.platform_ = cmd->GetSwitchValueASCII(kFingerprintPlatform);
  p.platform_version_ = cmd->GetSwitchValueASCII(kFingerprintPlatformVersion);
  p.brand_ = cmd->GetSwitchValueASCII(kFingerprintBrand);
  p.brand_version_ = cmd->GetSwitchValueASCII(kFingerprintBrandVersion);
  p.device_model_ = cmd->GetSwitchValueASCII(kFingerprintDeviceModel);
  p.gpu_vendor_ = cmd->GetSwitchValueASCII(kFingerprintGpuVendor);
  p.gpu_renderer_ = cmd->GetSwitchValueASCII(kFingerprintGpuRenderer);
  p.hardware_concurrency_ =
      cmd->GetSwitchValueASCII(kFingerprintHardwareConcurrency);
  p.timezone_ = cmd->GetSwitchValueASCII(kFingerprintTimezone);
  p.languages_ = cmd->GetSwitchValueASCII(kFingerprintLanguages);
  p.screen_ = cmd->GetSwitchValueASCII(kFingerprintScreen);

  p.webrtc_public_ip_ = cmd->GetSwitchValueASCII(kFingerprintWebrtcPublicIp);
  if (!p.webrtc_public_ip_.empty()) {

    p.enabled_ = true;
  }

  p.prefers_color_scheme_ =
      cmd->GetSwitchValueASCII(kFingerprintPrefersColorScheme);
  p.prefers_reduced_motion_ =
      cmd->GetSwitchValueASCII(kFingerprintPrefersReducedMotion);
  p.prefers_reduced_transparency_ =
      cmd->GetSwitchValueASCII(kFingerprintPrefersReducedTransparency);

  p.stealth_ = cmd->GetSwitchValueASCII(kFingerprintStealth) != "0";

  p.anti_bot_bypass_ =
      cmd->GetSwitchValueASCII(kFingerprintAntiBotBypass) == "1";

  p.audio_sample_rate_ = cmd->GetSwitchValueASCII(kFingerprintAudioSampleRate);
  p.audio_max_channels_ =
      cmd->GetSwitchValueASCII(kFingerprintAudioMaxChannels);
  p.audio_base_latency_ =
      cmd->GetSwitchValueASCII(kFingerprintAudioBaseLatency);
  p.audio_output_latency_ =
      cmd->GetSwitchValueASCII(kFingerprintAudioOutputLatency);

  p.device_memory_ = cmd->GetSwitchValueASCII(kFingerprintDeviceMemory);

  const std::string webgl_ext_list =
      cmd->GetSwitchValueASCII(kFingerprintWebglExtensions);
  if (!webgl_ext_list.empty()) {
    p.webgl_extensions_ =
        base::SplitString(webgl_ext_list, ",", base::TRIM_WHITESPACE,
                          base::SPLIT_WANT_NONEMPTY);
  }

  const std::string disable_list = cmd->GetSwitchValueASCII(kDisableSpoofing);
  if (!disable_list.empty()) {
    p.disabled_surfaces_ =
        base::SplitString(disable_list, ",", base::TRIM_WHITESPACE,
                          base::SPLIT_WANT_NONEMPTY);
  }

  return p;
}

const FingerprintPolicy& FingerprintPolicy::ProcessDefault() {
  const FingerprintPolicy* p =
      g_process_default.load(std::memory_order_acquire);
  if (p) {
    return *p;
  }

  auto* fresh = new FingerprintPolicy(FromCommandLine());
  const FingerprintPolicy* expected = nullptr;
  if (g_process_default.compare_exchange_strong(expected, fresh,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
    return *fresh;
  }
  delete fresh;
  return *g_process_default.load(std::memory_order_acquire);
}

void FingerprintPolicy::SetProcessDefaultForRenderer(FingerprintPolicy policy) {
  auto* fresh = new FingerprintPolicy(std::move(policy));

  g_process_default.exchange(fresh, std::memory_order_acq_rel);
}

bool FingerprintPolicy::IsSurfaceDisabled(std::string_view surface) const {
  for (const auto& s : disabled_surfaces_) {
    if (s == surface) {
      return true;
    }
  }
  return false;
}

}  
