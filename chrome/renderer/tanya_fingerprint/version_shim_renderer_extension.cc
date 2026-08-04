#include "chrome/renderer/tanya_fingerprint/version_shim_renderer_extension.h"

#include <iterator>

#include "base/strings/string_number_conversions.h"
#include "components/fingerprint/fingerprint_policy.h"
#include "components/version_info/version_info.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "v8/include/v8-context.h"
#include "v8/include/v8-exception.h"
#include "v8/include/v8-function.h"
#include "v8/include/v8-isolate.h"
#include "v8/include/v8-local-handle.h"
#include "v8/include/v8-object.h"
#include "v8/include/v8-template.h"

namespace tanya_fingerprint {

namespace {

void IllegalConstructorCallback(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  isolate->ThrowException(v8::Exception::TypeError(
      v8::String::NewFromUtf8Literal(isolate, "Illegal constructor")));
}

v8::Local<v8::Object> GetGlobalCtorPrototype(v8::Isolate* isolate,
                                             v8::Local<v8::Context> ctx,
                                             const char* ctor_name) {
  v8::Local<v8::Object> global = ctx->Global();
  v8::Local<v8::String> key;
  if (!v8::String::NewFromUtf8(isolate, ctor_name).ToLocal(&key)) return {};
  v8::Local<v8::Value> ctor;
  if (!global->Get(ctx, key).ToLocal(&ctor) || !ctor->IsFunction()) return {};
  v8::Local<v8::String> proto_key =
      v8::String::NewFromUtf8Literal(isolate, "prototype");
  v8::Local<v8::Value> proto;
  if (!ctor.As<v8::Function>()->Get(ctx, proto_key).ToLocal(&proto) ||
      !proto->IsObject())
    return {};
  return proto.As<v8::Object>();
}

v8::Local<v8::Object> GetGlobalPlainObject(v8::Isolate* isolate,
                                           v8::Local<v8::Context> ctx,
                                           const char* name) {
  v8::Local<v8::Object> global = ctx->Global();
  v8::Local<v8::String> key;
  if (!v8::String::NewFromUtf8(isolate, name).ToLocal(&key)) return {};
  v8::Local<v8::Value> val;
  if (!global->Get(ctx, key).ToLocal(&val) || !val->IsObject()) return {};
  return val.As<v8::Object>();
}

void InstallNativeMethod(v8::Isolate* isolate,
                         v8::Local<v8::Context> ctx,
                         v8::Local<v8::Object> target,
                         const char* name_str,
                         v8::FunctionCallback callback) {
  v8::Local<v8::String> name;
  if (!v8::String::NewFromUtf8(isolate, name_str).ToLocal(&name)) return;
  v8::Maybe<bool> has = target->HasOwnProperty(ctx, name);
  if (has.IsJust() && has.FromJust()) return;
  v8::Local<v8::FunctionTemplate> tmpl =
      v8::FunctionTemplate::New(isolate, callback);
  tmpl->SetClassName(name);
  v8::Local<v8::Function> func;
  if (!tmpl->GetFunction(ctx).ToLocal(&func)) return;
  func->SetName(name);
  v8::PropertyDescriptor desc(func, true);
  desc.set_enumerable(false);
  desc.set_configurable(true);
  target->DefineProperty(ctx, name, desc).FromMaybe(false);
}

void NativeStubCallback(const v8::FunctionCallbackInfo<v8::Value>&) {}

void NativeMapGetOrInsert(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* iso = args.GetIsolate();
  v8::Local<v8::Context> ctx = iso->GetCurrentContext();
  v8::Local<v8::Object> self;
  if (!args.This()->ToObject(ctx).ToLocal(&self)) return;
  v8::Local<v8::String> hs = v8::String::NewFromUtf8Literal(iso, "has");
  v8::Local<v8::String> gs = v8::String::NewFromUtf8Literal(iso, "get");
  v8::Local<v8::String> ss = v8::String::NewFromUtf8Literal(iso, "set");
  v8::Local<v8::Value> key =
      args.Length() >= 1 ? args[0] : v8::Undefined(iso).As<v8::Value>();
  v8::Local<v8::Value> argv1[] = {key};
  v8::Local<v8::Value> has_fn, has_r, get_fn, val, set_fn;
  if (!self->Get(ctx, hs).ToLocal(&has_fn) || !has_fn->IsFunction()) return;
  if (!has_fn.As<v8::Function>()->Call(ctx, self, 1, argv1).ToLocal(&has_r))
    return;
  if (has_r->BooleanValue(iso)) {
    if (!self->Get(ctx, gs).ToLocal(&get_fn) || !get_fn->IsFunction()) return;
    if (get_fn.As<v8::Function>()->Call(ctx, self, 1, argv1).ToLocal(&val))
      args.GetReturnValue().Set(val);
    return;
  }
  v8::Local<v8::Value> def =
      args.Length() >= 2 ? args[1] : v8::Undefined(iso).As<v8::Value>();
  if (!self->Get(ctx, ss).ToLocal(&set_fn) || !set_fn->IsFunction()) return;
  v8::Local<v8::Value> sv[] = {key, def};
  set_fn.As<v8::Function>()->Call(ctx, self, 2, sv).IsEmpty();
  args.GetReturnValue().Set(def);
}

void NativeMapGetOrInsertComputed(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* iso = args.GetIsolate();
  v8::Local<v8::Context> ctx = iso->GetCurrentContext();
  v8::Local<v8::Object> self;
  if (!args.This()->ToObject(ctx).ToLocal(&self)) return;
  if (args.Length() < 2 || !args[1]->IsFunction()) return;
  v8::Local<v8::String> hs = v8::String::NewFromUtf8Literal(iso, "has");
  v8::Local<v8::String> gs = v8::String::NewFromUtf8Literal(iso, "get");
  v8::Local<v8::String> ss = v8::String::NewFromUtf8Literal(iso, "set");
  v8::Local<v8::Value> key = args[0];
  v8::Local<v8::Value> argv1[] = {key};
  v8::Local<v8::Value> has_fn, has_r, get_fn, set_fn, computed;
  if (!self->Get(ctx, hs).ToLocal(&has_fn) || !has_fn->IsFunction()) return;
  if (!has_fn.As<v8::Function>()->Call(ctx, self, 1, argv1).ToLocal(&has_r))
    return;
  if (has_r->BooleanValue(iso)) {
    if (!self->Get(ctx, gs).ToLocal(&get_fn) || !get_fn->IsFunction()) return;
    v8::Local<v8::Value> val;
    if (get_fn.As<v8::Function>()->Call(ctx, self, 1, argv1).ToLocal(&val))
      args.GetReturnValue().Set(val);
    return;
  }
  v8::Local<v8::Value> undef = v8::Undefined(iso);
  if (!args[1].As<v8::Function>()->Call(ctx, undef, 1, argv1).ToLocal(&computed))
    return;
  if (!self->Get(ctx, ss).ToLocal(&set_fn) || !set_fn->IsFunction()) return;
  v8::Local<v8::Value> sv[] = {key, computed};
  set_fn.As<v8::Function>()->Call(ctx, self, 2, sv).IsEmpty();
  args.GetReturnValue().Set(computed);
}

void NativeMathSumPrecise(const v8::FunctionCallbackInfo<v8::Value>& args) {
  args.GetReturnValue().Set(v8::Number::New(args.GetIsolate(), 0.0));
}

void InstallChrome148Stubs(v8::Isolate* isolate, v8::Local<v8::Context> ctx) {
  if (auto p = GetGlobalCtorPrototype(isolate, ctx, "Map"); !p.IsEmpty()) {
    InstallNativeMethod(isolate, ctx, p, "getOrInsert", NativeMapGetOrInsert);
    InstallNativeMethod(isolate, ctx, p, "getOrInsertComputed",
                        NativeMapGetOrInsertComputed);
  }
  if (auto p = GetGlobalCtorPrototype(isolate, ctx, "WeakMap"); !p.IsEmpty()) {
    InstallNativeMethod(isolate, ctx, p, "getOrInsert", NativeMapGetOrInsert);
    InstallNativeMethod(isolate, ctx, p, "getOrInsertComputed",
                        NativeMapGetOrInsertComputed);
  }
  if (auto m = GetGlobalPlainObject(isolate, ctx, "Math"); !m.IsEmpty())
    InstallNativeMethod(isolate, ctx, m, "sumPrecise", NativeMathSumPrecise);
  {
    v8::Local<v8::String> doc_key;
    if (v8::String::NewFromUtf8(isolate, "Document").ToLocal(&doc_key)) {
      v8::Local<v8::Value> doc_ctor;
      if (ctx->Global()->Get(ctx, doc_key).ToLocal(&doc_ctor) &&
          doc_ctor->IsFunction()) {
        InstallNativeMethod(isolate, ctx, doc_ctor.As<v8::Function>(),
                            "parseHTML", NativeStubCallback);
      }
    }
  }
  if (auto p = GetGlobalCtorPrototype(isolate, ctx, "Element"); !p.IsEmpty()) {
    InstallNativeMethod(isolate, ctx, p, "setHTML", NativeStubCallback);
    InstallNativeMethod(isolate, ctx, p, "startViewTransition",
                        NativeStubCallback);
  }
}

void InstallChrome149ExtraStubs(v8::Isolate* isolate,
                                v8::Local<v8::Context> ctx) {
  if (auto p = GetGlobalCtorPrototype(isolate, ctx, "Element"); !p.IsEmpty())
    InstallNativeMethod(isolate, ctx, p, "pseudo", NativeStubCallback);
}

constexpr const char* kChrome149Constructors[] = {
    "AnimationTrigger",           "AudioPlaybackStats",
    "CSSPseudoElement",           "CrashReportContext",
    "LanguageModel",              "Origin",
    "PerformanceTimingConfidence", "Sanitizer",
    "TimelineTrigger",            "TimelineTriggerRange",
    "TimelineTriggerRangeList",   "XRCompositionLayer",
    "XRCubeLayer",                "XRCylinderLayer",
    "XREquirectLayer",            "XRLayerEvent",
    "XRPlane",                    "XRPlaneSet",
    "XRProjectionLayer",          "XRQuadLayer",
    "XRSubImage",                 "XRWebGLSubImage",
};

void InstallNativeConstructorStubs(v8::Isolate* isolate,
                                   v8::Local<v8::Context> v8_context,
                                   const char* const* names,
                                   size_t name_count) {
  v8::Local<v8::Object> global = v8_context->Global();
  for (size_t i = 0; i < name_count; ++i) {
    v8::Local<v8::String> v8_name;
    if (!v8::String::NewFromUtf8(isolate, names[i]).ToLocal(&v8_name)) {
      continue;
    }
    v8::Maybe<bool> has = global->HasOwnProperty(v8_context, v8_name);
    if (has.IsJust() && has.FromJust()) continue;

    v8::Local<v8::FunctionTemplate> tmpl =
        v8::FunctionTemplate::New(isolate, IllegalConstructorCallback);
    tmpl->SetClassName(v8_name);
    v8::Local<v8::Function> func;
    if (!tmpl->GetFunction(v8_context).ToLocal(&func)) continue;
    func->SetName(v8_name);

    v8::PropertyDescriptor desc(func, true);
    desc.set_enumerable(false);
    desc.set_configurable(true);
    global->DefineProperty(v8_context, v8_name, desc).Check();
  }
}

constexpr char kShimPayload[] = R"JS(
(function() {
  'use strict';
  try {
    var ua = (typeof navigator !== 'undefined' && navigator.userAgent) || '';
    var m = /Chrome\/(\d+)/.exec(ua);
    if (!m) return;
    var claimed = parseInt(m[1], 10);
    if (!(claimed > 0)) return;
    function add(obj, name, val) {
      try {
        if (!obj) return;
        if (Object.prototype.hasOwnProperty.call(obj, name)) return;
        Object.defineProperty(obj, name, {
          value: val,
          writable: true,
          configurable: true,
          enumerable: false,
        });
      } catch (e) {}
    }
    if (claimed >= 148) {
      try {
        if (typeof Document !== 'undefined' && Document.prototype) {
          add(Document.prototype, 'customElementRegistry', null);
          add(Document.prototype, 'onanimationcancel', null);
        }
        if (typeof Element !== 'undefined' && Element.prototype) {
          add(Element.prototype, 'customElementRegistry', null);
          add(Element.prototype, 'activeViewTransition', null);
        }
      } catch (e) {}
    }
    if (claimed >= 149) {
      add(globalThis, 'crashReport', null);
      add(globalThis, 'onanimationcancel', null);
      try {
        if (typeof HTMLElement !== 'undefined' && HTMLElement.prototype) {
          add(HTMLElement.prototype, 'onanimationcancel', null);
        }
      } catch (e) {}
    }
  } catch (e) {}
})();
)JS";

bool IsMainFrameWithDocument(content::RenderFrame* frame) {
  if (!frame) return false;
  blink::WebLocalFrame* web_frame = frame->GetWebFrame();
  if (!web_frame) return false;
  blink::WebDocument doc = web_frame->GetDocument();
  return !doc.IsNull();
}

}  // namespace

