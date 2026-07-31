
#include "chrome/browser/container/container_useragent_manager.h"

#include <algorithm>
#include <random>
#include <sstream>

#include "base/hash/hash.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/string_number_conversions.h"
#include "base/no_destructor.h"
#include "content/public/browser/browser_context.h"
#include "url/gurl.h"

namespace tab_container {

namespace {

std::string DeviceTypeToString(UADeviceType type) {
  switch (type) {
    case UADeviceType::kDesktop: return "Desktop";
    case UADeviceType::kMobile: return "Mobile";
    case UADeviceType::kTablet: return "Tablet";
    case UADeviceType::kSmartTV: return "SmartTV";
    case UADeviceType::kConsole: return "Console";
    case UADeviceType::kWearable: return "Wearable";
    case UADeviceType::kEmbedded: return "Embedded";
  }
  return "Unknown";
}

std::string OSToString(UAOperatingSystem os) {
  switch (os) {
    case UAOperatingSystem::kWindows: return "Windows";
    case UAOperatingSystem::kMacOS: return "macOS";
    case UAOperatingSystem::kLinux: return "Linux";
    case UAOperatingSystem::kAndroid: return "Android";
    case UAOperatingSystem::kIOS: return "iOS";
    case UAOperatingSystem::kChromeOS: return "Chrome OS";
    case UAOperatingSystem::kUnknown: return "Unknown";
  }
  return "Unknown";
}

const std::vector<std::string>& GetChromeVersions() {
  static const base::NoDestructor<std::vector<std::string>> kChromeVersions(
      std::vector<std::string>{
          "120.0.0.0", "121.0.0.0", "122.0.0.0", "123.0.0.0", "124.0.0.0",
          "125.0.0.0", "126.0.0.0", "127.0.0.0", "128.0.0.0", "129.0.0.0",
      });
  return *kChromeVersions;
}

const std::vector<std::string>& GetWindowsVersions() {
  static const base::NoDestructor<std::vector<std::string>> kWindowsVersions(
      std::vector<std::string>{"10.0", "11.0"});
  return *kWindowsVersions;
}

const std::vector<std::string>& GetMacOSVersions() {
  static const base::NoDestructor<std::vector<std::string>> kMacOSVersions(
      std::vector<std::string>{"12_0_0", "12_6_0", "13_0_0", "13_4_0",
                                "14_0_0", "14_2_0"});
  return *kMacOSVersions;
}

const std::vector<std::string>& GetAndroidVersions() {
  static const base::NoDestructor<std::vector<std::string>> kAndroidVersions(
      std::vector<std::string>{"11", "12", "13", "14"});
  return *kAndroidVersions;
}

const std::vector<std::pair<std::string, std::string>>&
GetAndroidDevices() {
  static const base::NoDestructor<std::vector<std::pair<std::string, std::string>>>
      kAndroidDevices(std::vector<std::pair<std::string, std::string>>{
          {"SM-G991B", "Samsung"},
          {"SM-S918B", "Samsung"},
          {"Pixel 7", "Google"},
          {"Pixel 8 Pro", "Google"},
          {"M2102K1G", "Xiaomi"},
          {"CPH2451", "OnePlus"},
      });
  return *kAndroidDevices;
}

}  

std::string UserAgentProfile::GenerateUserAgent() const {
  if (!user_agent.empty()) {
    return user_agent;
  }

  std::stringstream ua;

  ua << "Mozilla/5.0 ";

  switch (device_type) {
    case UADeviceType::kDesktop:
      if (os == UAOperatingSystem::kWindows) {
        ua << "(Windows NT " << os_version << "; Win64; x64) ";
      } else if (os == UAOperatingSystem::kMacOS) {
        ua << "(Macintosh; Intel Mac OS X " << os_version << ") ";
      } else if (os == UAOperatingSystem::kLinux) {
        ua << "(X11; Linux x86_64) ";
      }
      break;

    case UADeviceType::kMobile:
      if (os == UAOperatingSystem::kAndroid) {
        ua << "(Linux; Android " << os_version;
        if (!device_model.empty()) {
          ua << "; " << device_model;
        }
        ua << ") ";
      } else if (os == UAOperatingSystem::kIOS) {
        ua << "(iPhone; CPU iPhone OS " << os_version << " like Mac OS X) ";
      }
      break;

    case UADeviceType::kTablet:
      if (os == UAOperatingSystem::kAndroid) {
        ua << "(Linux; Android " << os_version;
        if (!device_model.empty()) {
          ua << "; " << device_model;
        }
        ua << ") ";
      } else if (os == UAOperatingSystem::kIOS) {
        ua << "(iPad; CPU OS " << os_version << " like Mac OS X) ";
      }
      break;

    default:
      ua << "(Unknown) ";
      break;
  }

  if (engine == UABrowserEngine::kBlink) {
    ua << "AppleWebKit/537.36 (KHTML, like Gecko) ";
    ua << "Chrome/" << browser_version << " ";
    if (device_type == UADeviceType::kMobile) {
      ua << "Mobile ";
    }
    ua << "Safari/537.36";
  } else if (engine == UABrowserEngine::kGecko) {
    ua << "Gecko/" << engine_version << " Firefox/" << browser_version;
  } else if (engine == UABrowserEngine::kWebKit) {
    ua << "AppleWebKit/537.36 (KHTML, like Gecko) ";
    ua << "Version/" << browser_version << " Safari/537.36";
  }

  return ua.str();
}

std::map<std::string, std::string> UserAgentProfile::GenerateClientHints() const {
  std::map<std::string, std::string> hints;

  if (!sec_ch_ua.empty()) {
    hints["Sec-CH-UA"] = sec_ch_ua;
  } else if (engine == UABrowserEngine::kBlink) {
    hints["Sec-CH-UA"] = base::StringPrintf(
        "\"Chromium\";v=\"%s\", \"Google Chrome\";v=\"%s\", \"Not-A.Brand\";v=\"24\"",
        browser_major_version.c_str(), browser_major_version.c_str());
  }

  if (!sec_ch_ua_mobile.empty()) {
    hints["Sec-CH-UA-Mobile"] = sec_ch_ua_mobile;
  } else {
    hints["Sec-CH-UA-Mobile"] = (device_type == UADeviceType::kMobile || 
                                  device_type == UADeviceType::kTablet) 
                                 ? "?1" : "?0";
  }

  if (!sec_ch_ua_platform.empty()) {
    hints["Sec-CH-UA-Platform"] = sec_ch_ua_platform;
  } else {
    switch (os) {
      case UAOperatingSystem::kWindows:
        hints["Sec-CH-UA-Platform"] = "\"Windows\"";
        break;
      case UAOperatingSystem::kMacOS:
        hints["Sec-CH-UA-Platform"] = "\"macOS\"";
        break;
      case UAOperatingSystem::kLinux:
        hints["Sec-CH-UA-Platform"] = "\"Linux\"";
        break;
      case UAOperatingSystem::kAndroid:
        hints["Sec-CH-UA-Platform"] = "\"Android\"";
        break;
      case UAOperatingSystem::kIOS:
        hints["Sec-CH-UA-Platform"] = "\"iOS\"";
        break;
      default:
        hints["Sec-CH-UA-Platform"] = "\"Unknown\"";
        break;
    }
  }

  if (!sec_ch_ua_platform_version.empty()) {
    hints["Sec-CH-UA-Platform-Version"] = sec_ch_ua_platform_version;
  } else {
    hints["Sec-CH-UA-Platform-Version"] = "\"" + os_version + "\"";
  }

  if (!sec_ch_ua_arch.empty()) {
    hints["Sec-CH-UA-Arch"] = sec_ch_ua_arch;
  } else if (device_type == UADeviceType::kDesktop) {
    hints["Sec-CH-UA-Arch"] = "\"x86\"";
  }

  if (!sec_ch_ua_bitness.empty()) {
    hints["Sec-CH-UA-Bitness"] = sec_ch_ua_bitness;
  } else if (device_type == UADeviceType::kDesktop) {
    hints["Sec-CH-UA-Bitness"] = "\"64\"";
  }

  if (!sec_ch_ua_model.empty()) {
    hints["Sec-CH-UA-Model"] = sec_ch_ua_model;
  } else if (!device_model.empty()) {
    hints["Sec-CH-UA-Model"] = "\"" + device_model + "\"";
  }

  return hints;
}

ContainerUserAgentManager::ContainerUserAgentManager(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  InitializeBuiltinProfiles();

  ;
}

ContainerUserAgentManager::~ContainerUserAgentManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ;
}

