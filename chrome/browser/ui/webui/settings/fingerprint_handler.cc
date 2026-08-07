#include "chrome/browser/ui/webui/settings/fingerprint_handler.h"

#include <optional>
#include <string>
#include <utility>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/system/sys_info.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/settings/tanya_ua_override_tab_helper.h"
#include "chrome/common/webui_url_constants.h"
#include "components/fingerprint/fingerprint_policy.h"
#include "components/fingerprint/switches.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace {

std::string ReadFingerprintJsonOnBlockingThread() {
  base::FilePath dir;
  if (!base::PathService::Get(base::DIR_EXE, &dir)) {
    return std::string();
  }
  base::FilePath path = dir.AppendASCII("fingerprint.json");
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    return std::string();
  }
  return contents;
}

void ApplyProfileToCommandLine(const base::DictValue& row, int index) {
  base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
  cmd->AppendSwitchASCII(fingerprint::kFingerprint,
                         base::NumberToString(index + 1));
  const auto set = [&](const char* sw, const char* key) {
    const std::string* value = row.FindString(key);
    cmd->AppendSwitchASCII(sw, value ? *value : std::string());
  };
  set(fingerprint::kFingerprintPlatform, "platform");
  set(fingerprint::kFingerprintPlatformVersion, "platform_version");
  set(fingerprint::kFingerprintBrand, "brand");
  set(fingerprint::kFingerprintBrandVersion, "brand_version");
  set(fingerprint::kFingerprintDeviceModel, "device_model");
  set(fingerprint::kFingerprintGpuVendor, "gpu_vendor");
  set(fingerprint::kFingerprintGpuRenderer, "gpu_renderer");
  set(fingerprint::kFingerprintHardwareConcurrency, "hardware_concurrency");
  set(fingerprint::kFingerprintDeviceMemory, "device_memory");
  set(fingerprint::kFingerprintTimezone, "timezone");
  set(fingerprint::kFingerprintLanguages, "languages");
  set(fingerprint::kFingerprintScreen, "screen");
  set(fingerprint::kFingerprintWebrtcPublicIp, "webrtc_public_ip");
}

