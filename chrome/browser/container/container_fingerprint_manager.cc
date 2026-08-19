
#include "chrome/browser/container/container_fingerprint_manager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <random>
#include <sstream>
#include <string_view>

#include "base/hash/hash.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "content/public/browser/browser_context.h"

namespace tab_container {

namespace {

const std::vector<std::string>& GetStandardFonts() {
  static const base::NoDestructor<std::vector<std::string>> kFonts(
      std::vector<std::string>{
          "Arial",        "Arial Black",    "Comic Sans MS", "Courier New",
          "Georgia",      "Impact",         "Times New Roman",
          "Trebuchet MS", "Verdana",        "Webdings",      "Wingdings",
          "Tahoma",       "Palatino Linotype", "Lucida Console"});
  return *kFonts;
}

struct WebGLSpoofProfile {
  const char* vendor;
  const char* renderer;
};

constexpr std::array<WebGLSpoofProfile, 6> kWebGLSpoofProfiles{{
    {"Google Inc. (Intel)", "ANGLE (Intel, Intel(R) UHD Graphics 620, OpenGL 4.6)"},
    {"Google Inc. (NVIDIA)", "ANGLE (NVIDIA, NVIDIA GeForce GTX 1060, OpenGL 4.6)"},
    {"Google Inc. (AMD)", "ANGLE (AMD, AMD Radeon RX 580, OpenGL 4.6)"},
    {"Google Inc. (Intel)", "ANGLE (Intel, Intel(R) Iris Plus Graphics 640, OpenGL 4.6)"},
    {"Intel Inc.", "Intel(R) HD Graphics 630"},
    {"NVIDIA Corporation", "GeForce GTX 1050/PCIe/SSE2"},
}};

std::string FingerprintProtectionLevelToString(FingerprintProtectionLevel level) {
  switch (level) {
    case FingerprintProtectionLevel::kNone: return "None";
    case FingerprintProtectionLevel::kBasic: return "Basic";
    case FingerprintProtectionLevel::kStandard: return "Standard";
    case FingerprintProtectionLevel::kStrict: return "Strict";
    case FingerprintProtectionLevel::kMaximum: return "Maximum";
    case FingerprintProtectionLevel::kCustom: return "Custom";
  }
  return "Unknown";
}

}  

bool ContainerFingerprintConfig::IsValid() const {

  if (canvas_config.noise_factor < 0 || canvas_config.noise_factor > 1) {
    return false;
  }
  if (audio_config.noise_factor < 0 || audio_config.noise_factor > 1) {
    return false;
  }
  return true;
}

void ContainerFingerprintConfig::ApplyPreset(FingerprintProtectionLevel level) {
  protection_level = level;

  switch (level) {
    case FingerprintProtectionLevel::kNone:

      canvas_config.mode = CanvasProtectionMode::kOff;
      webgl_config.mode = WebGLProtectionMode::kOff;
      audio_config.mode = AudioProtectionMode::kOff;
      font_config.mode = FontProtectionMode::kOff;
      navigator_config = NavigatorConfig();
      screen_config = ScreenConfig();
      timezone_config = TimezoneConfig();
      geolocation_config = GeolocationConfig();
      battery_config.block_battery_api = false;
      webrtc_config = WebRTCConfig();
      break;

    case FingerprintProtectionLevel::kBasic:

      canvas_config.mode = CanvasProtectionMode::kNoise;
      canvas_config.noise_factor = 0.00005;
      webgl_config.mode = WebGLProtectionMode::kSpoofVendor;
      audio_config.mode = AudioProtectionMode::kOff;
      font_config.mode = FontProtectionMode::kOff;
      navigator_config.hide_plugins = true;
      battery_config.block_battery_api = true;
      webrtc_config.hide_local_ips = true;
      break;

    case FingerprintProtectionLevel::kStandard:

      canvas_config.mode = CanvasProtectionMode::kNoise;
      canvas_config.noise_factor = 0.0001;
      canvas_config.consistent_per_origin = true;
      webgl_config.mode = WebGLProtectionMode::kSpoofVendor;
      audio_config.mode = AudioProtectionMode::kNoise;
      audio_config.noise_factor = 0.0001;
      font_config.mode = FontProtectionMode::kStandardSet;
      navigator_config.hide_plugins = true;
      navigator_config.hide_mime_types = true;
      navigator_config.spoof_hardware_concurrency = true;
      navigator_config.spoofed_hardware_concurrency = 4;
      battery_config.block_battery_api = true;
      webrtc_config.hide_local_ips = true;
      webrtc_config.disable_non_proxied_udp = true;
      break;

    case FingerprintProtectionLevel::kStrict:

      canvas_config.mode = CanvasProtectionMode::kNoise;
      canvas_config.noise_factor = 0.0005;
      canvas_config.consistent_per_origin = true;
      webgl_config.mode = WebGLProtectionMode::kSpoofAll;
      webgl_config.randomize_parameters = true;
      audio_config.mode = AudioProtectionMode::kNoise;
      audio_config.noise_factor = 0.0005;
      font_config.mode = FontProtectionMode::kStandardSet;
      font_config.randomize_metrics = true;
      navigator_config.hide_plugins = true;
      navigator_config.hide_mime_types = true;
      navigator_config.spoof_hardware_concurrency = true;
      navigator_config.spoofed_hardware_concurrency = 4;
      navigator_config.spoof_device_memory = true;
      navigator_config.spoofed_device_memory = 8;
      screen_config.hide_multi_monitor = true;
      battery_config.block_battery_api = true;
      webrtc_config.hide_local_ips = true;
      webrtc_config.disable_non_proxied_udp = true;
      webrtc_config.block_mdns = true;
      speech_config.block_speech_synthesis = true;
      break;

    case FingerprintProtectionLevel::kMaximum:

      canvas_config.mode = CanvasProtectionMode::kFake;
      webgl_config.mode = WebGLProtectionMode::kBlock;
      audio_config.mode = AudioProtectionMode::kBlock;
      font_config.mode = FontProtectionMode::kBlock;
      navigator_config.hide_plugins = true;
      navigator_config.hide_mime_types = true;
      navigator_config.spoof_hardware_concurrency = true;
      navigator_config.spoofed_hardware_concurrency = 4;
      navigator_config.spoof_device_memory = true;
      navigator_config.spoofed_device_memory = 8;
      navigator_config.spoof_platform = true;
      navigator_config.spoofed_platform = "Win32";
      screen_config.spoof_resolution = true;
      screen_config.hide_multi_monitor = true;
      battery_config.block_battery_api = true;
      webrtc_config.block_webrtc = true;
      geolocation_config.block_geolocation = true;
      speech_config.block_speech_synthesis = true;
      break;

    case FingerprintProtectionLevel::kCustom:

      break;
  }
}

std::string ContainerFingerprintConfig::Serialize() const {
  base::DictValue dict;
  dict.Set("container_id", container_id);
  dict.Set("protection_level", static_cast<int>(protection_level));

  base::DictValue canvas_dict;
  canvas_dict.Set("mode", static_cast<int>(canvas_config.mode));
  canvas_dict.Set("noise_factor", canvas_config.noise_factor);
  canvas_dict.Set("consistent_per_origin", canvas_config.consistent_per_origin);
  canvas_dict.Set("consistent_per_session", canvas_config.consistent_per_session);
  canvas_dict.Set("seed", canvas_config.seed);
  dict.Set("canvas_config", std::move(canvas_dict));

  base::DictValue webgl_dict;
  webgl_dict.Set("mode", static_cast<int>(webgl_config.mode));
  webgl_dict.Set("spoofed_vendor", webgl_config.spoofed_vendor);
  webgl_dict.Set("spoofed_renderer", webgl_config.spoofed_renderer);
  webgl_dict.Set("randomize_parameters", webgl_config.randomize_parameters);
  dict.Set("webgl_config", std::move(webgl_dict));

  base::DictValue audio_dict;
  audio_dict.Set("mode", static_cast<int>(audio_config.mode));
  audio_dict.Set("noise_factor", audio_config.noise_factor);
  audio_dict.Set("block_audio_worklet", audio_config.block_audio_worklet);
  dict.Set("audio_config", std::move(audio_dict));

  base::DictValue font_dict;
  font_dict.Set("mode", static_cast<int>(font_config.mode));
  font_dict.Set("block_font_enumeration", font_config.block_font_enumeration);
  font_dict.Set("randomize_metrics", font_config.randomize_metrics);
  dict.Set("font_config", std::move(font_dict));

  base::DictValue nav_dict;
  nav_dict.Set("spoof_platform", navigator_config.spoof_platform);
  nav_dict.Set("spoofed_platform", navigator_config.spoofed_platform);
  nav_dict.Set("hide_plugins", navigator_config.hide_plugins);
  nav_dict.Set("hide_mime_types", navigator_config.hide_mime_types);
  nav_dict.Set("spoof_hardware_concurrency", 
               navigator_config.spoof_hardware_concurrency);
  nav_dict.Set("spoofed_hardware_concurrency",
               navigator_config.spoofed_hardware_concurrency);
  nav_dict.Set("spoof_device_memory", navigator_config.spoof_device_memory);
  nav_dict.Set("spoofed_device_memory", navigator_config.spoofed_device_memory);
  dict.Set("navigator_config", std::move(nav_dict));

  base::DictValue webrtc_dict;
  webrtc_dict.Set("block_webrtc", webrtc_config.block_webrtc);
  webrtc_dict.Set("disable_non_proxied_udp", 
                  webrtc_config.disable_non_proxied_udp);
  webrtc_dict.Set("hide_local_ips", webrtc_config.hide_local_ips);
  webrtc_dict.Set("block_mdns", webrtc_config.block_mdns);
  dict.Set("webrtc_config", std::move(webrtc_dict));

  base::DictValue battery_dict;
  battery_dict.Set("block_battery_api", battery_config.block_battery_api);
  dict.Set("battery_config", std::move(battery_dict));

  std::string output;
  base::JSONWriter::Write(dict, &output);
  return output;
}

std::optional<ContainerFingerprintConfig> 
ContainerFingerprintConfig::Deserialize(const std::string& data) {
  auto parsed =
      base::JSONReader::Read(data, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }

  ContainerFingerprintConfig config;
  const base::DictValue& dict = parsed->GetDict();

  const std::string* container_id = dict.FindString("container_id");
  if (container_id) config.container_id = *container_id;

  config.protection_level = static_cast<FingerprintProtectionLevel>(
      dict.FindInt("protection_level").value_or(2));

  return config;
}

ContainerFingerprintManager::ContainerFingerprintManager(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  InitializeBuiltinProfiles();

  ;
}

ContainerFingerprintManager::~ContainerFingerprintManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ;
}

