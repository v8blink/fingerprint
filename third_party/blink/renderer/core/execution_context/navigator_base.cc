// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/execution_context/navigator_base.h"

#include "base/feature_list.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "build/build_config.h"
#include "components/fingerprint/fingerprint_policy.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/navigator_concurrent_hardware.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

#if !BUILDFLAG(IS_MAC) && !BUILDFLAG(IS_WIN)
#include <sys/utsname.h>
#include "third_party/blink/renderer/platform/wtf/thread_specific.h"
#include "third_party/blink/renderer/platform/wtf/threading.h"
#endif

namespace blink {

namespace {

String GetReducedNavigatorPlatform(const std::string& fp_platform) {
  if (fp_platform == "windows") {
    return "Win32";
  } else if (fp_platform == "macos") {
    return "MacIntel";
  } else if (fp_platform == "linux") {
    return "Linux x86_64";
  } else if (fp_platform == "android") {
    return "Linux armv8l";
  } else if (fp_platform == "ios") {
    return "iPhone";
  }
#if BUILDFLAG(IS_ANDROID)
  return "Linux armv81";
#elif BUILDFLAG(IS_MAC)
  return "MacIntel";
#elif BUILDFLAG(IS_WIN)
  return "Win32";
#elif BUILDFLAG(IS_FUCHSIA)
  return "";
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  return "Linux x86_64";
#elif BUILDFLAG(IS_IOS)
  return "iPhone";
#else
#error Unsupported platform
#endif
}

const fingerprint::FingerprintPolicy& ResolveFingerprintPolicy(
    ExecutionContext* ec) {
  if (auto* win = DynamicTo<LocalDOMWindow>(ec)) {
    if (auto* doc = win->document()) {
      return doc->GetFingerprintPolicy();
    }
  }
  return fingerprint::FingerprintPolicy::ProcessDefault();
}

}  // namespace

NavigatorBase::NavigatorBase(ExecutionContext* context)
    : NavigatorLanguage(context), ExecutionContextClient(context) {}

String NavigatorBase::userAgent() const {
  ExecutionContext* execution_context = GetExecutionContext();
  return execution_context ? execution_context->UserAgent() : String();
}

String NavigatorBase::platform() const {
  const fingerprint::FingerprintPolicy& policy =
      ResolveFingerprintPolicy(GetExecutionContext());
  std::string fp_platform = policy.platform();
#if BUILDFLAG(IS_ANDROID)
  // We need to check the ReduceUserAgentMinorVersion feature flag for
  // Android WebView, which does not currently ship a reduced User-Agent.
  if (!RuntimeEnabledFeatures::ReduceUserAgentMinorVersionEnabled()) {
    return NavigatorID::platform();
  }
#endif
  return GetReducedNavigatorPlatform(fp_platform);
}

void NavigatorBase::Trace(Visitor* visitor) const {
  ScriptWrappable::Trace(visitor);
  NavigatorLanguage::Trace(visitor);
  ExecutionContextClient::Trace(visitor);
  Supplementable<NavigatorBase>::Trace(visitor);
}

unsigned int NavigatorBase::hardwareConcurrency() const {
  unsigned int hardware_concurrency =
      NavigatorConcurrentHardware::hardwareConcurrency();

  const fingerprint::FingerprintPolicy& policy =
      ResolveFingerprintPolicy(GetExecutionContext());
  if (!policy.hardware_concurrency().empty()) {
    int parsed = 0;
    if (base::StringToInt(policy.hardware_concurrency(), &parsed) &&
        parsed > 0) {
      hardware_concurrency = static_cast<unsigned int>(parsed);
    }
  }

  probe::ApplyHardwareConcurrencyOverride(
      probe::ToCoreProbeSink(GetExecutionContext()), hardware_concurrency);
  return hardware_concurrency;
}

float NavigatorBase::deviceMemory() const {
  const fingerprint::FingerprintPolicy& policy =
      ResolveFingerprintPolicy(GetExecutionContext());
  if (!policy.device_memory().empty()) {
    double parsed = 0.0;
    if (base::StringToDouble(policy.device_memory(), &parsed) && parsed > 0) {
      return static_cast<float>(parsed);
    }
  }
  if (policy.platform() == "android" || policy.platform() == "ios") {
    return 8.0f;
  }
  return NavigatorDeviceMemory::deviceMemory();
}

ExecutionContext* NavigatorBase::GetUAExecutionContext() const {
  return GetExecutionContext();
}

UserAgentMetadata NavigatorBase::GetUserAgentMetadata() const {
  ExecutionContext* execution_context = GetExecutionContext();
  return execution_context ? execution_context->GetUserAgentMetadata()
                           : blink::UserAgentMetadata();
}

}  // namespace blink
