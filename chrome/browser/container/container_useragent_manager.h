
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_USERAGENT_MANAGER_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_USERAGENT_MANAGER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"

namespace content {
class BrowserContext;
}  

namespace tab_container {

enum class UADeviceType {
  kDesktop,
  kMobile,
  kTablet,
  kSmartTV,
  kConsole,
  kWearable,
  kEmbedded,
};

enum class UAOperatingSystem {
  kWindows,
  kMacOS,
  kLinux,
  kAndroid,
  kIOS,
  kChromeOS,
  kUnknown,
};

enum class UABrowserEngine {
  kBlink,
  kGecko,
  kWebKit,
  kUnknown,
};

struct UserAgentProfile {
  std::string profile_id;
  std::string name;
  std::string description;

  UADeviceType device_type = UADeviceType::kDesktop;
  UAOperatingSystem os = UAOperatingSystem::kWindows;
  std::string os_version;
  std::string device_model;
  std::string device_vendor;

  std::string browser_name;
  std::string browser_version;
  std::string browser_major_version;
  UABrowserEngine engine = UABrowserEngine::kBlink;
  std::string engine_version;

  std::string user_agent;
  std::string sec_ch_ua;
  std::string sec_ch_ua_mobile;
  std::string sec_ch_ua_platform;
  std::string sec_ch_ua_platform_version;
  std::string sec_ch_ua_arch;
  std::string sec_ch_ua_bitness;
  std::string sec_ch_ua_model;
  std::string sec_ch_ua_full_version_list;

  bool is_builtin = false;
  base::Time created_at;
  base::Time last_used;

  std::string GenerateUserAgent() const;

  std::map<std::string, std::string> GenerateClientHints() const;

  UserAgentProfile() = default;
};

struct ContainerUserAgentConfig {
  std::string container_id;

  std::string profile_id;

  bool use_custom_ua = false;
  std::string custom_user_agent;

  bool use_custom_client_hints = false;
  std::map<std::string, std::string> custom_client_hints;

  bool spoof_accept_language = false;
  std::string spoofed_accept_language;

  bool spoof_platform = false;
  std::string spoofed_platform;

  bool reduce_user_agent = false;

  std::map<std::string, std::string> domain_overrides;

  ContainerUserAgentConfig() = default;
};

class ContainerUserAgentObserver : public base::CheckedObserver {
 public:
  ~ContainerUserAgentObserver() override = default;

  virtual void OnUserAgentChanged(const std::string& container_id,
                                  const std::string& user_agent) {}
  virtual void OnProfileApplied(const std::string& container_id,
                                const std::string& profile_id) {}
};

class ContainerUserAgentManager {
 public:
  explicit ContainerUserAgentManager(content::BrowserContext* browser_context);
  ~ContainerUserAgentManager();

  ContainerUserAgentManager(const ContainerUserAgentManager&) = delete;
  ContainerUserAgentManager& operator=(const ContainerUserAgentManager&) = delete;

  void AddObserver(ContainerUserAgentObserver* observer);
  void RemoveObserver(ContainerUserAgentObserver* observer);

  bool CreateProfile(const UserAgentProfile& profile);

  std::optional<UserAgentProfile> GetProfile(const std::string& profile_id) const;

  std::vector<UserAgentProfile> GetAllProfiles() const;

  std::vector<UserAgentProfile> GetBuiltinProfiles() const;

  std::vector<UserAgentProfile> GetProfilesByDeviceType(
      UADeviceType device_type) const;

  std::vector<UserAgentProfile> GetProfilesByOS(
      UAOperatingSystem os) const;

  bool DeleteProfile(const std::string& profile_id);

  bool UpdateProfile(const UserAgentProfile& profile);

  bool SetContainerConfig(const std::string& container_id,
                          const ContainerUserAgentConfig& config);

  std::optional<ContainerUserAgentConfig> GetContainerConfig(
      const std::string& container_id) const;

  bool RemoveContainerConfig(const std::string& container_id);

  bool ApplyProfileToContainer(const std::string& container_id,
                               const std::string& profile_id);

  bool SetCustomUserAgent(const std::string& container_id,
                          const std::string& user_agent);

  std::string GetUserAgent(const std::string& container_id) const;

  std::string GetUserAgentForUrl(const std::string& container_id,
                                 const std::string& url) const;

  std::map<std::string, std::string> GetClientHints(
      const std::string& container_id) const;

  std::string GetAcceptLanguage(const std::string& container_id) const;

  std::string GetNavigatorPlatform(const std::string& container_id) const;

  bool SetDomainOverride(const std::string& container_id,
                         const std::string& domain,
                         const std::string& user_agent);

  bool RemoveDomainOverride(const std::string& container_id,
                            const std::string& domain);

  std::map<std::string, std::string> GetDomainOverrides(
      const std::string& container_id) const;

  UserAgentProfile GenerateRandomProfile() const;

  UserAgentProfile GenerateRandomProfile(UADeviceType device_type) const;

  bool ApplyRandomProfile(const std::string& container_id);

  bool ApplyRandomProfile(const std::string& container_id,
                          UADeviceType device_type);

  size_t GetProfileCount() const;

  size_t GetContainerConfigCount() const;

  void SetDebugLoggingEnabled(bool enabled);
  std::string GetDiagnosticReport() const;
  void DumpStateToLog() const;

 private:

  struct ContainerInfo {
    std::string container_id;
    ContainerUserAgentConfig config;
    std::string effective_user_agent;
    std::map<std::string, std::string> effective_client_hints;
  };

  void UpdateEffectiveValues(const std::string& container_id);

  void InitializeBuiltinProfiles();

  void NotifyUserAgentChanged(const std::string& container_id,
                              const std::string& user_agent);
  void NotifyProfileApplied(const std::string& container_id,
                            const std::string& profile_id);

  raw_ptr<content::BrowserContext> browser_context_;

  std::map<std::string, UserAgentProfile> profiles_;

  std::map<std::string, std::unique_ptr<ContainerInfo>> containers_;

  bool debug_logging_enabled_ = false;

  base::ObserverList<ContainerUserAgentObserver> observers_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ContainerUserAgentManager> weak_factory_{this};
};

}  

#endif  
