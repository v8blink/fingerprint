# User Manual: Tanya Fingerprint Browser + VisibleV8

## Contents

1. Prerequisites
2. Fingerprinting: using the Settings page
3. Fingerprinting: using the command line
4. `fingerprint.json` format
5. VisibleV8

---

## 1. Prerequisites

| What you need | Notes |
|---|---|
| Build output | `out\default\chrome.exe` |
| `fingerprint.json` | Place it in **the same directory as `chrome.exe`** (the code looks it up via `DIR_EXE`; the file name is fixed) |
| Applied patches | VisibleV8 / CDP stealth → `src/v8`; WebRTC → `src/third_party/webrtc` |

The repository contains **no** fingerprint capture tool and no sample `fingerprint.json`. The file content has to be captured on a real device with an external tool; its structure matches the output of CreepJS (see section 4).

---

## 2. Fingerprinting: using the Settings page (recommended)

This is the only way to use a **complete fingerprint** (including `surfaces` such as Canvas, WebGL, audio and fonts).

### Steps

1. Put `fingerprint.json` in the same directory as `chrome.exe`.
2. Start the browser. A separate user data directory per fingerprint is recommended:
   ```bat
   chrome.exe --user-data-dir=D:\profiles\fp0
   ```
3. Open `chrome://settings/fingerprint`, or click "Fingerprint" in the left-hand menu of the Settings page.
4. The page lists every profile in the file, showing its `name` field; if there is no `name`, it shows `Profile <index>`.
5. Click "New Tab" on the right of a row. The browser opens a new tab and applies that fingerprint to it.
6. Type the URL of the target site **in that new tab**.

### What happens when you click "New Tab"

| Step | Behaviour |
|---|---|
| 1 | The profile's 11 top-level fields, plus `--fingerprint=<index+1>`, are appended to the command line of the **browser process** |
| 2 | A new tab is opened (`chrome://newtab`) |
| 3 | The whole profile (JSON) is attached to that tab and sent to the renderer process via `PageBroadcast.UpdateTanyaFingerprintProfile` |
| 4 | The renderer process parses the JSON and replaces the fingerprint policy of the **whole process**; it also sets the locale from `surfaces.intl.locale` |
| 5 | A spoofed User-Agent and UA-CH are generated on the browser side and forced onto every navigation in that tab |
| 6 | The tab is reloaded (bypassing the cache) |

### Notes

- The file is re-read every time the Fingerprint page in Settings is opened, so after editing `fingerprint.json` you only need to refresh the Settings page; no restart is required.
- If the file does not exist, is not valid JSON, or its top level is not an array, the list is empty and no error is shown.

---

## 3. Fingerprinting: using the command line

⚠️ The command line can **only set the top-level fields**. There are no `surfaces`, so Canvas, WebGL pixels, audio, fonts, DOMRect and so on are not replaced. The main frame's `navigator.userAgent` is not changed either (browser-side UA spoofing is only done in the Settings page flow), while the UA inside Workers is rebuilt from the switches, so the two may disagree. This mode suits cases where you only want to change a few values.

### 3.1 Switches that actually work (12 in total, forwarded to renderer processes)

| Switch | Value format | Affects |
|---|---|---|
| `--fingerprint=<integer>` | Must be an integer, e.g. `1` | Master switch. The value itself is not used as a seed; the feature is enabled as long as it parses as an integer |
| `--fingerprint-platform` | `windows` / `macos` / `linux` / `android` / `ios` (lowercase) | `navigator.platform`, the Worker UA |
| `--fingerprint-platform-version` | e.g. `10.0.0` | UA-CH `platformVersion` (Worker) |
| `--fingerprint-brand` | `Chrome` / `Edge` / `Opera` / `Vivaldi` or any string | UA suffix and UA-CH brands (Worker) |
| `--fingerprint-brand-version` | e.g. `152.0.0.0` | Major version in the UA, the version feature shim, the `getComputedStyle` property list |
| `--fingerprint-device-model` | Any string | UA-CH `model` on mobile |
| `--fingerprint-gpu-vendor` | e.g. `Google Inc. (Intel)` | WebGL `UNMASKED_VENDOR_WEBGL` |
| `--fingerprint-gpu-renderer` | e.g. `ANGLE (Intel, ...)` | WebGL `UNMASKED_RENDERER_WEBGL`; also selects the built-in GPU parameter table |
| `--fingerprint-hardware-concurrency` | Positive integer | `navigator.hardwareConcurrency` |
| `--fingerprint-device-memory` | Positive number | `navigator.deviceMemory` |
| `--fingerprint-timezone` | IANA time zone name, e.g. `Asia/Shanghai` | Time zone of `Date` and `Intl` |
| `--fingerprint-languages` | Comma-separated, e.g. `zh-CN,zh` | `navigator.language(s)`; the first entry becomes the ICU default locale |

