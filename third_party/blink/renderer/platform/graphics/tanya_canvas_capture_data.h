#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_TANYA_CANVAS_CAPTURE_DATA_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_TANYA_CANVAS_CAPTURE_DATA_H_

#include "third_party/blink/renderer/platform/platform_export.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/skia/include/core/SkImageInfo.h"

namespace blink {

PLATFORM_EXPORT void TanyaReplaceCanvasPixels(void* pixels,
                                              const SkImageInfo& info,
                                              int x_offset,
                                              int y_offset,
                                              const String& key);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_TANYA_CANVAS_CAPTURE_DATA_H_