void ContainerUserAgentManager::AddObserver(
    ContainerUserAgentObserver* observer) {
  observers_.AddObserver(observer);
}

void ContainerUserAgentManager::RemoveObserver(
    ContainerUserAgentObserver* observer) {
  observers_.RemoveObserver(observer);
}

bool ContainerUserAgentManager::CreateProfile(const UserAgentProfile& profile) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (profile.profile_id.empty()) {
    return false;
  }

  UserAgentProfile new_profile = profile;
  new_profile.created_at = base::Time::Now();

  profiles_[profile.profile_id] = new_profile;

  ;

  return true;
}

std::optional<UserAgentProfile> ContainerUserAgentManager::GetProfile(
    const std::string& profile_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = profiles_.find(profile_id);
  if (it == profiles_.end()) {
    return std::nullopt;
  }

  return it->second;
}

std::vector<UserAgentProfile> ContainerUserAgentManager::GetAllProfiles() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<UserAgentProfile> result;
  for (const auto& pair : profiles_) {
    result.push_back(pair.second);
  }
  return result;
}

std::vector<UserAgentProfile>
ContainerUserAgentManager::GetBuiltinProfiles() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<UserAgentProfile> result;
  for (const auto& pair : profiles_) {
    if (pair.second.is_builtin) {
      result.push_back(pair.second);
    }
  }
  return result;
}