bool BuildTanyaUaFromPolicy(const fingerprint::FingerprintPolicy& policy,
                            std::string* out_ua,
                            blink::UserAgentMetadata* out_meta) {
  std::string platform = base::ToLowerASCII(policy.platform());
  if (platform == "mac") {
    platform = "macos";
  }
  if (platform.empty() && policy.brand().empty() &&
      policy.brand_version().empty()) {
    return false;
  }
  if (platform.empty()) {
#if BUILDFLAG(IS_MAC)
    platform = "macos";
#elif BUILDFLAG(IS_WIN)
    platform = "windows";
#elif BUILDFLAG(IS_LINUX)
    platform = "linux";
#endif
  }

  std::string ua_platform;
  std::string ua_arch = "x86";
  std::string uad_platform = "Unknown";
  std::string uad_platform_version;
  const std::string& gpu_renderer = policy.gpu_renderer();
  const bool apple_silicon_renderer =
      gpu_renderer.find("Apple M") != std::string::npos;
  bool host_is_apple_silicon = false;
#if BUILDFLAG(IS_MAC) && defined(ARCH_CPU_ARM64)
  host_is_apple_silicon = true;
#endif

  if (platform == "windows") {
    ua_platform = "Windows NT 10.0; Win64; x64";
    uad_platform = "Windows";
    uad_platform_version = policy.platform_version();
  } else if (platform == "macos") {
    ua_platform = "Macintosh; Intel Mac OS X 10_15_7";
    uad_platform = "macOS";
    if (apple_silicon_renderer || host_is_apple_silicon) {
      ua_arch = "arm";
    }
    uad_platform_version = policy.platform_version();
  } else if (platform == "linux") {
    ua_platform = "X11; Linux x86_64";
    uad_platform = "Linux";
    uad_platform_version = policy.platform_version();
  } else if (platform == "android") {
    ua_platform = "Linux; Android 10; K";
    ua_arch = "";
    uad_platform = "Android";
    uad_platform_version = policy.platform_version().empty()
                               ? "14.0.0"
                               : policy.platform_version();
  } else if (platform == "ios") {
    const std::string& real_ios_ver = policy.platform_version();
    uad_platform_version = real_ios_ver.empty() ? "17.6.1" : real_ios_ver;
    std::string os_under;
    base::ReplaceChars(uad_platform_version, ".", "_", &os_under);
    ua_platform =
        base::StrCat({"iPhone; CPU iPhone OS ", os_under, " like Mac OS X"});
    ua_arch = "";
    uad_platform = "iOS";
  } else {
    return false;
  }

  if (uad_platform_version.empty()) {
    int32_t maj = 0, min = 0, bugfix = 0;
    base::SysInfo::OperatingSystemVersionNumbers(&maj, &min, &bugfix);
    if (maj > 0) {
      uad_platform_version = base::StringPrintf("%d.%d.%d", maj, min, bugfix);
    }
  }

  std::string brand = policy.brand().empty() ? "Chrome" : policy.brand();
  std::string brand_version = policy.brand_version();
  if (brand_version.empty()) {
    brand_version = std::string(version_info::GetVersionNumber());
  }
  std::string major_version = brand_version;
  if (auto dot = major_version.find('.'); dot != std::string::npos) {
    major_version.resize(dot);
  }
  const std::string reduced_version = major_version + ".0.0.0";
  const bool is_ios = (platform == "ios");
  const bool is_mobile = (platform == "android" || is_ios);

  std::string ua;
  if (is_ios) {
    ua = base::StrCat({
        "Mozilla/5.0 (",
        ua_platform,
        ") AppleWebKit/605.1.15 (KHTML, like Gecko) CriOS/",
        brand_version,
        " Mobile/15E148 Safari/604.1",
    });
  } else {
    ua = base::StrCat({
        "Mozilla/5.0 (",
        ua_platform,
        ") AppleWebKit/537.36 (KHTML, like Gecko) Chrome/",
        reduced_version,
        is_mobile ? " Mobile Safari/537.36" : " Safari/537.36",
    });
  }
  if (!is_mobile) {
    if (brand == "Edge") {
      ua = base::StrCat({ua, " Edg/", reduced_version});
    } else if (brand == "Opera") {
      ua = base::StrCat({ua, " OPR/", reduced_version});
    } else if (brand == "Vivaldi") {
      ua = base::StrCat({ua, " Vivaldi/", brand_version});
    }
  }
  *out_ua = ua;

  blink::UserAgentMetadata meta;
  int major_version_int = 0;
  base::StringToInt(major_version, &major_version_int);
  std::string brand_name;
  if (brand == "Chrome" || brand == "Chromium") {
    brand_name = "Google Chrome";
  } else if (brand == "Edge") {
    brand_name = "Microsoft Edge";
  } else {
    brand_name = brand;
  }

  static const char* const kGreasyChars[] = {" ", "(", ":", "-", ".", "/",
                                             ")", ";", "=", "?", "_"};
  static const char* const kGreasyVersions[] = {"8", "99", "24"};
  static constexpr size_t kGreasyCharCount =
      sizeof(kGreasyChars) / sizeof(kGreasyChars[0]);
  static constexpr size_t kGreasyVersionCount =
      sizeof(kGreasyVersions) / sizeof(kGreasyVersions[0]);
  const std::string greasy_brand = base::StrCat(
      {"Not", kGreasyChars[major_version_int % kGreasyCharCount], "A",
       kGreasyChars[(major_version_int + 1) % kGreasyCharCount], "Brand"});
  const std::string greasy_major =
      kGreasyVersions[major_version_int % kGreasyVersionCount];
  const std::string greasy_full = greasy_major + ".0.0.0";

  static constexpr size_t kOrders[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2},
                                           {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
  const size_t* order = kOrders[major_version_int % 6];

  {
    blink::UserAgentBrandVersion pre[3] = {
        {greasy_brand, greasy_major},
        {"Chromium", major_version},
        {brand_name, major_version},
    };
    meta.brand_version_list.resize(3);
    for (size_t i = 0; i < 3; ++i) {
      meta.brand_version_list[order[i]] = pre[i];
    }
  }
  {
    blink::UserAgentBrandVersion pre[3] = {
        {greasy_brand, greasy_full},
        {"Chromium", brand_version},
        {brand_name, brand_version},
    };
    meta.brand_full_version_list.resize(3);
    for (size_t i = 0; i < 3; ++i) {
      meta.brand_full_version_list[order[i]] = pre[i];
    }
  }
  meta.full_version = brand_version;
  meta.platform = uad_platform;
  meta.platform_version = uad_platform_version;
  meta.architecture = is_mobile ? "" : ua_arch;
  meta.bitness = is_mobile ? "" : "64";
  meta.wow64 = false;
  meta.mobile = is_mobile;
  meta.form_factors = is_mobile ? std::vector<std::string>{"Mobile"}
                                : std::vector<std::string>{"Desktop"};
  if (is_mobile) {
    std::string device_model = policy.device_model();
    if (device_model.empty()) {
      device_model = is_ios ? "iPhone" : "Pixel 7";
    }
    meta.model = device_model;
  }
  *out_meta = std::move(meta);
  return true;
}

}  // namespace

