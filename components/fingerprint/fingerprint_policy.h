
#ifndef COMPONENTS_FINGERPRINT_FINGERPRINT_POLICY_H_
#define COMPONENTS_FINGERPRINT_FINGERPRINT_POLICY_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/component_export.h"
#include "base/containers/span.h"
#include "base/values.h"

namespace fingerprint {

class COMPONENT_EXPORT(FINGERPRINT) FingerprintPolicy {
 public:
  FingerprintPolicy();
  FingerprintPolicy(const FingerprintPolicy&);
  FingerprintPolicy(FingerprintPolicy&&) noexcept;
  FingerprintPolicy& operator=(const FingerprintPolicy&);
  FingerprintPolicy& operator=(FingerprintPolicy&&) noexcept;
  ~FingerprintPolicy();

  static FingerprintPolicy FromCommandLine();

  static const FingerprintPolicy& ProcessDefault();

  static void SetProcessDefaultForRenderer(FingerprintPolicy policy);

  // Phase A: parse a full structured profile (JSON bytes delivered via mojo
  // BigBuffer) into a policy. Fills the flat identity scalars from the
  // top-level fields and stores the "surfaces" block for structured access.
  static FingerprintPolicy FromProfileJson(base::span<const uint8_t> bytes);

  bool enabled() const { return enabled_; }

  bool IsSurfaceDisabled(std::string_view surface) const;

  // Structured "surfaces" access (populated by FromProfileJson). Used by the
  // per-surface consumers ("has -> inject user value, absent -> real value").
  bool has_surface(std::string_view name) const;
  const base::DictValue* surface(std::string_view name) const;
  // Returns surfaces[surface_name][key] or nullptr when absent.
  const base::Value* SurfaceValue(std::string_view surface_name,
                                  std::string_view key) const;

  // Phase C C-0 typed helpers over SurfaceValue(). Each returns nullopt/nullptr
  // when the field is absent or of the wrong type.
  bool SurfaceActive(std::string_view surface_name) const;
  std::optional<std::string> GetSurfaceString(std::string_view surface_name,
                                              std::string_view key) const;
  std::optional<int> GetSurfaceInt(std::string_view surface_name,
                                   std::string_view key) const;
  std::optional<double> GetSurfaceDouble(std::string_view surface_name,
                                         std::string_view key) const;
  const base::ListValue* GetSurfaceList(std::string_view surface_name,
                                          std::string_view key) const;
  const base::DictValue* GetSurfaceDict(std::string_view surface_name,
                                          std::string_view key) const;
  // Canvas2D content-addressed replay: surfaces.canvas2d.pixelsByKey[key].
  const base::ListValue* GetCanvas2dPixelsByKey(std::string_view key) const;

  const std::string& platform() const { return platform_; }
  const std::string& platform_version() const { return platform_version_; }
  const std::string& brand() const { return brand_; }
  const std::string& brand_version() const { return brand_version_; }
  const std::string& gpu_vendor() const { return gpu_vendor_; }
  const std::string& gpu_renderer() const { return gpu_renderer_; }
  const std::string& hardware_concurrency() const {
    return hardware_concurrency_;
  }
  const std::string& timezone() const { return timezone_; }
  const std::string& screen() const { return screen_; }
  const std::string& languages() const { return languages_; }
  const std::vector<std::string>& disabled_surfaces() const {
    return disabled_surfaces_;
  }

  const std::string& webrtc_public_ip() const { return webrtc_public_ip_; }

  const std::string& canvas_font_hinting() const {
    return canvas_font_hinting_;
  }
  const std::string& canvas_font_edging() const {
    return canvas_font_edging_;
  }

  const std::string& prefers_color_scheme() const {
    return prefers_color_scheme_;
  }
  const std::string& prefers_reduced_motion() const {
    return prefers_reduced_motion_;
  }
  const std::string& prefers_reduced_transparency() const {
    return prefers_reduced_transparency_;
  }

  bool stealth() const { return stealth_; }

  bool anti_bot_bypass() const { return anti_bot_bypass_; }