std::vector<UserAgentProfile>
ContainerUserAgentManager::GetProfilesByDeviceType(
    UADeviceType device_type) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<UserAgentProfile> result;
  for (const auto& pair : profiles_) {
    if (pair.second.device_type == device_type) {
      result.push_back(pair.second);
    }
  }
  return result;
}

std::vector<UserAgentProfile>
ContainerUserAgentManager::GetProfilesByOS(UAOperatingSystem os) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<UserAgentProfile> result;
  for (const auto& pair : profiles_) {
    if (pair.second.os == os) {
      result.push_back(pair.second);
    }
  }
  return result;
}

bool ContainerUserAgentManager::DeleteProfile(const std::string& profile_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = profiles_.find(profile_id);
  if (it == profiles_.end()) {
    return false;
  }

  if (it->second.is_builtin) {
    ;
    return false;
  }

  profiles_.erase(it);
  return true;
}

bool ContainerUserAgentManager::UpdateProfile(const UserAgentProfile& profile) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = profiles_.find(profile.profile_id);
  if (it == profiles_.end()) {
    return false;
  }

  it->second = profile;

  for (auto& pair : containers_) {
    if (pair.second->config.profile_id == profile.profile_id) {
      UpdateEffectiveValues(pair.first);
    }
  }

  return true;
}

bool ContainerUserAgentManager::SetContainerConfig(
    const std::string& container_id,
    const ContainerUserAgentConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = containers_.find(container_id);
  if (it == containers_.end()) {
    auto info = std::make_unique<ContainerInfo>();
    info->container_id = container_id;
    info->config = config;
    info->config.container_id = container_id;
    containers_[container_id] = std::move(info);
  } else {
    it->second->config = config;
    it->second->config.container_id = container_id;
  }

  UpdateEffectiveValues(container_id);

  ;

  return true;
}

std::optional<ContainerUserAgentConfig>
ContainerUserAgentManager::GetContainerConfig(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = containers_.find(container_id);
  if (it == containers_.end()) {
    return std::nullopt;
  }

  return it->second->config;
}

