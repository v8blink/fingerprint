#ifndef CHROME_RENDERER_TANYA_FINGERPRINT_VERSION_SHIM_RENDERER_EXTENSION_H_
#define CHROME_RENDERER_TANYA_FINGERPRINT_VERSION_SHIM_RENDERER_EXTENSION_H_

#include "content/public/renderer/render_frame_observer.h"
#include "v8/include/v8-forward.h"

namespace tanya_fingerprint {

class VersionShimRendererExtension : public content::RenderFrameObserver {
 public:
  static void Create(content::RenderFrame* frame);

  VersionShimRendererExtension(const VersionShimRendererExtension&) = delete;
  VersionShimRendererExtension& operator=(
      const VersionShimRendererExtension&) = delete;

 private:
  explicit VersionShimRendererExtension(content::RenderFrame* frame);
  ~VersionShimRendererExtension() override;

  void DidCreateScriptContext(v8::Local<v8::Context> v8_context,
                              int32_t world_id) override;
  void OnDestruct() override;

  void RunShim(v8::Local<v8::Context> v8_context);
};

}

#endif