void ContainerFingerprintManager::AddObserver(
    ContainerFingerprintObserver* observer) {
  observers_.AddObserver(observer);
}

void ContainerFingerprintManager::RemoveObserver(
    ContainerFingerprintObserver* observer) {
  observers_.RemoveObserver(observer);
}

bool ContainerFingerprintManager::SetFingerprintConfig(
    const std::string& container_id,
    const ContainerFingerprintConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!config.IsValid()) {
    ;
    return false;
  }

  ;

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    auto info = std::make_unique<ContainerFingerprintInfo>();
    info->container_id = container_id;
    info->config = config;
    info->config.container_id = container_id;
    info->canvas_noise_seed = GenerateRandomString(32, container_id);
    info->last_activity = base::TimeTicks::Now();
    container_fingerprints_[container_id] = std::move(info);
  } else {
    it->second->config = config;
    it->second->config.container_id = container_id;
    it->second->last_activity = base::TimeTicks::Now();
  }

  NotifyConfigUpdated(container_id, config);

  return true;
}

std::optional<ContainerFingerprintConfig>
ContainerFingerprintManager::GetFingerprintConfig(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return std::nullopt;
  }

  return it->second->config;
}

bool ContainerFingerprintManager::RemoveFingerprintConfig(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return container_fingerprints_.erase(container_id) > 0;
}

