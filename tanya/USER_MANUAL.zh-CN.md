# 使用手册：Tanya 指纹浏览器 + VisibleV8
## 目录

1. 准备工作
2. 指纹功能：通过设置页使用
3. 指纹功能：通过命令行使用
4. `fingerprint.json` 格式
5. VisibleV8

---

## 1. 准备工作

| 需要什么 | 说明 |
|---|---|
| 编译产物 | `out\default\chrome.exe` |
| `fingerprint.json` | 放在 **`chrome.exe` 所在目录**（代码用 `DIR_EXE` 查找，文件名固定） |
| 已打的补丁 | VisibleV8 / CDP 隐身 → `src/v8`；WebRTC → `src/third_party/webrtc` |

仓库里**没有**采集指纹的工具，也没有样例 `fingerprint.json`。文件内容需要在真实设备上用外部工具采集，结构与 CreepJS 的输出一致（见第 4 节）。

---

## 2. 指纹功能：通过设置页使用（推荐）

这是唯一能用上**完整指纹**（含 Canvas、WebGL、音频、字体等 `surfaces`）的方式。

### 步骤

1. 把 `fingerprint.json` 放到 `chrome.exe` 同目录。
2. 启动浏览器，建议每个指纹用独立的用户目录：
   ```bat
   chrome.exe --user-data-dir=D:\profiles\fp0
   ```
3. 打开 `chrome://settings/fingerprint`，或在设置页左侧菜单点「Fingerprint」。
4. 页面列出文件里的每个配置，显示的是它的 `name` 字段；没有 `name` 时显示 `Profile <序号>`。
5. 点某一行右侧的「New Tab」。浏览器会打开一个新标签页，并对它应用这份指纹。
6. **在这个新标签页里**输入网址访问目标网站。

### 点「New Tab」时发生了什么

| 步骤 | 行为 |
|---|---|
| 1 | 把该配置的 11 个顶层字段加上 `--fingerprint=<序号+1>` 追加到**浏览器进程**的命令行 |
| 2 | 打开一个新标签页（`chrome://newtab`） |
| 3 | 把整份配置（JSON）挂到这个标签页上，并通过 `PageBroadcast.UpdateTanyaFingerprintProfile` 发给渲染进程 |
| 4 | 渲染进程解析 JSON，替换**整个进程**的指纹策略；同时按 `surfaces.intl.locale` 设置语言区域 |
| 5 | 在浏览器侧生成伪装的 User-Agent 和 UA-CH，并强制该标签页的每次导航都使用它 |
| 6 | 重新加载标签页（绕过缓存） |

### 注意

- 每次打开设置页的 Fingerprint 页面都会重新读取文件，所以改完 `fingerprint.json` 后刷新设置页即可，不用重启。
- 文件不存在、不是合法 JSON、或顶层不是数组时，列表为空，不会报错。

---

## 3. 指纹功能：通过命令行使用

⚠️ 命令行方式**只能设置顶层字段**，没有 `surfaces`，所以 Canvas、WebGL 像素、音频、字体、DOMRect 等都不会被替换；主框架的 `navigator.userAgent` 也不会改（浏览器侧的 UA 伪装只在设置页流程里做），而 Worker 里的 UA 会按开关重建，两者可能不一致。适合只想改少数几个值的场景。

### 3.1 真正有效的开关（共 12 个，会传给渲染进程）

