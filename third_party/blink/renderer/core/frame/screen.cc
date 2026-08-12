/*
 * Copyright (C) 2007 Apple Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/core/frame/screen.h"

#include <optional>

#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "components/fingerprint/fingerprint_policy.h"
#include "services/network/public/mojom/permissions_policy/permissions_policy_feature.mojom-blink.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/event_target_names.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "ui/display/screen_info.h"
#include "ui/display/screen_infos.h"

namespace blink {

Screen::Screen(LocalDOMWindow* window, int64_t display_id)
    : ExecutionContextClient(window), display_id_(display_id) {}

// static
bool Screen::AreWebExposedScreenPropertiesEqual(
    const display::ScreenInfo& prev,
    const display::ScreenInfo& current) {
  // height() and width() use rect.size()
  if (prev.rect.size() != current.rect.size()) {
    return false;
  }

  // height() and width() use device_scale_factor
  // Note: comparing device_scale_factor is a bit of a lie as Screen only uses
  // this with the PhysicalPixelsQuirk (see width() / height() below).  However,
  // this value likely changes rarely and should not throw many false positives.
  if (prev.device_scale_factor != current.device_scale_factor) {
    return false;
  }

  // avail[Left|Top|Width|Height](), height() and width() use
  // text_scale_multiplier Note: similar to device_scale_factor,
  // text_scale_multiplier is only used in select scenarios, but is unlikely to
  // change and should not result in many false positives.
  if (prev.text_scale_multiplier != current.text_scale_multiplier) {
    return false;
  }

  // avail[Left|Top|Width|Height]() use available_rect
  if (prev.available_rect != current.available_rect) {
    return false;
  }

  // colorDepth() and pixelDepth() use depth
  if (prev.depth != current.depth) {
    return false;
  }

  // isExtended()
  if (prev.is_extended != current.is_extended) {
    return false;
  }

  return true;
}

namespace {

// Tanya810 Phase A: "has -> inject user value, absent -> real value".
// Reads surfaces["screen"][key] from the process-default fingerprint policy
// (installed from the BigBuffer profile). Accepts int or numeric-string values.
std::optional<int> TanyaScreenOverride(const char* key) {
  const fingerprint::FingerprintPolicy& policy =
      fingerprint::FingerprintPolicy::ProcessDefault();
  if (!policy.enabled() || policy.IsSurfaceDisabled("screen")) {
    return std::nullopt;
  }
  const base::Value* v = policy.SurfaceValue("screen", key);
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

}  // namespace

int Screen::height() const {
  if (std::optional<int> ov = TanyaScreenOverride("height")) {
    VLOG(1) << "Tanya810 [screen] height mode=INJECT val=" << *ov;
    return *ov;
  }
  if (!DomWindow())
    return 0;
  VLOG(1) << "Tanya810 [screen] height mode=REAL_FALLBACK";
  return GetRect(/*available=*/false).height();
}

int Screen::width() const {
  if (std::optional<int> ov = TanyaScreenOverride("width")) {
    VLOG(1) << "Tanya810 [screen] width mode=INJECT val=" << *ov;
    return *ov;
  }
  if (!DomWindow())
    return 0;
  VLOG(1) << "Tanya810 [screen] width mode=REAL_FALLBACK";
  return GetRect(/*available=*/false).width();
}

unsigned Screen::colorDepth() const {
  if (std::optional<int> ov = TanyaScreenOverride("colorDepth")) {
    VLOG(1) << "Tanya810 [screen] colorDepth mode=INJECT val=" << *ov;
    return base::saturated_cast<unsigned>(*ov);
  }
  // "If the user agent does not know the color depth or does not want to
  // return it for privacy considerations, it should return 24."
  //
  // https://drafts.csswg.org/cssom-view/#dom-screen-colordepth
  unsigned unknown_color_depth = 24u;

  if (!DomWindow()) {
    return unknown_color_depth;
  }
  return GetScreenInfo().depth == 0
             ? unknown_color_depth
             : base::saturated_cast<unsigned>(GetScreenInfo().depth);
}

unsigned Screen::pixelDepth() const {
  if (std::optional<int> ov = TanyaScreenOverride("pixelDepth")) {
    VLOG(1) << "Tanya810 [screen] pixelDepth mode=INJECT val=" << *ov;
    return base::saturated_cast<unsigned>(*ov);
  }
  return colorDepth();
}

int Screen::availLeft() const {
  if (!DomWindow())
    return 0;
  return GetRect(/*available=*/true).x();
}

int Screen::availTop() const {
  if (!DomWindow())
    return 0;
  return GetRect(/*available=*/true).y();
}

int Screen::availHeight() const {
  if (std::optional<int> ov = TanyaScreenOverride("availHeight")) {
    VLOG(1) << "Tanya810 [screen] availHeight mode=INJECT val=" << *ov;
    return *ov;
  }
  if (!DomWindow())
    return 0;
  VLOG(1) << "Tanya810 [screen] availHeight mode=REAL_FALLBACK";
  return GetRect(/*available=*/true).height();
}

int Screen::availWidth() const {
  if (std::optional<int> ov = TanyaScreenOverride("availWidth")) {
    VLOG(1) << "Tanya810 [screen] availWidth mode=INJECT val=" << *ov;
    return *ov;
  }
  if (!DomWindow())
    return 0;
  VLOG(1) << "Tanya810 [screen] availWidth mode=REAL_FALLBACK";
  return GetRect(/*available=*/true).width();
}

void Screen::Trace(Visitor* visitor) const {
  EventTarget::Trace(visitor);
  ExecutionContextClient::Trace(visitor);
  Supplementable<Screen>::Trace(visitor);
}

const AtomicString& Screen::InterfaceName() const {
  return event_target_names::kScreen;
}

ExecutionContext* Screen::GetExecutionContext() const {
  return ExecutionContextClient::GetExecutionContext();
}

bool Screen::isExtended() const {
  if (!DomWindow()) {
    return false;
  }
  auto* context = GetExecutionContext();
  if (!context->IsFeatureEnabled(
          network::mojom::PermissionsPolicyFeature::kWindowManagement)) {
    return false;
  }

  return GetScreenInfo().is_extended;
}

gfx::Rect Screen::GetRect(bool available) const {
  if (!DomWindow())
    return gfx::Rect();
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo();
  gfx::Rect rect = available ? screen_info.available_rect : screen_info.rect;
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk())
    return gfx::ScaleToRoundedRect(rect, screen_info.device_scale_factor);
  if (frame->GetDocument() && frame->GetDocument()->TextScaleMetaTagPresent()) {
    return gfx::ScaleToRoundedRect(rect, screen_info.text_scale_multiplier);
  }
  return rect;
}

const display::ScreenInfo& Screen::GetScreenInfo() const {
  DCHECK(DomWindow());
  LocalFrame* frame = DomWindow()->GetFrame();

  const auto& screen_infos = frame->GetChromeClient().GetScreenInfos(*frame);
  for (const auto& screen : screen_infos.screen_infos) {
    if (screen.display_id == display_id_)
      return screen;
  }
  DEFINE_STATIC_LOCAL(display::ScreenInfo, kEmptyScreenInfo, ());
  return kEmptyScreenInfo;
}

}  // namespace blink