bool ContainerFingerprintManager::SetProtectionLevel(
    const std::string& container_id,
    FingerprintProtectionLevel level) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerFingerprintConfig config;
  auto it = container_fingerprints_.find(container_id);
  if (it != container_fingerprints_.end()) {
    config = it->second->config;
  }

  config.container_id = container_id;
  config.ApplyPreset(level);

  return SetFingerprintConfig(container_id, config);
}

FingerprintProtectionLevel ContainerFingerprintManager::GetProtectionLevel(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return FingerprintProtectionLevel::kNone;
  }

  return it->second->config.protection_level;
}

bool ContainerFingerprintManager::CreateProfile(const FingerprintProfile& profile) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (profile.profile_id.empty()) {
    return false;
  }

  profiles_[profile.profile_id] = profile;
  profiles_[profile.profile_id].created_at = base::Time::Now();

  ;

  return true;
}

std::optional<FingerprintProfile> ContainerFingerprintManager::GetProfile(
    const std::string& profile_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = profiles_.find(profile_id);
  if (it == profiles_.end()) {
    return std::nullopt;
  }

  return it->second;
}

std::vector<FingerprintProfile> 
ContainerFingerprintManager::GetAllProfiles() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<FingerprintProfile> result;
  for (const auto& pair : profiles_) {
    result.push_back(pair.second);
  }
  return result;
}

std::vector<FingerprintProfile>
ContainerFingerprintManager::GetBuiltinProfiles() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<FingerprintProfile> result;
  for (const auto& pair : profiles_) {
    if (pair.second.is_builtin) {
      result.push_back(pair.second);
    }
  }
  return result;
}