Example:

```bat
chrome.exe --user-data-dir=D:\profiles\p1 ^
  --fingerprint=1 ^
  --fingerprint-platform=windows ^
  --fingerprint-platform-version=10.0.0 ^
  --fingerprint-brand=Chrome ^
  --fingerprint-brand-version=152.0.0.0 ^
  --fingerprint-gpu-vendor="Google Inc. (Intel)" ^
  --fingerprint-gpu-renderer="ANGLE (Intel, Intel(R) Iris(R) Xe Graphics (0x00009A49) Direct3D11 vs_5_0 ps_5_0, D3D11)" ^
  --fingerprint-hardware-concurrency=8 ^
  --fingerprint-device-memory=16 ^
  --fingerprint-timezone=Asia/Shanghai ^
  --fingerprint-languages=zh-CN,zh
```

---

## 4. `fingerprint.json` format

The top level of the file is an **array**; each element is one profile (an object):

```json
[
  {
    "name": "Real FP #0 (Win Chrome 152)",
    "platform": "windows",
    "platform_version": "10.0.0",
    "brand": "Google Chrome",
    "brand_version": "152.0.0.0",
    "device_model": "real-win-chrome152",
    "gpu_vendor": "Google Inc. (Intel)",
    "gpu_renderer": "ANGLE (Intel, Intel(R) Iris(R) Xe Graphics (0x00009A49) Direct3D11 vs_5_0 ps_5_0, D3D11)",
    "hardware_concurrency": "8",
    "device_memory": "16",
    "timezone": "Asia/Shanghai",
    "languages": "zh-CN,zh",
    "screen": "1368x912",
    "webrtc_public_ip": "",
    "surfaces": { "...": "see 4.2" }
  }
]
```

### 4.1 Top-level fields

⚠️ **All top-level fields must be JSON strings; numbers need quotes too.** The code reads them with `FindString`, so `"hardware_concurrency": 8` is silently ignored.

| Field | Format | Purpose |
|---|---|---|
| `name` | Any string | Name shown in the list on the Settings page |
| `platform` | `windows` / `macos` / `linux` / `android` / `ios`, lowercase | `navigator.platform` (`Win32` / `MacIntel` / `Linux x86_64` / `Linux armv8l` / `iPhone` respectively), the UA |
| `platform_version` | e.g. `10.0.0` | UA-CH `platformVersion`; if empty, the host OS version is used |
| `brand` | `Chrome`, `Chromium`, `Google Chrome`, `Edge`, `Opera`, `Vivaldi`, etc. | UA suffix and UA-CH brand list |
| `brand_version` | e.g. `152.0.0.0` | The part before the first dot is used as the major version |
| `device_model` | Any string | UA-CH `model` on mobile; mostly unused on desktop |
| `gpu_vendor` / `gpu_renderer` | Raw strings | WebGL unmasked vendor/renderer; `gpu_renderer` is also used to match the built-in GPU parameter table |
| `hardware_concurrency` | Positive integer as a string | `navigator.hardwareConcurrency` (window and Workers) |
| `device_memory` | Positive number as a string | `navigator.deviceMemory`; not rounded to the buckets defined by the spec |
| `timezone` | IANA time zone name | ICU default time zone; names ICU does not recognise are ignored |
| `languages` | Comma-separated, no q-values | `navigator.language(s)` |
| `webrtc_public_ip` | IPv4 dotted decimal; empty means no rewriting | Rewrites the public IPv4 address reported to the page in WebRTC ICE candidates |
| `surfaces` | Object | See 4.2 |

### 4.2 `surfaces`: keys that are actually read

The structure of `surfaces` matches the output of CreepJS, but the code only reads part of it. The table below lists the keys that **actually take effect**; keys not in the table are not used even if present.

