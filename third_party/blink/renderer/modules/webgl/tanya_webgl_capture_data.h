#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGL_TANYA_WEBGL_CAPTURE_DATA_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGL_TANYA_WEBGL_CAPTURE_DATA_H_

#include <stddef.h>
#include <stdint.h>

#include <string>

namespace blink {

struct TanyaWebGLCaptureEntry {
  const char* kind;
  uint32_t format;
  uint32_t type;
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
  int32_t canvas_width;
  int32_t canvas_height;
  const uint8_t* pixels;
  size_t pixels_size;
};

struct TanyaWebGLCaptureDevice {
  const char* device_slug;
  const TanyaWebGLCaptureEntry* entries;
  size_t entry_count;
};

extern const TanyaWebGLCaptureDevice* const kTanyaWebGLCaptureDevices;
extern const size_t kTanyaWebGLCaptureDeviceCount;

const TanyaWebGLCaptureEntry* FindTanyaWebGLCapture(
    const std::string& device_model,
    const char* context_kind,
    uint32_t format,
    uint32_t type,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    int32_t canvas_width,
    int32_t canvas_height);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGL_TANYA_WEBGL_CAPTURE_DATA_H_
