
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_FINGERPRINT_MANAGER_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_FINGERPRINT_MANAGER_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/values.h"

namespace content {
class BrowserContext;
class WebContents;
}  

namespace tab_container {

enum class FingerprintProtectionLevel {

  kNone = 0,

  kBasic = 1,

  kStandard = 2,

  kStrict = 3,

  kMaximum = 4,

  kCustom = 5,
};

enum class CanvasProtectionMode {
  kOff,           
  kNoise,         
  kBlock,         
  kFake,          
  kPermission,    
};

enum class WebGLProtectionMode {
  kOff,           
  kNoise,         
  kSpoofVendor,   
  kSpoofAll,      
  kBlock,         
};

enum class AudioProtectionMode {
  kOff,           
  kNoise,         
  kBlock,         
  kFake,          
};

enum class FontProtectionMode {
  kOff,           
  kStandardSet,   
  kRandomSubset,  
  kBlock,         
};

struct ContainerFingerprintConfig {
  std::string container_id;
  FingerprintProtectionLevel protection_level = FingerprintProtectionLevel::kStandard;

  struct CanvasConfig {
    CanvasProtectionMode mode = CanvasProtectionMode::kNoise;
    double noise_factor = 0.0001;  
    bool consistent_per_origin = true;  
    bool consistent_per_session = false;  
    std::string seed;  
  };
  CanvasConfig canvas_config;

  struct WebGLConfig {
    WebGLProtectionMode mode = WebGLProtectionMode::kSpoofVendor;
    std::string spoofed_vendor;
    std::string spoofed_renderer;
    std::string spoofed_version;
    std::string spoofed_shading_language_version;
    std::vector<std::string> spoofed_extensions;
    bool randomize_parameters = false;
    int max_texture_size_override = 0;
    int max_viewport_dims_override = 0;
  };
  WebGLConfig webgl_config;

  struct AudioConfig {
    AudioProtectionMode mode = AudioProtectionMode::kNoise;
    double noise_factor = 0.0001;
    bool block_audio_worklet = false;
    int sample_rate_override = 0;
    int channel_count_override = 0;
  };
  AudioConfig audio_config;

  struct FontConfig {
    FontProtectionMode mode = FontProtectionMode::kStandardSet;
    std::vector<std::string> allowed_fonts;
    std::vector<std::string> blocked_fonts;
    bool block_font_enumeration = false;
    bool randomize_metrics = false;
  };
  FontConfig font_config;

  struct NavigatorConfig {
    bool spoof_platform = false;
    std::string spoofed_platform;
    bool spoof_languages = false;
    std::vector<std::string> spoofed_languages;
    bool spoof_hardware_concurrency = false;
    int spoofed_hardware_concurrency = 4;
    bool spoof_device_memory = false;
    int spoofed_device_memory = 8;
    bool spoof_max_touch_points = false;
    int spoofed_max_touch_points = 0;
    bool hide_plugins = true;
    bool hide_mime_types = true;
    bool spoof_do_not_track = false;
    bool spoofed_do_not_track = true;
    bool spoof_connection_type = false;
    std::string spoofed_connection_type;
  };
  NavigatorConfig navigator_config;

  struct ScreenConfig {
    bool spoof_resolution = false;
    int spoofed_width = 1920;
    int spoofed_height = 1080;
    bool spoof_color_depth = false;
    int spoofed_color_depth = 24;
    bool spoof_pixel_ratio = false;
    double spoofed_pixel_ratio = 1.0;
    bool spoof_orientation = false;
    std::string spoofed_orientation;
    bool hide_multi_monitor = true;
  };
  ScreenConfig screen_config;

  struct TimezoneConfig {
    bool spoof_timezone = false;
    std::string spoofed_timezone;
    int spoofed_offset_minutes = 0;
    bool match_ip_location = false;
    bool randomize_per_session = false;
  };
  TimezoneConfig timezone_config;

  struct GeolocationConfig {
    bool spoof_location = false;
    double spoofed_latitude = 0.0;
    double spoofed_longitude = 0.0;
    double spoofed_accuracy = 100.0;
    bool match_ip_location = false;
    bool add_noise = false;
    double noise_radius_meters = 1000.0;
    bool block_geolocation = false;
  };
  GeolocationConfig geolocation_config;

  struct BatteryConfig {
    bool block_battery_api = true;
    bool spoof_battery = false;
    double spoofed_level = 1.0;
    bool spoofed_charging = true;
  };
  BatteryConfig battery_config;