| surface | Key (type) | Web API affected |
|---|---|---|
| `navigator` | `vendor` (string) | `navigator.vendor` |
| | `maxTouchPoints` (integer) | `navigator.maxTouchPoints` |
| | `userAgentData.architecture` / `.model` / `.bitness` (string) | These 3 items of `getHighEntropyValues()` |
| | `userAgentData.uaFullVersion` (string) | Full version in the browser-side UA-CH |
| | `bluetoothAvailability` (boolean) | `navigator.bluetooth.getAvailability()` |
| | `permissions` (object with keys `granted` / `denied` / `prompt`, each an array of permission names) | `permissions.query()`, only for geolocation, notifications, midi, camera, microphone |
| | `plugins` (array of `{name, description, filename}`) | `navigator.plugins` |
| `screen` | `width`, `height`, `availWidth`, `availHeight`, `colorDepth`, `pixelDepth` (integer) | `screen.*`, plus CSS `device-width` / `device-height` / `device-aspect-ratio` |
| `cssMedia` | `matchMediaCSS` (object with string values) | `matchMedia` and CSS `@media`; available keys are listed below |
| `css` | `system.colors` (**must be an object**: `{"ButtonFace": "rgb(...)"}`) | CSS system colors |
| `clientRects` | `elementClientRects`, `elementBoundingClientRect` (array of `{x, y, width, height}`) | `Element.getClientRects()` / `getBoundingClientRect()`, **only for elements whose class contains `rects`** |
| | `rangeClientRects`, `rangeBoundingClientRect` (same shape) | `Range.getClientRects()` / `getBoundingClientRect()`, for every Range |
| `svg` | `bBoxFull`, `extentOfCharFull` (`{x, y, width, height}`) | `getBBox()`, `getExtentOfChar()` |
| | `computedTextLengthByEmoji` (object: text → number), `computedTextLength` (number, fallback) | `getComputedTextLength()` |
| | `subStringLength` (number) | `getSubStringLength()` |
| `media` | `mimeTypes` (array of `{mimeType, audioPlayType, videoPlayType, mediaSource, mediaRecorder}`) | `canPlayType`, `MediaSource.isTypeSupported`, `MediaRecorder.isTypeSupported` |
| `voices` | `local`, `remote` (arrays of names), `defaultVoiceName`, `defaultVoiceLang` | `speechSynthesis.getVoices()` |
| `fonts` | `fontFaceLoadFonts` (array of strings) | Font availability, as an **allow-list**: any font not in the list is treated as not installed |
| `canvas2d` | `pixelsByKey` (object: key → array of RGBA bytes) | `canvas.toDataURL()`, `ctx.getImageData()` |
| `canvasWebgl` | `parameters` (object: GL constant name → integer); only 11 keys are read | `getParameter()` |
| | `extensionsGl1` / `extensionsGl2` (array of strings) | `getSupportedExtensions()` (WebGL1 / WebGL2) |
| | `pixelsRaw` / `pixels2Raw` (array of bytes) | `readPixels()` (WebGL1 / WebGL2) |
| `offlineAudioContext` | `binsFull` (array of numbers) | Rendered result of `OfflineAudioContext` |
| | `floatFrequencyDataFull`, `floatTimeDomainDataFull`, `byteFrequencyData` | The three data getters of `AnalyserNode` |
| | `compressorGainReduction` (number) | `DynamicsCompressorNode.reduction` |
| `intl` | `locale` (BCP-47 string) | Blink locale override (the default locale of `Intl`) |

Keys and values available in `cssMedia.matchMediaCSS`:

| Key | Allowed values |
|---|---|
| `prefers-color-scheme` | `dark` / `light` |
| `prefers-reduced-motion` | `reduce` / anything else |
| `forced-colors` | `active` / anything else |
| `hover`, `any-hover` | `hover` / anything else (treated as none) |
| `pointer`, `any-pointer` | `fine` / `coarse` / `none` |
| `display-mode` | `fullscreen` / `standalone` / `minimal-ui` / `browser` |
| `orientation` | `landscape` / `portrait` |
| `color-gamut` | `srgb` / `p3` / `rec2020` |
| `monochrome` | Integer as a string |