  const std::string& audio_sample_rate() const { return audio_sample_rate_; }
  const std::string& audio_max_channels() const { return audio_max_channels_; }
  const std::string& audio_base_latency() const { return audio_base_latency_; }
  const std::string& audio_output_latency() const {
    return audio_output_latency_;
  }

  const std::vector<std::string>& webgl_extensions() const {
    return webgl_extensions_;
  }

  const std::string& device_model() const { return device_model_; }

  const std::string& device_memory() const { return device_memory_; }

  bool is_mobile() const {
    return platform_ == "android" || platform_ == "Android" ||
           platform_ == "ios" || platform_ == "iOS";
  }

  void set_enabled(bool v) { enabled_ = v; }
  void set_platform(std::string v) { platform_ = std::move(v); }
  void set_platform_version(std::string v) {
    platform_version_ = std::move(v);
  }
  void set_brand(std::string v) { brand_ = std::move(v); }
  void set_brand_version(std::string v) { brand_version_ = std::move(v); }
  void set_gpu_vendor(std::string v) { gpu_vendor_ = std::move(v); }
  void set_gpu_renderer(std::string v) { gpu_renderer_ = std::move(v); }
  void set_hardware_concurrency(std::string v) {
    hardware_concurrency_ = std::move(v);
  }
  void set_timezone(std::string v) { timezone_ = std::move(v); }
  void set_screen(std::string v) { screen_ = std::move(v); }
  void set_languages(std::string v) { languages_ = std::move(v); }
  void set_disabled_surfaces(std::vector<std::string> v) {
    disabled_surfaces_ = std::move(v);
  }
  void set_webrtc_public_ip(std::string v) {
    webrtc_public_ip_ = std::move(v);
  }
  void set_canvas_font_hinting(std::string v) {
    canvas_font_hinting_ = std::move(v);
  }
  void set_canvas_font_edging(std::string v) {
    canvas_font_edging_ = std::move(v);
  }
  void set_prefers_color_scheme(std::string v) {
    prefers_color_scheme_ = std::move(v);
  }
  void set_prefers_reduced_motion(std::string v) {
    prefers_reduced_motion_ = std::move(v);
  }
  void set_prefers_reduced_transparency(std::string v) {
    prefers_reduced_transparency_ = std::move(v);
  }
  void set_stealth(bool v) { stealth_ = v; }
  void set_anti_bot_bypass(bool v) { anti_bot_bypass_ = v; }
  void set_audio_sample_rate(std::string v) {
    audio_sample_rate_ = std::move(v);
  }
  void set_audio_max_channels(std::string v) {
    audio_max_channels_ = std::move(v);
  }
  void set_audio_base_latency(std::string v) {
    audio_base_latency_ = std::move(v);
  }
  void set_audio_output_latency(std::string v) {
    audio_output_latency_ = std::move(v);
  }
  void set_webgl_extensions(std::vector<std::string> v) {
    webgl_extensions_ = std::move(v);
  }
  void set_device_model(std::string v) { device_model_ = std::move(v); }
  void set_device_memory(std::string v) { device_memory_ = std::move(v); }

 private:
  bool enabled_ = false;
  std::string platform_;
  std::string platform_version_;
  std::string brand_;
  std::string brand_version_;
  std::string gpu_vendor_;
  std::string gpu_renderer_;
  std::string hardware_concurrency_;
  std::string timezone_;
  std::string screen_;
  std::string languages_;
  std::vector<std::string> disabled_surfaces_;
  std::string webrtc_public_ip_;
  std::string canvas_font_hinting_;
  std::string canvas_font_edging_;
  std::string prefers_color_scheme_;
  std::string prefers_reduced_motion_;
  std::string prefers_reduced_transparency_;

  bool stealth_ = true;
  std::string audio_sample_rate_;
  std::string audio_max_channels_;
  std::string audio_base_latency_;
  std::string audio_output_latency_;
  std::vector<std::string> webgl_extensions_;
  std::string device_model_;
  std::string device_memory_;

  bool anti_bot_bypass_ = false;

  // Structured surfaces block (immutable after parse). Held via shared_ptr so
  // the defaulted copy/move constructors stay valid and copies stay cheap.
  std::shared_ptr<const base::DictValue> surfaces_;
};

}

#endif  