bool ContainerFingerprintManager::ApplyProfile(
    const std::string& container_id,
    const std::string& profile_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto profile_it = profiles_.find(profile_id);
  if (profile_it == profiles_.end()) {
    ;
    return false;
  }

  FingerprintProfile& profile = profile_it->second;
  profile.last_used = base::Time::Now();

  auto it = container_fingerprints_.find(container_id);
  if (it != container_fingerprints_.end()) {
    it->second->applied_profile_id = profile_id;
  }

  return SetFingerprintConfig(container_id, profile.config);
}

bool ContainerFingerprintManager::DeleteProfile(const std::string& profile_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = profiles_.find(profile_id);
  if (it == profiles_.end()) {
    return false;
  }

  if (it->second.is_builtin) {
    ;
    return false;
  }

  profiles_.erase(it);
  return true;
}

ContainerFingerprintManager::CanvasReadbackResult
ContainerFingerprintManager::GetProtectedCanvasData(
    const std::string& container_id,
    const std::vector<uint8_t>& original_data,
    int width, int height,
    const std::string& origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  CanvasReadbackResult result;
  result.data = original_data;

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    result.success = true;
    return result;
  }

  const ContainerFingerprintConfig& config = it->second->config;

  ReportFingerprintAttempt(container_id, "canvas", origin, 
                           base::StringPrintf("Canvas readback %dx%d", width, height));

  switch (config.canvas_config.mode) {
    case CanvasProtectionMode::kOff:
      result.success = true;
      break;

    case CanvasProtectionMode::kNoise: {
      std::string seed = GenerateCanvasNoiseSeed(container_id, origin);
      ApplyNoiseToData(result.data, config.canvas_config.noise_factor, seed);
      result.success = true;
      result.protection_applied = true;
      result.protection_method = "noise";
      break;
    }

    case CanvasProtectionMode::kBlock:
      result.success = false;
      result.protection_applied = true;
      result.protection_method = "blocked";
      result.data.clear();
      break;

    case CanvasProtectionMode::kFake: {

      std::mt19937 rng(base::Hash(container_id + origin));
      std::uniform_int_distribution<uint8_t> dist(0, 255);
      for (auto& byte : result.data) {
        byte = dist(rng);
      }
      result.success = true;
      result.protection_applied = true;
      result.protection_method = "fake";
      break;
    }

    case CanvasProtectionMode::kPermission:
      result.success = false;
      result.protection_applied = true;
      result.protection_method = "permission";
      result.data.clear();
      break;
  }

  if (result.protection_applied) {
    NotifyProtectionTriggered(container_id, "canvas", result.protection_method);
    for (auto& observer : observers_) {
      observer.OnCanvasFingerprintDetected(container_id, origin);
    }
  }

  return result;
}

std::string ContainerFingerprintManager::GenerateCanvasNoiseSeed(
    const std::string& container_id,
    const std::string& origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return container_id;
  }

  const ContainerFingerprintConfig& config = it->second->config;
  std::string base_seed = it->second->canvas_noise_seed;

  if (!config.canvas_config.seed.empty()) {
    base_seed = config.canvas_config.seed;
  }

  if (config.canvas_config.consistent_per_origin) {
    return base_seed + origin;
  } else if (config.canvas_config.consistent_per_session) {
    return base_seed;
  } else {
    return base_seed + base::NumberToString(base::RandUint64());
  }
}

ContainerFingerprintManager::WebGLParameters
ContainerFingerprintManager::GetSpoofedWebGLParameters(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  WebGLParameters params;

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {

    params.vendor = "Google Inc.";
    params.renderer = "ANGLE";
    params.version = "WebGL 1.0";
    params.shading_language_version = "WebGL GLSL ES 1.0";
    return params;
  }

  const ContainerFingerprintConfig::WebGLConfig& config = 
      it->second->config.webgl_config;

  if (config.mode == WebGLProtectionMode::kOff) {
    return params;
  }

  if (!config.spoofed_vendor.empty()) {
    params.vendor = config.spoofed_vendor;
    params.renderer = config.spoofed_renderer;
  } else {

    const size_t profile_idx =
        base::Hash(container_id) % kWebGLSpoofProfiles.size();
    const WebGLSpoofProfile& profile = kWebGLSpoofProfiles.at(profile_idx);
    params.vendor = profile.vendor;
    params.renderer = profile.renderer;
  }

  params.version = "WebGL 1.0 (OpenGL ES 2.0 Chromium)";
  params.shading_language_version = "WebGL GLSL ES 1.0 (OpenGL ES GLSL ES 1.0 Chromium)";

  if (config.randomize_parameters) {

    int base_hash = base::Hash(container_id);
    params.parameters[0x0D33] = 16384 - (base_hash % 2048);  
    params.parameters[0x0D3A] = 16384 - (base_hash % 2048);  
    params.parameters[0x8869] = 16 + (base_hash % 16);       
    params.parameters[0x8DFB] = 32 + (base_hash % 32);       
  }

  NotifyProtectionTriggered(container_id, "webgl", "parameters_spoofed");

  return params;
}