bool ContainerUserAgentManager::RemoveContainerConfig(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return containers_.erase(container_id) > 0;
}

bool ContainerUserAgentManager::ApplyProfileToContainer(
    const std::string& container_id,
    const std::string& profile_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto profile_it = profiles_.find(profile_id);
  if (profile_it == profiles_.end()) {
    ;
    return false;
  }

  auto it = containers_.find(container_id);
  if (it == containers_.end()) {
    auto info = std::make_unique<ContainerInfo>();
    info->container_id = container_id;
    containers_[container_id] = std::move(info);
    it = containers_.find(container_id);
  }

  it->second->config.profile_id = profile_id;
  it->second->config.use_custom_ua = false;

  profiles_[profile_id].last_used = base::Time::Now();

  UpdateEffectiveValues(container_id);
  NotifyProfileApplied(container_id, profile_id);

  ;

  return true;
}

bool ContainerUserAgentManager::SetCustomUserAgent(
    const std::string& container_id,
    const std::string& user_agent) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = containers_.find(container_id);
  if (it == containers_.end()) {
    auto info = std::make_unique<ContainerInfo>();
    info->container_id = container_id;
    containers_[container_id] = std::move(info);
    it = containers_.find(container_id);
  }

  it->second->config.use_custom_ua = true;
  it->second->config.custom_user_agent = user_agent;

  UpdateEffectiveValues(container_id);

  return true;
}

std::string ContainerUserAgentManager::GetUserAgent(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = containers_.find(container_id);
  if (it == containers_.end()) {
    return "";  
  }

  return it->second->effective_user_agent;
}

std::string ContainerUserAgentManager::GetUserAgentForUrl(
    const std::string& container_id,
    const std::string& url) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = containers_.find(container_id);
  if (it == containers_.end()) {
    return "";
  }

  GURL parsed_url(url);
  if (parsed_url.is_valid()) {
    auto& overrides = it->second->config.domain_overrides;

    const auto host_view = parsed_url.host();
    const std::string host(host_view.data(), host_view.size());
    auto override_it = overrides.find(host);
    if (override_it != overrides.end()) {
      return override_it->second;
    }
  }

  return it->second->effective_user_agent;
}

std::map<std::string, std::string>
ContainerUserAgentManager::GetClientHints(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = containers_.find(container_id);
  if (it == containers_.end()) {
    return {};
  }

  return it->second->effective_client_hints;
}

std::string ContainerUserAgentManager::GetAcceptLanguage(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = containers_.find(container_id);
  if (it == containers_.end()) {
    return "";
  }

  if (it->second->config.spoof_accept_language) {
    return it->second->config.spoofed_accept_language;
  }

  return "";
}

std::string ContainerUserAgentManager::GetNavigatorPlatform(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = containers_.find(container_id);
  if (it == containers_.end()) {
    return "";
  }

  if (it->second->config.spoof_platform) {
    return it->second->config.spoofed_platform;
  }

  if (!it->second->config.profile_id.empty()) {
    auto profile_it = profiles_.find(it->second->config.profile_id);
    if (profile_it != profiles_.end()) {
      switch (profile_it->second.os) {
        case UAOperatingSystem::kWindows: return "Win32";
        case UAOperatingSystem::kMacOS: return "MacIntel";
        case UAOperatingSystem::kLinux: return "Linux x86_64";
        case UAOperatingSystem::kAndroid: return "Linux armv8l";
        case UAOperatingSystem::kIOS: return "iPhone";
        default: return "";
      }
    }
  }

  return "";
}

bool ContainerUserAgentManager::SetDomainOverride(
    const std::string& container_id,
    const std::string& domain,
    const std::string& user_agent) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = containers_.find(container_id);
  if (it == containers_.end()) {
    auto info = std::make_unique<ContainerInfo>();
    info->container_id = container_id;
    containers_[container_id] = std::move(info);
    it = containers_.find(container_id);
  }

  it->second->config.domain_overrides[domain] = user_agent;

  return true;
}