The 11 keys read from `canvasWebgl.parameters`:
`MAX_TEXTURE_SIZE`, `MAX_CUBE_MAP_TEXTURE_SIZE`, `MAX_RENDERBUFFER_SIZE`, `MAX_VERTEX_ATTRIBS`, `MAX_VERTEX_UNIFORM_VECTORS`, `MAX_VARYING_VECTORS`, `MAX_FRAGMENT_UNIFORM_VECTORS`, `MAX_TEXTURE_IMAGE_UNITS`, `MAX_VERTEX_TEXTURE_IMAGE_UNITS`, `MAX_COMBINED_TEXTURE_IMAGE_UNITS`, `SUBPIXEL_BITS`.

---

## 5. VisibleV8

VisibleV8 logs "what the page's JavaScript did to the browser". It is **compiled into V8** and has no runtime switch: as long as you run a patched build, it is always recording.

### 5.1 Launching

```bat
cd /d D:\vv8logs
C:\path\to\chrome.exe --no-sandbox --user-data-dir=D:\vv8profile https://example.com
```

| Point | Notes |
|---|---|
| `--no-sandbox` | **Required.** A sandboxed renderer process cannot create files, and when the log file cannot be opened the code calls `abort()` directly, which shows up as a crashed tab |
| Current directory | Logs are written to the **current working directory of the process**, so `cd` to the directory where you want the logs first, and make sure it is writable |
| Disk space | Logs grow quickly; browsing a few pages can produce several MB to tens of MB |

### 5.2 Log files

File name: `vv8-<timestamp>-<pid>-<tid>-vv8.<seq>.log`

- One file per thread that executes JavaScript.
- When a single file exceeds 1 GB, the sequence number is incremented and a new file is opened.
- In the actual output of the current Windows build, the timestamp segment is always `-2147483648` (for example `vv8--2147483648-15728-8064-vv8.0.log`), so **tell files apart by process ID and thread ID**; do not rely on the timestamp.
- Files are written in binary mode; line endings are `\n`.

### 5.3 Record format

One record per line. The first character gives the record type; fields are separated by `:`.

| First character | Meaning | Format |
|---|---|---|
| `~` | Switch to another isolate | `~<isolate address>` |
| `@` | Origin of the current page | `@"<location.href>":<security token>`; `@?` when it cannot be determined |
| `$` | A script seen for the first time, with its source | `$<script ID>:"<script name or URL>":<source>`; for scripts produced by `eval`, `$<script ID>:<parent script ID>:<source>` |
| `!` | Which script the following records belong to | `!<script ID>`; `!?` when it cannot be determined |
| `c` | Call to a browser API function | `c<offset>:<function>:<receiver>[:<args>...]` |
| `n` | Constructor call with `new` | `n<offset>:<function>[:<args>...]` |
| `g` | Property read | `g<offset>:<object>:<property name>` |
| `s` | Property write | `s<offset>:<object>:<property name>:<new value>` |

`<offset>` is the character offset of the call site within that script's source, or `-1` when it cannot be determined.

How values are written:

| Value | Written as |
|---|---|
| Integers, floating-point numbers | The number itself |
| Strings | `"..."`; `:` and `\` are preceded by `\`; control characters are written as `\xNN`; non-ASCII characters as `\uNNNN` |
| `true` / `false` / `null` / `undefined` | `#T` / `#F` / `#N` / `#U` |
| Functions | The function name; native browser functions are prefixed with `%`; anonymous functions are `<anonymous>` |
| Regular expressions | `/source/` |
| Objects | `{<identity hash>,<constructor name>}`, e.g. `{806607,Navigator}` |
| Plain `Object` / `Array` passed as a call argument | Expanded one level: `{<hash>,key\:value,key\:value}` |
| Anything else | `?` |

### 5.4 What is recorded

| Recorded | Not recorded |
|---|---|
| Calls to and construction of host API functions (DOM, BOM, Canvas, WebGL, etc.) | Property reads and writes on plain JS objects |
| Property reads and writes on the global object and host objects (including `Reflect.get` / `Reflect.set` and the `in` operator) | Property access on primitive values |
| Calls to `eval` and the `Function` constructor | Calls on V8 built-ins such as `Intl`, `Date` and `Math` (they are not host APIs) |
| The full source of every script (the first time it appears) | |