std::vector<uint8_t> ContainerFingerprintManager::GetProtectedWebGLData(
    const std::string& container_id,
    const std::vector<uint8_t>& original_data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<uint8_t> data = original_data;

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return data;
  }

  const ContainerFingerprintConfig::WebGLConfig& config = 
      it->second->config.webgl_config;

  if (config.mode == WebGLProtectionMode::kNoise ||
      config.mode == WebGLProtectionMode::kSpoofAll) {
    std::string seed = container_id + "_webgl";
    ApplyNoiseToData(data, 0.0001, seed);
  }

  return data;
}

std::vector<float> ContainerFingerprintManager::GetProtectedAudioData(
    const std::string& container_id,
    const std::vector<float>& original_data,
    int sample_rate) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<float> data = original_data;

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return data;
  }

  const ContainerFingerprintConfig::AudioConfig& config =
      it->second->config.audio_config;

  if (config.mode == AudioProtectionMode::kNoise) {
    ApplyNoiseToAudio(data, config.noise_factor, container_id + "_audio");
    NotifyProtectionTriggered(container_id, "audio", "noise_applied");
    for (auto& observer : observers_) {
      observer.OnAudioFingerprintDetected(container_id, "");
    }
  } else if (config.mode == AudioProtectionMode::kFake) {

    std::mt19937 rng(base::Hash(container_id));
    std::uniform_real_distribution<float> dist(-0.001f, 0.001f);
    for (auto& sample : data) {
      sample = dist(rng);
    }
    NotifyProtectionTriggered(container_id, "audio", "fake_data");
  }

  return data;
}

ContainerFingerprintManager::AudioContextParameters
ContainerFingerprintManager::GetSpoofedAudioParameters(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  AudioContextParameters params;
  params.sample_rate = 44100;
  params.channel_count = 2;
  params.base_latency = 0.01;
  params.output_latency = 0.01;

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return params;
  }

  const ContainerFingerprintConfig::AudioConfig& config =
      it->second->config.audio_config;

  if (config.sample_rate_override > 0) {
    params.sample_rate = config.sample_rate_override;
  }
  if (config.channel_count_override > 0) {
    params.channel_count = config.channel_count_override;
  }

  return params;
}

ContainerFingerprintManager::NavigatorProperties
ContainerFingerprintManager::GetSpoofedNavigatorProperties(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  NavigatorProperties props;

  props.platform = "Win32";
  props.languages = {"en-US", "en"};
  props.hardware_concurrency = 4;
  props.device_memory = 8;
  props.max_touch_points = 0;
  props.connection_type = "4g";
  props.do_not_track = false;

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return props;
  }

  const ContainerFingerprintConfig::NavigatorConfig& config =
      it->second->config.navigator_config;

  if (config.spoof_platform && !config.spoofed_platform.empty()) {
    props.platform = config.spoofed_platform;
  }
  if (config.spoof_languages && !config.spoofed_languages.empty()) {
    props.languages = config.spoofed_languages;
  }
  if (config.spoof_hardware_concurrency) {
    props.hardware_concurrency = config.spoofed_hardware_concurrency;
  }
  if (config.spoof_device_memory) {
    props.device_memory = config.spoofed_device_memory;
  }
  if (config.spoof_max_touch_points) {
    props.max_touch_points = config.spoofed_max_touch_points;
  }
  if (config.spoof_connection_type) {
    props.connection_type = config.spoofed_connection_type;
  }
  if (config.spoof_do_not_track) {
    props.do_not_track = config.spoofed_do_not_track;
  }
  if (config.hide_plugins) {
    props.plugins.clear();
  }
  if (config.hide_mime_types) {
    props.mime_types.clear();
  }

  return props;
}