bool ContainerUserAgentManager::RemoveDomainOverride(
    const std::string& container_id,
    const std::string& domain) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = containers_.find(container_id);
  if (it == containers_.end()) {
    return false;
  }

  return it->second->config.domain_overrides.erase(domain) > 0;
}

std::map<std::string, std::string>
ContainerUserAgentManager::GetDomainOverrides(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = containers_.find(container_id);
  if (it == containers_.end()) {
    return {};
  }

  return it->second->config.domain_overrides;
}

UserAgentProfile ContainerUserAgentManager::GenerateRandomProfile() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> type_dist(0, 100);

  int roll = type_dist(gen);
  UADeviceType device_type;
  if (roll < 60) {
    device_type = UADeviceType::kDesktop;
  } else if (roll < 90) {
    device_type = UADeviceType::kMobile;
  } else {
    device_type = UADeviceType::kTablet;
  }

  return GenerateRandomProfile(device_type);
}

UserAgentProfile ContainerUserAgentManager::GenerateRandomProfile(
    UADeviceType device_type) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::random_device rd;
  std::mt19937 gen(rd());

  UserAgentProfile profile;
  profile.profile_id = "random_" + base::NumberToString(base::RandUint64());
  profile.device_type = device_type;
  profile.engine = UABrowserEngine::kBlink;
  profile.browser_name = "Chrome";

  std::uniform_int_distribution<size_t> version_dist(0,
                                                     GetChromeVersions().size() - 1);
  profile.browser_version =
      GetChromeVersions().at(version_dist(gen));
  profile.browser_major_version = profile.browser_version.substr(
      0, profile.browser_version.find('.'));
  profile.engine_version = profile.browser_version;

  switch (device_type) {
    case UADeviceType::kDesktop: {
      std::uniform_int_distribution<> os_dist(0, 2);
      int os_roll = os_dist(gen);
      if (os_roll == 0) {
        profile.os = UAOperatingSystem::kWindows;
        std::uniform_int_distribution<size_t> win_dist(
            0, GetWindowsVersions().size() - 1);
        profile.os_version = GetWindowsVersions().at(win_dist(gen));
      } else if (os_roll == 1) {
        profile.os = UAOperatingSystem::kMacOS;
        std::uniform_int_distribution<size_t> mac_dist(
            0, GetMacOSVersions().size() - 1);
        profile.os_version = GetMacOSVersions().at(mac_dist(gen));
      } else {
        profile.os = UAOperatingSystem::kLinux;
        profile.os_version = "x86_64";
      }
      break;
    }

    case UADeviceType::kMobile:
    case UADeviceType::kTablet: {
      profile.os = UAOperatingSystem::kAndroid;
      std::uniform_int_distribution<size_t> android_dist(
          0, GetAndroidVersions().size() - 1);
      profile.os_version = GetAndroidVersions().at(android_dist(gen));
      std::uniform_int_distribution<size_t> device_dist(
          0, GetAndroidDevices().size() - 1);
      const auto& device = GetAndroidDevices().at(device_dist(gen));
      profile.device_model = device.first;
      profile.device_vendor = device.second;
      break;
    }

    default:
      profile.os = UAOperatingSystem::kWindows;
      profile.os_version = "10.0";
      break;
  }

  profile.name = "Random " + DeviceTypeToString(device_type) + " " + 
                 OSToString(profile.os);

  return profile;
}

bool ContainerUserAgentManager::ApplyRandomProfile(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  UserAgentProfile profile = GenerateRandomProfile();
  CreateProfile(profile);
  return ApplyProfileToContainer(container_id, profile.profile_id);
}

bool ContainerUserAgentManager::ApplyRandomProfile(
    const std::string& container_id,
    UADeviceType device_type) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  UserAgentProfile profile = GenerateRandomProfile(device_type);
  CreateProfile(profile);
  return ApplyProfileToContainer(container_id, profile.profile_id);
}

size_t ContainerUserAgentManager::GetProfileCount() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return profiles_.size();
}

