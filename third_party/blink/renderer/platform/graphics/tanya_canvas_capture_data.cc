#include "third_party/blink/renderer/platform/graphics/tanya_canvas_capture_data.h"

#include <cstdint>
#include <cstring>

namespace blink {

namespace {

struct TanyaCanvasCaptureEntry {
  const char* device_slug;
  int width;
  int height;
  int x;
  int y;
  const uint8_t* pixels;
  size_t byte_count;
};

const TanyaCanvasCaptureEntry kTanyaCanvasCaptures[] = {};
const size_t kTanyaCanvasCaptureCount = 0u;

const TanyaCanvasCaptureEntry* FindTanyaCanvasCapture(const String& device_model,
                                                      int width,
                                                      int height,
                                                      int x,
                                                      int y) {
  if (kTanyaCanvasCaptureCount == 0u) {
    return nullptr;
  }
  for (size_t i = 0; i != kTanyaCanvasCaptureCount; ++i) {
    const TanyaCanvasCaptureEntry& e = kTanyaCanvasCaptures[i];
    if (e.width != width || e.height != height || e.x != x || e.y != y) {
      continue;
    }
    if (device_model != e.device_slug) {
      continue;
    }
    return &e;
  }
  return nullptr;
}

}  // namespace

void TanyaReplaceCanvasPixels(void* pixels,
                              const SkImageInfo& info,
                              int x_offset,
                              int y_offset,
                              const String& device_model) {
  if (!pixels || device_model.empty()) {
    return;
  }
  const TanyaCanvasCaptureEntry* entry = FindTanyaCanvasCapture(
      device_model, info.width(), info.height(), x_offset, y_offset);
  if (!entry) {
    return;
  }
  const size_t byte_size = info.computeByteSize(info.minRowBytes());
  if (SkImageInfo::ByteSizeOverflowed(byte_size) ||
      entry->byte_count != byte_size) {
    return;
  }
  std::memcpy(pixels, entry->pixels, byte_size);
}

}  // namespace blink