ContainerFingerprintManager::ScreenProperties
ContainerFingerprintManager::GetSpoofedScreenProperties(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ScreenProperties props;
  props.width = 1920;
  props.height = 1080;
  props.avail_width = 1920;
  props.avail_height = 1040;
  props.color_depth = 24;
  props.pixel_ratio = 1.0;
  props.orientation = "landscape-primary";

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return props;
  }

  const ContainerFingerprintConfig::ScreenConfig& config =
      it->second->config.screen_config;

  if (config.spoof_resolution) {
    props.width = config.spoofed_width;
    props.height = config.spoofed_height;
    props.avail_width = config.spoofed_width;
    props.avail_height = config.spoofed_height - 40;  
  }
  if (config.spoof_color_depth) {
    props.color_depth = config.spoofed_color_depth;
  }
  if (config.spoof_pixel_ratio) {
    props.pixel_ratio = config.spoofed_pixel_ratio;
  }
  if (config.spoof_orientation && !config.spoofed_orientation.empty()) {
    props.orientation = config.spoofed_orientation;
  }

  return props;
}

ContainerFingerprintManager::TimezoneInfo
ContainerFingerprintManager::GetSpoofedTimezone(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  TimezoneInfo info;
  info.timezone_id = "America/New_York";
  info.offset_minutes = -300;
  info.display_name = "Eastern Standard Time";

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return info;
  }

  const ContainerFingerprintConfig::TimezoneConfig& config =
      it->second->config.timezone_config;

  if (config.spoof_timezone && !config.spoofed_timezone.empty()) {
    info.timezone_id = config.spoofed_timezone;
    info.offset_minutes = config.spoofed_offset_minutes;
  }

  return info;
}

ContainerFingerprintManager::GeolocationInfo
ContainerFingerprintManager::GetSpoofedGeolocation(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  GeolocationInfo info;
  info.latitude = 40.7128;   
  info.longitude = -74.0060;
  info.accuracy = 100.0;
  info.altitude = 0.0;
  info.altitude_accuracy = 0.0;
  info.heading = 0.0;
  info.speed = 0.0;

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return info;
  }

  const ContainerFingerprintConfig::GeolocationConfig& config =
      it->second->config.geolocation_config;

  if (config.spoof_location) {
    info.latitude = config.spoofed_latitude;
    info.longitude = config.spoofed_longitude;
    info.accuracy = config.spoofed_accuracy;

    if (config.add_noise) {

      double noise_lat = GenerateRandomDouble(
          -config.noise_radius_meters / 111320.0,
          config.noise_radius_meters / 111320.0,
          container_id + "_geo_lat");
      double noise_lng = GenerateRandomDouble(
          -config.noise_radius_meters /
              (111320.0 * std::cos(info.latitude * std::numbers::pi_v<double> /
                                   180.0)),
          config.noise_radius_meters /
              (111320.0 * std::cos(info.latitude * std::numbers::pi_v<double> /
                                   180.0)),
          container_id + "_geo_lng");
      info.latitude += noise_lat;
      info.longitude += noise_lng;
    }
  }

  return info;
}

std::vector<std::string> ContainerFingerprintManager::GetAllowedFonts(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return GetStandardFonts();
  }

  const ContainerFingerprintConfig::FontConfig& config =
      it->second->config.font_config;

  switch (config.mode) {
    case FontProtectionMode::kOff:
      return {};  

    case FontProtectionMode::kStandardSet:
      return GetStandardFonts();

    case FontProtectionMode::kRandomSubset: {

      std::vector<std::string> subset;
      std::mt19937 rng(base::Hash(container_id));
      std::vector<std::string> all_fonts = GetStandardFonts();
      std::shuffle(all_fonts.begin(), all_fonts.end(), rng);
      size_t count = all_fonts.size() / 2 + (rng() % (all_fonts.size() / 4));
      for (size_t i = 0; i < count && i < all_fonts.size(); ++i) {
        subset.push_back(all_fonts[i]);
      }
      return subset;
    }

    case FontProtectionMode::kBlock:
      return {"Arial"};  
  }

  return GetStandardFonts();
}

bool ContainerFingerprintManager::IsFontAllowed(
    const std::string& container_id,
    const std::string& font_name) {
  std::vector<std::string> allowed = GetAllowedFonts(container_id);
  if (allowed.empty()) {
    return true;  
  }
  return std::find(allowed.begin(), allowed.end(), font_name) != allowed.end();
}

ContainerFingerprintManager::FontMetrics
ContainerFingerprintManager::GetSpoofedFontMetrics(
    const std::string& container_id,
    const std::string& font_name,
    const FontMetrics& original) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  FontMetrics metrics = original;

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return metrics;
  }

  const ContainerFingerprintConfig::FontConfig& config =
      it->second->config.font_config;

  if (config.randomize_metrics) {

    std::string seed = container_id + font_name;
    double factor = 1.0 + GenerateRandomDouble(-0.02, 0.02, seed);
    metrics.ascent *= factor;
    metrics.descent *= factor;
    metrics.height *= factor;
    metrics.width *= factor;
  }

  return metrics;
}