size_t ContainerUserAgentManager::GetContainerConfigCount() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return containers_.size();
}

void ContainerUserAgentManager::SetDebugLoggingEnabled(bool enabled) {
  debug_logging_enabled_ = enabled;
}

std::string ContainerUserAgentManager::GetDiagnosticReport() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::stringstream report;
  report << "=== ContainerUserAgentManager Diagnostic Report ===\n\n";

  report << "Profiles: " << profiles_.size() << "\n";
  for (const auto& pair : profiles_) {
    const auto& p = pair.second;
    report << "  - " << pair.first;
    if (p.is_builtin) report << " (builtin)";
    report << ":\n";
    report << "      Device: " << DeviceTypeToString(p.device_type) << "\n";
    report << "      OS: " << OSToString(p.os) << " " << p.os_version << "\n";
    report << "      Browser: " << p.browser_name << " " << p.browser_version << "\n";
  }

  report << "\nContainer Configurations: " << containers_.size() << "\n";
  for (const auto& pair : containers_) {
    const auto* info = pair.second.get();
    report << "  - " << pair.first << ":\n";
    if (!info->config.profile_id.empty()) {
      report << "      Profile: " << info->config.profile_id << "\n";
    }
    if (info->config.use_custom_ua) {
      report << "      Custom UA: " << info->config.custom_user_agent.substr(0, 50) << "...\n";
    }
    report << "      Effective UA: " << info->effective_user_agent.substr(0, 50) << "...\n";
    report << "      Domain Overrides: " << info->config.domain_overrides.size() << "\n";
  }

  return report.str();
}

void ContainerUserAgentManager::DumpStateToLog() const {
  ;
}

void ContainerUserAgentManager::UpdateEffectiveValues(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = containers_.find(container_id);
  if (it == containers_.end()) {
    return;
  }

  ContainerInfo* info = it->second.get();

  if (info->config.use_custom_ua && !info->config.custom_user_agent.empty()) {
    info->effective_user_agent = info->config.custom_user_agent;

    if (info->config.use_custom_client_hints) {
      info->effective_client_hints = info->config.custom_client_hints;
    } else {
      info->effective_client_hints.clear();
    }
  } else if (!info->config.profile_id.empty()) {
    auto profile_it = profiles_.find(info->config.profile_id);
    if (profile_it != profiles_.end()) {
      info->effective_user_agent = profile_it->second.GenerateUserAgent();
      info->effective_client_hints = profile_it->second.GenerateClientHints();
    }
  } else {
    info->effective_user_agent.clear();
    info->effective_client_hints.clear();
  }

  NotifyUserAgentChanged(container_id, info->effective_user_agent);
}