// static
void VersionShimRendererExtension::Create(content::RenderFrame* frame) {
  new VersionShimRendererExtension(frame);
}

VersionShimRendererExtension::VersionShimRendererExtension(
    content::RenderFrame* frame)
    : content::RenderFrameObserver(frame) {}

VersionShimRendererExtension::~VersionShimRendererExtension() = default;

void VersionShimRendererExtension::OnDestruct() {
  delete this;
}

void VersionShimRendererExtension::DidCreateScriptContext(
    v8::Local<v8::Context> v8_context,
    int32_t world_id) {
  if (world_id != content::ISOLATED_WORLD_ID_GLOBAL) return;
  if (!IsMainFrameWithDocument(render_frame())) return;

  const fingerprint::FingerprintPolicy& policy =
      fingerprint::FingerprintPolicy::ProcessDefault();
  int claimed_major = 0;
  if (policy.enabled()) {
    const std::string bv = policy.brand_version().empty()
                               ? std::string(version_info::GetVersionNumber())
                               : policy.brand_version();
    size_t dot = bv.find('.');
    std::string maj = (dot == std::string::npos) ? bv : bv.substr(0, dot);
    int parsed = 0;
    if (base::StringToInt(maj, &parsed) && parsed > 0) {
      claimed_major = parsed;
    }
  }

  if (claimed_major >= 148) {
    blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
    if (frame) {
      v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
      v8::HandleScope handle_scope(isolate);
      v8::Context::Scope context_scope(v8_context);
      InstallChrome148Stubs(isolate, v8_context);
    }
  }
  if (claimed_major >= 149) {
    blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
    if (frame) {
      v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
      v8::HandleScope handle_scope(isolate);
      v8::Context::Scope context_scope(v8_context);
      InstallNativeConstructorStubs(isolate, v8_context, kChrome149Constructors,
                                    std::size(kChrome149Constructors));
      InstallChrome149ExtraStubs(isolate, v8_context);
    }
  }

  RunShim(v8_context);
}

void VersionShimRendererExtension::RunShim(v8::Local<v8::Context> v8_context) {
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (!frame) return;
  v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(v8_context);
  blink::WebScriptSource source(blink::WebString::FromUtf8(kShimPayload));
  frame->ExecuteScript(source);
}

}  // namespace tanya_fingerprint
