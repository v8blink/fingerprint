#include "third_party/blink/renderer/modules/webgl/tanya_webgl_capture_data.h"

#include <cctype>

namespace blink {

namespace {

const TanyaWebGLCaptureDevice kEmptyDevices[] = {};

std::string SlugifyDeviceModel(const std::string& model) {
  std::string out;
  out.reserve(model.size());
  bool prev_dash = true;
  for (char c : model) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      out.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      prev_dash = false;
    } else if (!prev_dash) {
      out.push_back('-');
      prev_dash = true;
    }
  }
  while (!out.empty() && out.back() == '-') {
    out.pop_back();
  }
  return out;
}

}  // namespace

const TanyaWebGLCaptureDevice* const kTanyaWebGLCaptureDevices = kEmptyDevices;
const size_t kTanyaWebGLCaptureDeviceCount = 0u;

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
    int32_t canvas_height) {
  if (kTanyaWebGLCaptureDeviceCount == 0u) {
    return nullptr;
  }
  const std::string slug = SlugifyDeviceModel(device_model);
  const TanyaWebGLCaptureDevice* device = nullptr;
  for (size_t i = 0; i != kTanyaWebGLCaptureDeviceCount; ++i) {
    if (slug == kTanyaWebGLCaptureDevices[i].device_slug) {
      device = &kTanyaWebGLCaptureDevices[i];
      break;
    }
  }
  if (!device) {
    return nullptr;
  }
  for (size_t i = 0; i < device->entry_count; ++i) {
    const TanyaWebGLCaptureEntry& e = device->entries[i];
    if (e.format != format || e.type != type) {
      continue;
    }
    if (e.width != width || e.height != height) {
      continue;
    }
    if (e.canvas_width != canvas_width || e.canvas_height != canvas_height) {
      continue;
    }
    if (e.x != x || e.y != y) {
      continue;
    }
    if (context_kind && e.kind && std::string(context_kind) != e.kind) {
      continue;
    }
    return &e;
  }
  return nullptr;
}

}  // namespace blink
