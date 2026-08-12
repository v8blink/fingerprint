
#include "components/fingerprint/fingerprint_policy.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/values.h"
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

FingerprintPolicy FingerprintPolicy::FromProfileJson(
    base::span<const uint8_t> bytes) {
  FingerprintPolicy p;
  std::string_view json(reinterpret_cast<const char*>(bytes.data()),
                        bytes.size());
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return p;
  }
  const base::DictValue& dict = parsed->GetDict();
  const auto get = [&dict](const char* key) -> std::string {
    const std::string* v = dict.FindString(key);
    return v ? *v : std::string();
  };
  p.enabled_ = true;
  p.platform_ = get("platform");
  p.platform_version_ = get("platform_version");
  p.brand_ = get("brand");
  p.brand_version_ = get("brand_version");
  p.device_model_ = get("device_model");
  p.gpu_vendor_ = get("gpu_vendor");
  p.gpu_renderer_ = get("gpu_renderer");
  p.hardware_concurrency_ = get("hardware_concurrency");
  p.device_memory_ = get("device_memory");
  p.timezone_ = get("timezone");
  p.languages_ = get("languages");
  p.screen_ = get("screen");
  p.webrtc_public_ip_ = get("webrtc_public_ip");

  size_t surface_count = 0;
  if (const base::DictValue* surfaces = dict.FindDict("surfaces")) {
    surface_count = surfaces->size();
    p.surfaces_ = std::make_shared<const base::DictValue>(surfaces->Clone());
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

bool FingerprintPolicy::has_surface(std::string_view name) const {
  return surfaces_ && surfaces_->Find(name) != nullptr;
}

const base::DictValue* FingerprintPolicy::surface(
    std::string_view name) const {
  return surfaces_ ? surfaces_->FindDict(name) : nullptr;
}

const base::Value* FingerprintPolicy::SurfaceValue(
    std::string_view surface_name,
    std::string_view key) const {
  const base::DictValue* s = surface(surface_name);
  return s ? s->Find(key) : nullptr;
}

bool FingerprintPolicy::SurfaceActive(std::string_view surface_name) const {
  return enabled_ && has_surface(surface_name) &&
         !IsSurfaceDisabled(surface_name);
}

std::optional<std::string> FingerprintPolicy::GetSurfaceString(
    std::string_view surface_name,
    std::string_view key) const {
  const base::Value* v = SurfaceValue(surface_name, key);
  if (v) {
    if (const std::string* s = v->GetIfString()) {
      return *s;
    }
  }
  return std::nullopt;
}

std::optional<int> FingerprintPolicy::GetSurfaceInt(
    std::string_view surface_name,
    std::string_view key) const {
  const base::Value* v = SurfaceValue(surface_name, key);
  if (!v) {
    return std::nullopt;
  }
  if (std::optional<int> i = v->GetIfInt()) {
    return i;
  }
  if (const std::string* s = v->GetIfString()) {
    int parsed = 0;
    if (base::StringToInt(*s, &parsed)) {
      return parsed;
    }
  }
  return std::nullopt;
}

std::optional<double> FingerprintPolicy::GetSurfaceDouble(
    std::string_view surface_name,
    std::string_view key) const {
  const base::Value* v = SurfaceValue(surface_name, key);
  if (!v) {
    return std::nullopt;
  }
  if (std::optional<int> i = v->GetIfInt()) {
    return static_cast<double>(*i);
  }
  if (std::optional<double> d = v->GetIfDouble()) {
    return d;
  }
  if (const std::string* s = v->GetIfString()) {
    double parsed = 0;
    if (base::StringToDouble(*s, &parsed)) {
      return parsed;
    }
  }
  return std::nullopt;
}

const base::ListValue* FingerprintPolicy::GetSurfaceList(
    std::string_view surface_name,
    std::string_view key) const {
  const base::Value* v = SurfaceValue(surface_name, key);
  return v ? v->GetIfList() : nullptr;
}

const base::DictValue* FingerprintPolicy::GetSurfaceDict(
    std::string_view surface_name,
    std::string_view key) const {
  const base::Value* v = SurfaceValue(surface_name, key);
  return v ? v->GetIfDict() : nullptr;
}

const base::ListValue* FingerprintPolicy::GetCanvas2dPixelsByKey(
    std::string_view key) const {
  const base::DictValue* c = surface("canvas2d");
  const base::DictValue* pbk = c ? c->FindDict("pixelsByKey") : nullptr;
  return pbk ? pbk->FindList(key) : nullptr;
}

}