void ContainerFingerprintManager::ReportFingerprintAttempt(
    const std::string& container_id,
    const std::string& type,
    const std::string& origin,
    const std::string& details) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return;
  }

  FingerprintAttempt attempt;
  attempt.timestamp = base::TimeTicks::Now();
  attempt.container_id = container_id;
  attempt.type = type;
  attempt.origin = origin;
  attempt.details = details;
  attempt.was_blocked = false;  

  it->second->attempts.push_back(attempt);
  it->second->stats.total_attempts++;

  if (type == "canvas") it->second->stats.canvas_attempts++;
  else if (type == "webgl") it->second->stats.webgl_attempts++;
  else if (type == "audio") it->second->stats.audio_attempts++;
  else if (type == "font") it->second->stats.font_attempts++;
  else if (type == "navigator") it->second->stats.navigator_attempts++;

  it->second->stats.attempts_by_origin[origin]++;

  if (it->second->attempts.size() > 1000) {
    it->second->attempts.erase(
        it->second->attempts.begin(),
        it->second->attempts.begin() + 500);
  }

  if (debug_logging_enabled_) {
    ;
  }
}

std::vector<ContainerFingerprintManager::FingerprintAttempt>
ContainerFingerprintManager::GetFingerprintAttempts(
    const std::string& container_id,
    size_t count) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return {};
  }

  const auto& attempts = it->second->attempts;
  if (attempts.size() <= count) {
    return attempts;
  }

  return std::vector<FingerprintAttempt>(
      attempts.end() - count, attempts.end());
}

ContainerFingerprintManager::AttemptStatistics
ContainerFingerprintManager::GetAttemptStatistics(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_fingerprints_.find(container_id);
  if (it == container_fingerprints_.end()) {
    return AttemptStatistics();
  }

  return it->second->stats;
}

void ContainerFingerprintManager::SetDebugLoggingEnabled(bool enabled) {
  debug_logging_enabled_ = enabled;
}

std::string ContainerFingerprintManager::GetDiagnosticReport() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::stringstream report;
  report << "=== ContainerFingerprintManager Diagnostic Report ===\n\n";

  report << "Profiles: " << profiles_.size() << "\n";
  for (const auto& pair : profiles_) {
    report << "  - " << pair.first;
    if (pair.second.is_builtin) report << " (builtin)";
    report << "\n";
  }

  report << "\nContainer Configurations: " << container_fingerprints_.size() << "\n";
  for (const auto& pair : container_fingerprints_) {
    const auto* info = pair.second.get();
    report << "  - " << pair.first << ":\n";
    report << "      Protection Level: " 
           << FingerprintProtectionLevelToString(info->config.protection_level) << "\n";
    report << "      Canvas Mode: " << static_cast<int>(info->config.canvas_config.mode) << "\n";
    report << "      WebGL Mode: " << static_cast<int>(info->config.webgl_config.mode) << "\n";
    report << "      Total Attempts: " << info->stats.total_attempts << "\n";
    report << "      Canvas Attempts: " << info->stats.canvas_attempts << "\n";
    report << "      WebGL Attempts: " << info->stats.webgl_attempts << "\n";
    if (!info->applied_profile_id.empty()) {
      report << "      Applied Profile: " << info->applied_profile_id << "\n";
    }
  }

  return report.str();
}

void ContainerFingerprintManager::DumpStateToLog() const {
  ;
}

int ContainerFingerprintManager::GenerateRandomInt(
    int min, int max, const std::string& seed) {
  std::mt19937 rng(base::Hash(seed));
  std::uniform_int_distribution<int> dist(min, max);
  return dist(rng);
}

double ContainerFingerprintManager::GenerateRandomDouble(
    double min, double max, const std::string& seed) {
  std::mt19937 rng(base::Hash(seed));
  std::uniform_real_distribution<double> dist(min, max);
  return dist(rng);
}

std::string ContainerFingerprintManager::GenerateRandomString(
    size_t length, const std::string& seed) {
  constexpr std::string_view kCharsetChars =
      "abcdefghijklmnopqrstuvwxyz0123456789";
  std::mt19937 rng(base::Hash(seed));
  std::uniform_int_distribution<size_t> dist(0, kCharsetChars.size() - 1);

  std::string result;
  result.reserve(length);
  for (size_t i = 0; i < length; ++i) {
    result.push_back(kCharsetChars.at(dist(rng)));
  }
  return result;
}