| 开关 | 值的格式 | 影响 |
|---|---|---|
| `--fingerprint=<整数>` | 必须是整数，如 `1` | 总开关。值本身不作为种子使用，只要能解析成整数就算启用 |
| `--fingerprint-platform` | `windows` / `macos` / `linux` / `android` / `ios`（小写） | `navigator.platform`，Worker 的 UA |
| `--fingerprint-platform-version` | 如 `10.0.0` | UA-CH `platformVersion`（Worker） |
| `--fingerprint-brand` | `Chrome` / `Edge` / `Opera` / `Vivaldi` 或任意字符串 | UA 后缀与 UA-CH 品牌（Worker） |
| `--fingerprint-brand-version` | 如 `152.0.0.0` | UA 里的主版本号、版本特性垫片、`getComputedStyle` 属性列表 |
| `--fingerprint-device-model` | 任意字符串 | 移动端 UA-CH `model` |
| `--fingerprint-gpu-vendor` | 如 `Google Inc. (Intel)` | WebGL `UNMASKED_VENDOR_WEBGL` |
| `--fingerprint-gpu-renderer` | 如 `ANGLE (Intel, ...)` | WebGL `UNMASKED_RENDERER_WEBGL`，并据此选择内置的 GPU 参数表 |
| `--fingerprint-hardware-concurrency` | 正整数 | `navigator.hardwareConcurrency` |
| `--fingerprint-device-memory` | 正数 | `navigator.deviceMemory` |
| `--fingerprint-timezone` | IANA 时区名，如 `Asia/Shanghai` | `Date`、`Intl` 的时区 |
| `--fingerprint-languages` | 逗号分隔，如 `zh-CN,zh` | `navigator.language(s)`；第一项成为 ICU 默认语言区域 |

示例：

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

## 4. `fingerprint.json` 格式