namespace settings {

FingerprintHandler::FingerprintHandler(Profile* profile) : profile_(profile) {}

FingerprintHandler::~FingerprintHandler() = default;

void FingerprintHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "initializeFingerprint",
      base::BindRepeating(&FingerprintHandler::HandleInitialize,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openFingerprintTab",
      base::BindRepeating(&FingerprintHandler::HandleOpenFingerprintTab,
                          base::Unretained(this)));
}

void FingerprintHandler::HandleInitialize(const base::ListValue& args) {
  AllowJavascript();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&ReadFingerprintJsonOnBlockingThread),
      base::BindOnce(&FingerprintHandler::OnJsonLoaded,
                     weak_factory_.GetWeakPtr()));
}

void FingerprintHandler::OnJsonLoaded(std::string contents) {
  profiles_.clear();
  base::ListValue rows;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(contents, base::JSON_PARSE_RFC);
  if (parsed && parsed->is_list()) {
    profiles_ = parsed->GetList().Clone();
    int index = 0;
    for (const base::Value& item : profiles_) {
      std::string label;
      if (const base::DictValue* dict = item.GetIfDict()) {
        if (const std::string* name = dict->FindString("name")) {
          label = *name;
        }
      }
      if (label.empty()) {
        label = "Profile " + base::NumberToString(index);
      }
      base::DictValue row;
      row.Set("id", base::NumberToString(index));
      row.Set("label", label);
      rows.Append(std::move(row));
      ++index;
    }
  }
  FireWebUIListener("fingerprint-rows-changed", rows);
}

void FingerprintHandler::HandleOpenFingerprintTab(
    const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  int index = -1;
  if (!base::StringToInt(args[0].GetString(), &index)) {
    return;
  }
  if (index < 0 || static_cast<size_t>(index) >= profiles_.size()) {
    return;
  }
  const base::DictValue* row = profiles_[index].GetIfDict();
  if (row) {
    ApplyProfileToCommandLine(*row, index);
  }
  content::WebContents* new_contents = web_ui()->GetWebContents()->OpenURL(
      content::OpenURLParams(chrome::ChromeUINewTabURLAsGURL(), content::Referrer(),
                             WindowOpenDisposition::NEW_FOREGROUND_TAB,
                             ui::PAGE_TRANSITION_LINK, false),
      {});
  if (row && new_contents) {
    blink::RendererPreferences* prefs = new_contents->GetMutableRendererPrefs();
    const auto get = [&](const char* key) -> std::string {
      const std::string* v = row->FindString(key);
      return v ? *v : std::string();
    };
    prefs->fingerprint_enabled = true;
    prefs->fingerprint_platform = get("platform");
    prefs->fingerprint_platform_version = get("platform_version");
    prefs->fingerprint_brand = get("brand");
    prefs->fingerprint_brand_version = get("brand_version");
    prefs->fingerprint_gpu_vendor = get("gpu_vendor");
    prefs->fingerprint_gpu_renderer = get("gpu_renderer");
    prefs->fingerprint_hardware_concurrency = get("hardware_concurrency");
    prefs->fingerprint_device_memory = get("device_memory");
    prefs->fingerprint_timezone = get("timezone");
    prefs->fingerprint_languages = get("languages");
    prefs->fingerprint_screen = get("screen");
    new_contents->SyncRendererPrefs();

    fingerprint::FingerprintPolicy ua_policy;
    ua_policy.set_platform(get("platform"));
    ua_policy.set_platform_version(get("platform_version"));
    ua_policy.set_brand(get("brand"));
    ua_policy.set_brand_version(get("brand_version"));
    ua_policy.set_gpu_renderer(get("gpu_renderer"));
    ua_policy.set_device_model(get("device_model"));
    std::string ua_str;
    blink::UserAgentMetadata ua_meta;
    if (BuildTanyaUaFromPolicy(ua_policy, &ua_str, &ua_meta) &&
        !ua_str.empty()) {
      blink::UserAgentOverride ua_override;
      ua_override.ua_string_override = ua_str;
      ua_override.ua_metadata_override = ua_meta;
      new_contents->SetUserAgentOverride(ua_override,
                                         /*override_in_new_tabs=*/true);

      TanyaUaOverrideTabHelper::CreateForWebContents(new_contents);
      TanyaUaOverrideTabHelper::FromWebContents(new_contents)
          ->set_force_ua_override(true);

      if (auto* entry = new_contents->GetController().GetVisibleEntry()) {
        entry->SetIsOverridingUserAgent(true);
        new_contents->GetController().Reload(
            content::ReloadType::BYPASSING_CACHE, /*check_for_repost=*/false);
      }
    }
  }
}

}  // namespace settings