void ContainerFingerprintManager::ApplyNoiseToData(
    std::vector<uint8_t>& data,
    double noise_factor,
    const std::string& seed) {
  std::mt19937 rng(base::Hash(seed));
  std::uniform_real_distribution<double> dist(-noise_factor, noise_factor);

  for (auto& byte : data) {
    double noise = dist(rng) * 255.0;
    int new_value = static_cast<int>(byte) + static_cast<int>(noise);
    byte = static_cast<uint8_t>(std::clamp(new_value, 0, 255));
  }
}

void ContainerFingerprintManager::ApplyNoiseToAudio(
    std::vector<float>& data,
    double noise_factor,
    const std::string& seed) {
  std::mt19937 rng(base::Hash(seed));
  std::uniform_real_distribution<float> dist(
      static_cast<float>(-noise_factor),
      static_cast<float>(noise_factor));

  for (auto& sample : data) {
    sample += dist(rng);
    sample = std::clamp(sample, -1.0f, 1.0f);
  }
}

void ContainerFingerprintManager::NotifyConfigUpdated(
    const std::string& container_id,
    const ContainerFingerprintConfig& config) {
  for (auto& observer : observers_) {
    observer.OnFingerprintConfigUpdated(container_id, config);
  }
}

void ContainerFingerprintManager::NotifyProtectionTriggered(
    const std::string& container_id,
    const std::string& type,
    const std::string& details) {
  for (auto& observer : observers_) {
    observer.OnFingerprintProtectionTriggered(container_id, type, details);
  }
}

void ContainerFingerprintManager::InitializeBuiltinProfiles() {

  {
    FingerprintProfile profile;
    profile.profile_id = "windows_chrome_desktop";
    profile.name = "Windows Chrome Desktop";
    profile.description = "Standard Windows Chrome desktop fingerprint";
    profile.device_type = "desktop";
    profile.os_type = "windows";
    profile.browser_type = "chrome";
    profile.is_builtin = true;
    profile.config.ApplyPreset(FingerprintProtectionLevel::kStandard);
    profile.config.navigator_config.spoof_platform = true;
    profile.config.navigator_config.spoofed_platform = "Win32";
    profile.config.screen_config.spoofed_width = 1920;
    profile.config.screen_config.spoofed_height = 1080;
    profiles_[profile.profile_id] = profile;
  }

  {
    FingerprintProfile profile;
    profile.profile_id = "mac_chrome_desktop";
    profile.name = "macOS Chrome Desktop";
    profile.description = "Standard macOS Chrome desktop fingerprint";
    profile.device_type = "desktop";
    profile.os_type = "macos";
    profile.browser_type = "chrome";
    profile.is_builtin = true;
    profile.config.ApplyPreset(FingerprintProtectionLevel::kStandard);
    profile.config.navigator_config.spoof_platform = true;
    profile.config.navigator_config.spoofed_platform = "MacIntel";
    profile.config.screen_config.spoofed_width = 1440;
    profile.config.screen_config.spoofed_height = 900;
    profile.config.screen_config.spoof_pixel_ratio = true;
    profile.config.screen_config.spoofed_pixel_ratio = 2.0;
    profiles_[profile.profile_id] = profile;
  }

  {
    FingerprintProfile profile;
    profile.profile_id = "android_chrome_mobile";
    profile.name = "Android Chrome Mobile";
    profile.description = "Standard Android Chrome mobile fingerprint";
    profile.device_type = "mobile";
    profile.os_type = "android";
    profile.browser_type = "chrome";
    profile.is_builtin = true;
    profile.config.ApplyPreset(FingerprintProtectionLevel::kStandard);
    profile.config.navigator_config.spoof_platform = true;
    profile.config.navigator_config.spoofed_platform = "Linux armv8l";
    profile.config.navigator_config.spoof_max_touch_points = true;
    profile.config.navigator_config.spoofed_max_touch_points = 5;
    profile.config.screen_config.spoof_resolution = true;
    profile.config.screen_config.spoofed_width = 412;
    profile.config.screen_config.spoofed_height = 915;
    profile.config.screen_config.spoof_pixel_ratio = true;
    profile.config.screen_config.spoofed_pixel_ratio = 2.625;
    profiles_[profile.profile_id] = profile;
  }

  {
    FingerprintProfile profile;
    profile.profile_id = "privacy_maximum";
    profile.name = "Maximum Privacy";
    profile.description = "Maximum fingerprint protection (may break sites)";
    profile.device_type = "desktop";
    profile.os_type = "windows";
    profile.browser_type = "chrome";
    profile.is_builtin = true;
    profile.config.ApplyPreset(FingerprintProtectionLevel::kMaximum);
    profiles_[profile.profile_id] = profile;
  }

  ;
}

}  
