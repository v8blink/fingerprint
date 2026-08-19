# VisibleV8 — V8-side patchset (Chromium 152 / V8 15.2.114)

Engine-level VisibleV8 instrumentation ported into V8 15.2 (Windows-only build).
When applied and built, Chrome logs every browser/host API call and host-object
property access to `vv8-*.log` in the process working directory (run with
`--no-sandbox` so the renderer can write the files).

## Contents

| File | What |
|---|---|
| `visiblev8-152-v8.patch` | **The full patchset** — all 19 files (18 modified + 1 new) in one applyable patch. |
| `cdp-stealth.patch` | Separate CDP anti-detection patch — 1 file (`src/inspector/v8-runtime-agent-impl.cc`), 2 hunks; independent of VisibleV8. |
| `version.txt` | Target version + origin. |

Covered files (19): `BUILD.gn`; `src/builtins/{builtins-api,builtins-call-gen,builtins-function,builtins-global,builtins-reflect}.cc`, `src/builtins/reflect.tq`; `src/compiler/js-call-reducer.cc`; `src/ic/accessor-assembler.cc`; `src/init/v8.cc`; `src/interpreter/bytecode-generator.cc`; `src/objects/{lookup-inl.h,objects.cc,objects.h}`; `src/runtime/{runtime-compiler.cc,runtime-test.cc,runtime-utils.cc(new),runtime-utils.h,runtime.h}`.

## How to apply (to a fresh Chromium 152 / V8 15.2 tree)

Run from the **V8 repo root** (`<chromium>/src/v8`):

```bash
git apply --3way  /path/to/tanya/patches/v8/visiblev8-152-v8.patch
#  or, without git:
patch -p1 < /path/to/tanya/patches/v8/visiblev8-152-v8.patch
```

Verify:

```bash
git status   # expect the 18 modified files + untracked src/runtime/runtime-utils.cc
```

Then `gn gen out/default` + build (autoninja re-runs gn gen to pick up the new
`runtime-utils.cc`).

## Notes

- `BUILD.gn` sets `vv8_trace_properties = true` (default) → enables the property
  read/write tracing (`g`/`s` records) via the `VV8_TRACE_PROPERTIES` define.
- The **sandbox / Blink side is intentionally NOT included** — on Windows just
  launch chrome with `--no-sandbox`, which is equivalent to the desktop
  renderer-sandbox patch VisibleV8 uses.
- Timezone/Math fingerprint vectors (`Intl`/`Date`/`Math`) are V8-internal
  built-ins, not host APIs, so they do not appear as `c` API-call records — by
  design, not a gap in this patchset.
- Apply onto a clean V8 15.2 checkout; do not commit these into the V8 submodule.