  struct WebRTCConfig {
    bool block_webrtc = false;
    bool disable_non_proxied_udp = true;
    bool hide_local_ips = true;
    std::string spoofed_local_ip;
    bool force_proxy = false;
    bool block_mdns = true;
  };
  WebRTCConfig webrtc_config;

  struct ClientHintsConfig {
    bool spoof_client_hints = false;
    std::string spoofed_brand;
    std::string spoofed_brand_version;
    std::string spoofed_full_version;
    std::string spoofed_platform_version;
    std::string spoofed_architecture;
    std::string spoofed_model;
    bool spoofed_mobile = false;
  };
  ClientHintsConfig client_hints_config;

  struct SpeechConfig {
    bool block_speech_synthesis = false;
    bool spoof_voices = false;
    std::vector<std::string> allowed_voices;
  };
  SpeechConfig speech_config;

  bool IsValid() const;

  void ApplyPreset(FingerprintProtectionLevel level);

  std::string Serialize() const;
  static std::optional<ContainerFingerprintConfig> Deserialize(
      const std::string& data);

  ContainerFingerprintConfig() = default;
};

struct FingerprintProfile {
  std::string profile_id;
  std::string name;
  std::string description;

  std::string device_type;  
  std::string os_type;      
  std::string browser_type; 

  ContainerFingerprintConfig config;

  base::Time created_at;
  base::Time last_used;
  bool is_builtin = false;

  FingerprintProfile() = default;
};

class ContainerFingerprintObserver : public base::CheckedObserver {
 public:
  ~ContainerFingerprintObserver() override = default;

  virtual void OnFingerprintConfigUpdated(const std::string& container_id,
                                          const ContainerFingerprintConfig& config) {}

  virtual void OnFingerprintProtectionTriggered(
      const std::string& container_id,
      const std::string& protection_type,
      const std::string& details) {}

  virtual void OnCanvasFingerprintDetected(const std::string& container_id,
                                           const std::string& origin) {}

  virtual void OnWebGLFingerprintDetected(const std::string& container_id,
                                          const std::string& origin) {}

  virtual void OnAudioFingerprintDetected(const std::string& container_id,
                                          const std::string& origin) {}

  virtual void OnFontFingerprintDetected(const std::string& container_id,
                                         const std::string& origin) {}
};

class ContainerFingerprintManager {
 public:
  explicit ContainerFingerprintManager(content::BrowserContext* browser_context);
  ~ContainerFingerprintManager();

  ContainerFingerprintManager(const ContainerFingerprintManager&) = delete;
  ContainerFingerprintManager& operator=(const ContainerFingerprintManager&) = delete;

  void AddObserver(ContainerFingerprintObserver* observer);
  void RemoveObserver(ContainerFingerprintObserver* observer);

  bool SetFingerprintConfig(const std::string& container_id,
                            const ContainerFingerprintConfig& config);

  std::optional<ContainerFingerprintConfig> GetFingerprintConfig(
      const std::string& container_id) const;

  bool RemoveFingerprintConfig(const std::string& container_id);

  bool SetProtectionLevel(const std::string& container_id,
                          FingerprintProtectionLevel level);

  FingerprintProtectionLevel GetProtectionLevel(
      const std::string& container_id) const;

  bool CreateProfile(const FingerprintProfile& profile);

  std::optional<FingerprintProfile> GetProfile(
      const std::string& profile_id) const;

  std::vector<FingerprintProfile> GetAllProfiles() const;

  std::vector<FingerprintProfile> GetBuiltinProfiles() const;

  bool ApplyProfile(const std::string& container_id,
                    const std::string& profile_id);

  bool DeleteProfile(const std::string& profile_id);

  struct CanvasReadbackResult {
    bool success = false;
    bool protection_applied = false;
    std::vector<uint8_t> data;
    std::string protection_method;
  };
  CanvasReadbackResult GetProtectedCanvasData(
      const std::string& container_id,
      const std::vector<uint8_t>& original_data,
      int width, int height,
      const std::string& origin);

  std::string GenerateCanvasNoiseSeed(const std::string& container_id,
                                      const std::string& origin);

  struct WebGLParameters {
    std::string vendor;
    std::string renderer;
    std::string version;
    std::string shading_language_version;
    std::vector<std::string> extensions;
    std::map<uint32_t, int64_t> parameters;  
  };
  WebGLParameters GetSpoofedWebGLParameters(const std::string& container_id);