void ContainerUserAgentManager::InitializeBuiltinProfiles() {

  {
    UserAgentProfile profile;
    profile.profile_id = "windows_chrome_latest";
    profile.name = "Windows Chrome (Latest)";
    profile.description = "Latest Chrome on Windows 10/11";
    profile.is_builtin = true;
    profile.device_type = UADeviceType::kDesktop;
    profile.os = UAOperatingSystem::kWindows;
    profile.os_version = "10.0";
    profile.browser_name = "Chrome";
    profile.browser_version = "126.0.0.0";
    profile.browser_major_version = "126";
    profile.engine = UABrowserEngine::kBlink;
    profile.engine_version = "126.0.0.0";
    profiles_[profile.profile_id] = profile;
  }

  {
    UserAgentProfile profile;
    profile.profile_id = "macos_chrome_latest";
    profile.name = "macOS Chrome (Latest)";
    profile.description = "Latest Chrome on macOS";
    profile.is_builtin = true;
    profile.device_type = UADeviceType::kDesktop;
    profile.os = UAOperatingSystem::kMacOS;
    profile.os_version = "14_0_0";
    profile.browser_name = "Chrome";
    profile.browser_version = "126.0.0.0";
    profile.browser_major_version = "126";
    profile.engine = UABrowserEngine::kBlink;
    profile.engine_version = "126.0.0.0";
    profiles_[profile.profile_id] = profile;
  }

  {
    UserAgentProfile profile;
    profile.profile_id = "linux_chrome_latest";
    profile.name = "Linux Chrome (Latest)";
    profile.description = "Latest Chrome on Linux";
    profile.is_builtin = true;
    profile.device_type = UADeviceType::kDesktop;
    profile.os = UAOperatingSystem::kLinux;
    profile.os_version = "x86_64";
    profile.browser_name = "Chrome";
    profile.browser_version = "126.0.0.0";
    profile.browser_major_version = "126";
    profile.engine = UABrowserEngine::kBlink;
    profile.engine_version = "126.0.0.0";
    profiles_[profile.profile_id] = profile;
  }

  {
    UserAgentProfile profile;
    profile.profile_id = "android_chrome_mobile";
    profile.name = "Android Chrome Mobile";
    profile.description = "Chrome on Android smartphone";
    profile.is_builtin = true;
    profile.device_type = UADeviceType::kMobile;
    profile.os = UAOperatingSystem::kAndroid;
    profile.os_version = "14";
    profile.device_model = "Pixel 8";
    profile.device_vendor = "Google";
    profile.browser_name = "Chrome";
    profile.browser_version = "126.0.0.0";
    profile.browser_major_version = "126";
    profile.engine = UABrowserEngine::kBlink;
    profile.engine_version = "126.0.0.0";
    profiles_[profile.profile_id] = profile;
  }

  {
    UserAgentProfile profile;
    profile.profile_id = "ios_safari_mobile";
    profile.name = "iOS Safari Mobile";
    profile.description = "Safari on iPhone";
    profile.is_builtin = true;
    profile.device_type = UADeviceType::kMobile;
    profile.os = UAOperatingSystem::kIOS;
    profile.os_version = "17_0";
    profile.device_model = "iPhone";
    profile.device_vendor = "Apple";
    profile.browser_name = "Safari";
    profile.browser_version = "17.0";
    profile.browser_major_version = "17";
    profile.engine = UABrowserEngine::kWebKit;
    profile.engine_version = "605.1.15";
    profiles_[profile.profile_id] = profile;
  }

  {
    UserAgentProfile profile;
    profile.profile_id = "android_tablet";
    profile.name = "Android Tablet";
    profile.description = "Chrome on Android tablet";
    profile.is_builtin = true;
    profile.device_type = UADeviceType::kTablet;
    profile.os = UAOperatingSystem::kAndroid;
    profile.os_version = "13";
    profile.device_model = "SM-X710";
    profile.device_vendor = "Samsung";
    profile.browser_name = "Chrome";
    profile.browser_version = "126.0.0.0";
    profile.browser_major_version = "126";
    profile.engine = UABrowserEngine::kBlink;
    profile.engine_version = "126.0.0.0";
    profiles_[profile.profile_id] = profile;
  }

  {
    UserAgentProfile profile;
    profile.profile_id = "ipad_safari";
    profile.name = "iPad Safari";
    profile.description = "Safari on iPad";
    profile.is_builtin = true;
    profile.device_type = UADeviceType::kTablet;
    profile.os = UAOperatingSystem::kIOS;
    profile.os_version = "17_0";
    profile.device_model = "iPad";
    profile.device_vendor = "Apple";
    profile.browser_name = "Safari";
    profile.browser_version = "17.0";
    profile.browser_major_version = "17";
    profile.engine = UABrowserEngine::kWebKit;
    profile.engine_version = "605.1.15";
    profiles_[profile.profile_id] = profile;
  }

  ;
}

void ContainerUserAgentManager::NotifyUserAgentChanged(
    const std::string& container_id,
    const std::string& user_agent) {
  for (auto& observer : observers_) {
    observer.OnUserAgentChanged(container_id, user_agent);
  }
}

void ContainerUserAgentManager::NotifyProfileApplied(
    const std::string& container_id,
    const std::string& profile_id) {
  for (auto& observer : observers_) {
    observer.OnProfileApplied(container_id, profile_id);
  }
}

}  
