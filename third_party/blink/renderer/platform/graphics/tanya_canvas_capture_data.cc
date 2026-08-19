#include "third_party/blink/renderer/platform/graphics/tanya_canvas_capture_data.h"

#include <cstdint>

#include "base/logging.h"
#include "base/values.h"
#include "components/fingerprint/fingerprint_policy.h"

namespace blink {

void TanyaReplaceCanvasPixels(void* pixels,
                              const SkImageInfo& info,
                              int x_offset,
                              int y_offset,
                              const String& key) {
  if (!pixels || key.empty()) {
    return;
  }
  const fingerprint::FingerprintPolicy& policy =
      fingerprint::FingerprintPolicy::ProcessDefault();
  if (!policy.SurfaceActive("canvas2d")) {
    return;
  }
  const base::ListValue* raw = policy.GetCanvas2dPixelsByKey(key.Utf8());
  if (!raw) {
    return;
  }
  const size_t byte_size = info.computeByteSize(info.minRowBytes());
  if (SkImageInfo::ByteSizeOverflowed(byte_size) || raw->size() != byte_size) {
    return;
  }
  const SkColorType ct = info.colorType();
  uint8_t* out = static_cast<uint8_t*>(pixels);
  if (ct == kRGBA_8888_SkColorType) {
    for (size_t i = 0; i < raw->size(); ++i) {
      out[i] = static_cast<uint8_t>((*raw)[i].GetIfInt().value_or(0) & 0xFF);
    }
  } else if (ct == kBGRA_8888_SkColorType) {
    for (size_t p = 0; p + 3 < raw->size(); p += 4) {
      const int r = (*raw)[p].GetIfInt().value_or(0);
      const int g = (*raw)[p + 1].GetIfInt().value_or(0);
      const int b = (*raw)[p + 2].GetIfInt().value_or(0);
      const int a = (*raw)[p + 3].GetIfInt().value_or(0);
      out[p] = static_cast<uint8_t>(b & 0xFF);
      out[p + 1] = static_cast<uint8_t>(g & 0xFF);
      out[p + 2] = static_cast<uint8_t>(r & 0xFF);
      out[p + 3] = static_cast<uint8_t>(a & 0xFF);
    }
  } else {
    return;
  }
}

}