  std::vector<uint8_t> GetProtectedWebGLData(
      const std::string& container_id,
      const std::vector<uint8_t>& original_data);

  std::vector<float> GetProtectedAudioData(
      const std::string& container_id,
      const std::vector<float>& original_data,
      int sample_rate);

  struct AudioContextParameters {
    int sample_rate;
    int channel_count;
    double base_latency;
    double output_latency;
  };
  AudioContextParameters GetSpoofedAudioParameters(const std::string& container_id);

  struct NavigatorProperties {
    std::string platform;
    std::vector<std::string> languages;
    int hardware_concurrency;
    int device_memory;
    int max_touch_points;
    std::string connection_type;
    bool do_not_track;
    std::vector<std::string> plugins;
    std::vector<std::string> mime_types;
  };
  NavigatorProperties GetSpoofedNavigatorProperties(const std::string& container_id);

  struct ScreenProperties {
    int width;
    int height;
    int avail_width;
    int avail_height;
    int color_depth;
    double pixel_ratio;
    std::string orientation;
  };
  ScreenProperties GetSpoofedScreenProperties(const std::string& container_id);

  struct TimezoneInfo {
    std::string timezone_id;
    int offset_minutes;
    std::string display_name;
  };
  TimezoneInfo GetSpoofedTimezone(const std::string& container_id);

  struct GeolocationInfo {
    double latitude;
    double longitude;
    double accuracy;
    double altitude;
    double altitude_accuracy;
    double heading;
    double speed;
  };
  GeolocationInfo GetSpoofedGeolocation(const std::string& container_id);

  std::vector<std::string> GetAllowedFonts(const std::string& container_id);

  bool IsFontAllowed(const std::string& container_id,
                     const std::string& font_name);

  struct FontMetrics {
    double ascent;
    double descent;
    double height;
    double width;
  };
  FontMetrics GetSpoofedFontMetrics(const std::string& container_id,
                                    const std::string& font_name,
                                    const FontMetrics& original);

  void ReportFingerprintAttempt(const std::string& container_id,
                                const std::string& type,
                                const std::string& origin,
                                const std::string& details);

  struct FingerprintAttempt {
    base::TimeTicks timestamp;
    std::string container_id;
    std::string type;
    std::string origin;
    std::string details;
    bool was_blocked;
  };
  std::vector<FingerprintAttempt> GetFingerprintAttempts(
      const std::string& container_id,
      size_t count = 100) const;

  struct AttemptStatistics {
    size_t total_attempts = 0;
    size_t canvas_attempts = 0;
    size_t webgl_attempts = 0;
    size_t audio_attempts = 0;
    size_t font_attempts = 0;
    size_t navigator_attempts = 0;
    size_t blocked_attempts = 0;
    std::map<std::string, size_t> attempts_by_origin;
  };
  AttemptStatistics GetAttemptStatistics(const std::string& container_id) const;

  void SetDebugLoggingEnabled(bool enabled);

  std::string GetDiagnosticReport() const;

  void DumpStateToLog() const;

 private:

  struct ContainerFingerprintInfo {
    std::string container_id;
    ContainerFingerprintConfig config;
    std::string applied_profile_id;
    std::vector<FingerprintAttempt> attempts;
    AttemptStatistics stats;
    std::string canvas_noise_seed;
    base::TimeTicks last_activity;

    ContainerFingerprintInfo() = default;
  };

  int GenerateRandomInt(int min, int max, const std::string& seed);
  double GenerateRandomDouble(double min, double max, const std::string& seed);
  std::string GenerateRandomString(size_t length, const std::string& seed);

  void ApplyNoiseToData(std::vector<uint8_t>& data, double noise_factor,
                        const std::string& seed);
  void ApplyNoiseToAudio(std::vector<float>& data, double noise_factor,
                         const std::string& seed);

  void NotifyConfigUpdated(const std::string& container_id,
                           const ContainerFingerprintConfig& config);
  void NotifyProtectionTriggered(const std::string& container_id,
                                 const std::string& type,
                                 const std::string& details);

  void InitializeBuiltinProfiles();

  raw_ptr<content::BrowserContext> browser_context_;

  std::map<std::string, std::unique_ptr<ContainerFingerprintInfo>>
      container_fingerprints_;

  std::map<std::string, FingerprintProfile> profiles_;

  bool debug_logging_enabled_ = false;

  base::ObserverList<ContainerFingerprintObserver> observers_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ContainerFingerprintManager> weak_factory_{this};
};

}  

#endif  