文件顶层是一个**数组**，每个元素是一个配置（对象）：

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
    "surfaces": { "...": "见 4.2" }
  }
]
```

### 4.1 顶层字段

⚠️ **所有顶层字段都必须是 JSON 字符串，数字也要加引号。** 代码用 `FindString` 读取，写成 `"hardware_concurrency": 8` 会被静默忽略。

| 字段 | 格式 | 作用 |
|---|---|---|
| `name` | 任意字符串 | 设置页列表里显示的名称 |
| `platform` | `windows` / `macos` / `linux` / `android` / `ios`，小写 | `navigator.platform`（分别为 `Win32` / `MacIntel` / `Linux x86_64` / `Linux armv8l` / `iPhone`）、UA |
| `platform_version` | 如 `10.0.0` | UA-CH `platformVersion`；留空时用本机系统版本 |
| `brand` | `Chrome`、`Chromium`、`Google Chrome`、`Edge`、`Opera`、`Vivaldi` 等 | UA 后缀和 UA-CH 品牌列表 |
| `brand_version` | 如 `152.0.0.0` | 取第一个点之前的部分作为主版本号 |
| `device_model` | 任意字符串 | 移动端 UA-CH 的 `model`；桌面端基本无用 |
| `gpu_vendor` / `gpu_renderer` | 原样字符串 | WebGL 未屏蔽的厂商/渲染器；`gpu_renderer` 还用来匹配内置 GPU 参数表 |
| `hardware_concurrency` | 正整数字符串 | `navigator.hardwareConcurrency`（窗口和 Worker） |
| `device_memory` | 正数字符串 | `navigator.deviceMemory`；不会按规范取整到档位 |
| `timezone` | IANA 时区名 | ICU 默认时区；ICU 不认识的名字会被忽略 |
| `languages` | 逗号分隔，不带 q 值 | `navigator.language(s)` |
| `webrtc_public_ip` | IPv4 点分十进制，留空表示不改写 | 改写 WebRTC ICE candidate 里报告给页面的公网 IPv4 地址 |
| `surfaces` | 对象 | 见 4.2 |

### 4.2 `surfaces`：实际会被读取的键

`surfaces` 的结构与 CreepJS 的输出一致，但代码只读取其中一部分。下表是**真正起作用**的键；表中没有的键写了也不会被使用。

| surface | 键（类型） | 影响的 Web API |
|---|---|---|
| `navigator` | `vendor`（字符串） | `navigator.vendor` |
| | `maxTouchPoints`（整数） | `navigator.maxTouchPoints` |
| | `userAgentData.architecture` / `.model` / `.bitness`（字符串） | `getHighEntropyValues()` 的这 3 项 |
| | `userAgentData.uaFullVersion`（字符串） | 浏览器侧 UA-CH 的完整版本号 |
| | `bluetoothAvailability`（布尔） | `navigator.bluetooth.getAvailability()` |
| | `permissions`（对象，键为 `granted` / `denied` / `prompt`，值为权限名数组） | `permissions.query()`，仅 geolocation、notifications、midi、camera、microphone |
| | `plugins`（数组，元素为 `{name, description, filename}`） | `navigator.plugins` |
| `screen` | `width`、`height`、`availWidth`、`availHeight`、`colorDepth`、`pixelDepth`（整数） | `screen.*`，以及 CSS `device-width` / `device-height` / `device-aspect-ratio` |
| `cssMedia` | `matchMediaCSS`（对象，值为字符串） | `matchMedia` 和 CSS `@media`，可用键见下 |
| `css` | `system.colors`（**必须是对象**：`{"ButtonFace": "rgb(...)"}`） | CSS 系统颜色 |
| `clientRects` | `elementClientRects`、`elementBoundingClientRect`（数组，元素为 `{x, y, width, height}`） | `Element.getClientRects()` / `getBoundingClientRect()`，**仅对 class 含 `rects` 的元素** |
| | `rangeClientRects`、`rangeBoundingClientRect`（同上） | `Range.getClientRects()` / `getBoundingClientRect()`，对所有 Range |
| `svg` | `bBoxFull`、`extentOfCharFull`（`{x, y, width, height}`） | `getBBox()`、`getExtentOfChar()` |
| | `computedTextLengthByEmoji`（对象：文本 → 数值）、`computedTextLength`（数值，兜底） | `getComputedTextLength()` |
| | `subStringLength`（数值） | `getSubStringLength()` |
| `media` | `mimeTypes`（数组，元素为 `{mimeType, audioPlayType, videoPlayType, mediaSource, mediaRecorder}`） | `canPlayType`、`MediaSource.isTypeSupported`、`MediaRecorder.isTypeSupported` |
| `voices` | `local`、`remote`（名称数组）、`defaultVoiceName`、`defaultVoiceLang` | `speechSynthesis.getVoices()` |
| `fonts` | `fontFaceLoadFonts`（字符串数组） | 字体可用性，**白名单**：不在列表里的字体一律视为不存在 |
| `canvas2d` | `pixelsByKey`（对象：键 → RGBA 字节数组） | `canvas.toDataURL()`、`ctx.getImageData()` |
| `canvasWebgl` | `parameters`（对象：GL 常量名 → 整数），只读 11 个键 | `getParameter()` |
| | `extensionsGl1` / `extensionsGl2`（字符串数组） | `getSupportedExtensions()`（WebGL1 / WebGL2） |
| | `pixelsRaw` / `pixels2Raw`（字节数组） | `readPixels()`（WebGL1 / WebGL2） |
| `offlineAudioContext` | `binsFull`（数值数组） | `OfflineAudioContext` 渲染结果 |
| | `floatFrequencyDataFull`、`floatTimeDomainDataFull`、`byteFrequencyData` | `AnalyserNode` 的三个取数方法 |
| | `compressorGainReduction`（数值） | `DynamicsCompressorNode.reduction` |
| `intl` | `locale`（BCP-47 字符串） | Blink 语言区域覆盖（`Intl` 默认 locale） |

`cssMedia.matchMediaCSS` 可用的键和值：

| 键 | 可取值 |
|---|---|
| `prefers-color-scheme` | `dark` / `light` |
| `prefers-reduced-motion` | `reduce` / 其他 |
| `forced-colors` | `active` / 其他 |
| `hover`、`any-hover` | `hover` / 其他（视为 none） |
| `pointer`、`any-pointer` | `fine` / `coarse` / `none` |
| `display-mode` | `fullscreen` / `standalone` / `minimal-ui` / `browser` |
| `orientation` | `landscape` / `portrait` |
| `color-gamut` | `srgb` / `p3` / `rec2020` |
| `monochrome` | 整数字符串 |

`canvasWebgl.parameters` 里会被读取的 11 个键：
`MAX_TEXTURE_SIZE`、`MAX_CUBE_MAP_TEXTURE_SIZE`、`MAX_RENDERBUFFER_SIZE`、`MAX_VERTEX_ATTRIBS`、`MAX_VERTEX_UNIFORM_VECTORS`、`MAX_VARYING_VECTORS`、`MAX_FRAGMENT_UNIFORM_VECTORS`、`MAX_TEXTURE_IMAGE_UNITS`、`MAX_VERTEX_TEXTURE_IMAGE_UNITS`、`MAX_COMBINED_TEXTURE_IMAGE_UNITS`、`SUBPIXEL_BITS`。

---

## 5. VisibleV8

VisibleV8 把「页面 JS 对浏览器做了什么」记成日志。它是**编译进 V8 的**，没有运行时开关：只要用的是打过补丁的构建，就一直在记录。

### 5.1 启动

```bat
cd /d D:\vv8logs
C:\path\to\chrome.exe --no-sandbox --user-data-dir=D:\vv8profile https://example.com
```

| 要点 | 说明 |
|---|---|
| `--no-sandbox` | **必须**。沙箱内的渲染进程无法创建文件，而日志文件打不开时代码会直接 `abort()`，表现为标签页崩溃 |
| 当前目录 | 日志写到**进程的当前工作目录**，所以先 `cd` 到想存日志的目录，并确保可写 |
| 磁盘空间 | 日志增长很快，浏览几个页面就能到几 MB～几十 MB |

### 5.2 日志文件

文件名：`vv8-<时间戳>-<进程号>-<线程号>-vv8.<序号>.log`

- 每个执行 JS 的线程一个文件。
- 单个文件超过 1 GB 后，序号加 1，开新文件。
- 在当前 Windows 构建的实际输出里，时间戳段固定显示为 `-2147483648`（例如 `vv8--2147483648-15728-8064-vv8.0.log`），所以**区分文件要看进程号和线程号**，不要依赖时间戳。
- 以二进制方式写入，换行是 `\n`。

### 5.3 记录格式

每行一条记录，首字符表示类型，字段用 `:` 分隔。

| 首字符 | 含义 | 格式 |
|---|---|---|
| `~` | 切换到另一个 isolate | `~<isolate 地址>` |
| `@` | 当前页面的 origin | `@"<location.href>":<安全令牌>`；取不到时为 `@?` |
| `$` | 第一次遇到的脚本及其源码 | `$<脚本ID>:"<脚本名或URL>":<源码>`；由 `eval` 产生的脚本为 `$<脚本ID>:<父脚本ID>:<源码>` |
| `!` | 之后的记录属于哪个脚本 | `!<脚本ID>`；无法判断时为 `!?` |
| `c` | 调用浏览器 API 函数 | `c<偏移>:<函数>:<接收者>[:<参数>...]` |
| `n` | 以 `new` 调用构造函数 | `n<偏移>:<函数>[:<参数>...]` |
| `g` | 读属性 | `g<偏移>:<对象>:<属性名>` |
| `s` | 写属性 | `s<偏移>:<对象>:<属性名>:<新值>` |

`<偏移>` 是调用点在该脚本源码中的字符偏移，无法确定时为 `-1`。

值的写法：

| 值 | 写法 |
|---|---|
| 整数、浮点数 | 直接写数字 |
| 字符串 | `"..."`；其中 `:` 和 `\` 前加 `\`；控制字符写成 `\xNN`；非 ASCII 字符写成 `\uNNNN` |
| `true` / `false` / `null` / `undefined` | `#T` / `#F` / `#N` / `#U` |
| 函数 | 函数名；浏览器原生函数前加 `%`；匿名函数为 `<anonymous>` |
| 正则 | `/源码/` |
| 对象 | `{<身份哈希>,<构造函数名>}`，如 `{806607,Navigator}` |
| 作为调用参数的普通 `Object` / `Array` | 展开一层：`{<哈希>,键\:值,键\:值}` |
| 其他 | `?` |

### 5.4 记录范围

| 会记录 | 不会记录 |
|---|---|
| 对宿主 API 函数的调用和构造（DOM、BOM、Canvas、WebGL 等） | 纯 JS 对象上的属性读写 |
| 对全局对象、宿主对象的属性读写（含 `Reflect.get` / `Reflect.set` 和 `in` 运算符） | 对原始值的属性访问 |
| `eval` 和 `Function` 构造器的调用 | `Intl`、`Date`、`Math` 等 V8 内建对象的调用（它们不是宿主 API） |
| 每个脚本的完整源码（首次出现时） | |
