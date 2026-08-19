
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_COOKIE_MANAGER_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_COOKIE_MANAGER_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
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

enum class CookieIsolationMode {

  kNone = 0,

  kFull = 1,

  kDomainBased = 2,

  kFirstPartyIsolation = 3,

  kDynamic = 4,
};

enum class CookiePolicyType {
  kAllowAll,           
  kBlockThirdParty,    
  kBlockAll,           
  kSessionOnly,        
  kFirstPartyOnly,     
  kWhitelistOnly,      
};

enum class SameSitePolicyOverride {
  kNone,        
  kStrict,      
  kLax,         
  kUnspecified, 
};

struct ContainerCookieConfig {
  std::string container_id;
  CookieIsolationMode isolation_mode = CookieIsolationMode::kFull;
  CookiePolicyType policy = CookiePolicyType::kBlockThirdParty;

  bool clear_on_exit = false;
  bool clear_on_container_close = true;
  base::TimeDelta max_cookie_age = base::Days(365);
  bool honor_cookie_expiry = true;

  bool block_third_party_cookies = true;
  std::set<std::string> third_party_exceptions;

  std::set<std::string> allowed_domains;
  std::set<std::string> blocked_domains;

  bool require_secure = false;      
  bool require_same_site = false;   
  SameSitePolicyOverride same_site_override = SameSitePolicyOverride::kNone;

  size_t max_cookies_per_domain = 180;
  size_t max_total_cookies = 3000;
  size_t max_cookie_size_bytes = 4096;

  bool strip_tracking_params = false;
  bool randomize_cookie_order = false;

  ContainerCookieConfig() = default;
};

struct ContainerCookie {
  std::string name;
  std::string value;
  std::string domain;
  std::string path;
  base::Time creation_time;
  base::Time expiry_time;
  base::Time last_access_time;
  bool secure = false;
  bool http_only = false;
  std::string same_site;  
  std::string source_scheme;
  int source_port = 0;

  bool IsExpired() const;
  bool IsSession() const;
  size_t GetSizeBytes() const;
  std::string GetFullDomain() const;

  ContainerCookie() = default;
};

struct ContainerCookieJar {
  std::string container_id;
  ContainerCookieConfig config;
  std::vector<ContainerCookie> cookies;

  base::Time created_at;
  base::Time last_modified;

  size_t cookies_set = 0;
  size_t cookies_blocked = 0;
  size_t cookies_expired = 0;
  size_t third_party_blocked = 0;

  size_t GetTotalSize() const;
  size_t GetCookieCount() const;
  std::vector<ContainerCookie> GetCookiesForDomain(
      const std::string& domain) const;

  ContainerCookieJar() = default;
};

enum class CookieEventType {
  kSet,
  kDeleted,
  kBlocked,
  kExpired,
  kModified,
  kCleared,
};

struct CookieEvent {
  base::TimeTicks timestamp;
  std::string container_id;
  CookieEventType type;
  std::string domain;
  std::string cookie_name;
  std::string reason;
  bool is_third_party;
  std::string top_frame_origin;
};

class ContainerCookieObserver : public base::CheckedObserver {
 public:
  ~ContainerCookieObserver() override = default;

  virtual void OnCookieSet(const std::string& container_id,
                           const ContainerCookie& cookie,
                           bool is_third_party) {}
  virtual void OnCookieBlocked(const std::string& container_id,
                               const std::string& domain,
                               const std::string& reason,
                               bool is_third_party) {}
  virtual void OnCookieDeleted(const std::string& container_id,
                               const ContainerCookie& cookie) {}
  virtual void OnCookiesCleared(const std::string& container_id,
                                size_t count) {}
  virtual void OnCookieConfigChanged(const std::string& container_id,
                                     const ContainerCookieConfig& config) {}
};

class ContainerCookieManager {
 public:
  explicit ContainerCookieManager(content::BrowserContext* browser_context);
  ~ContainerCookieManager();

  ContainerCookieManager(const ContainerCookieManager&) = delete;
  ContainerCookieManager& operator=(const ContainerCookieManager&) = delete;

  void AddObserver(ContainerCookieObserver* observer);
  void RemoveObserver(ContainerCookieObserver* observer);

  bool CreateCookieJar(const std::string& container_id,
                       const ContainerCookieConfig& config);

  ContainerCookieJar* GetOrCreateCookieJar(const std::string& container_id);

  bool DestroyCookieJar(const std::string& container_id);

  ContainerCookieJar* GetCookieJar(const std::string& container_id);
  const ContainerCookieJar* GetCookieJar(const std::string& container_id) const;

  std::vector<std::string> GetAllCookieJarContainerIds() const;

  struct SetCookieResult {
    bool success;
    std::string error;
    bool was_blocked;
    std::string block_reason;
  };
  SetCookieResult SetCookie(const std::string& container_id,
                            const ContainerCookie& cookie,
                            const std::string& top_frame_origin);

  std::vector<ContainerCookie> GetCookiesForUrl(
      const std::string& container_id,
      const std::string& url,
      const std::string& top_frame_origin) const;

  std::vector<ContainerCookie> GetAllCookies(
      const std::string& container_id) const;

  std::vector<ContainerCookie> GetCookiesForDomain(
      const std::string& container_id,
      const std::string& domain) const;

  bool DeleteCookie(const std::string& container_id,
                    const std::string& domain,
                    const std::string& name,
                    const std::string& path);

  size_t DeleteCookiesForDomain(const std::string& container_id,
                                const std::string& domain);

  size_t ClearAllCookies(const std::string& container_id);

  struct CookieAllowedResult {
    bool allowed;
    std::string reason;
    bool is_third_party;
    bool would_be_partitioned;
  };
  CookieAllowedResult IsCookieAllowed(
      const std::string& container_id,
      const ContainerCookie& cookie,
      const std::string& top_frame_origin) const;

  bool IsDomainBlocked(const std::string& container_id,
                       const std::string& domain) const;

  bool IsDomainAllowed(const std::string& container_id,
                       const std::string& domain) const;

  bool IsThirdPartyContext(const std::string& request_origin,
                           const std::string& top_frame_origin) const;

  bool UpdateCookieConfig(const std::string& container_id,
                          const ContainerCookieConfig& config);

  std::optional<ContainerCookieConfig> GetCookieConfig(
      const std::string& container_id) const;

  void SetDefaultConfig(const ContainerCookieConfig& config);

  bool AddAllowedDomain(const std::string& container_id,
                        const std::string& domain);

  bool AddBlockedDomain(const std::string& container_id,
                        const std::string& domain);

  bool RemoveAllowedDomain(const std::string& container_id,
                           const std::string& domain);
  bool RemoveBlockedDomain(const std::string& container_id,
                           const std::string& domain);

  size_t CleanupExpiredCookies(const std::string& container_id);

  size_t CleanupAllExpiredCookies();

  size_t EnforceCookieLimits(const std::string& container_id);

  size_t ClearSessionCookies(const std::string& container_id);

  std::string ExportCookies(const std::string& container_id) const;

  size_t ImportCookies(const std::string& container_id,
                       const std::string& data);

  size_t CopyCookies(const std::string& source_container_id,
                     const std::string& dest_container_id,
                     const std::string& domain_filter = "");

  struct CookieStatistics {
    size_t total_cookies = 0;
    size_t session_cookies = 0;
    size_t persistent_cookies = 0;
    size_t secure_cookies = 0;
    size_t http_only_cookies = 0;
    size_t same_site_strict = 0;
    size_t same_site_lax = 0;
    size_t same_site_none = 0;
    size_t cookies_set = 0;
    size_t cookies_blocked = 0;
    size_t third_party_blocked = 0;
    size_t total_size_bytes = 0;
    std::map<std::string, size_t> cookies_per_domain;
  };
  CookieStatistics GetStatistics(const std::string& container_id) const;

  CookieStatistics GetGlobalStatistics() const;

  std::vector<CookieEvent> GetCookieEvents(
      const std::string& container_id,
      size_t count = 100) const;

  void SetDebugLoggingEnabled(bool enabled);
  std::string GetDiagnosticReport() const;
  void DumpStateToLog() const;

 private:

  bool DomainMatches(const std::string& cookie_domain,
                     const std::string& request_domain) const;

  std::string ExtractDomain(const std::string& url) const;

  std::string GetEffectiveTLDPlusOne(const std::string& domain) const;

  bool ValidateCookie(const ContainerCookie& cookie) const;

  void RecordEvent(const std::string& container_id,
                   CookieEventType type,
                   const std::string& domain,
                   const std::string& cookie_name,
                   const std::string& reason,
                   bool is_third_party,
                   const std::string& top_frame_origin);

  void NotifyCookieSet(const std::string& container_id,
                       const ContainerCookie& cookie,
                       bool is_third_party);
  void NotifyCookieBlocked(const std::string& container_id,
                           const std::string& domain,
                           const std::string& reason,
                           bool is_third_party);
  void NotifyCookieDeleted(const std::string& container_id,
                           const ContainerCookie& cookie);
  void NotifyCookiesCleared(const std::string& container_id, size_t count);
  void NotifyConfigChanged(const std::string& container_id,
                           const ContainerCookieConfig& config);

  raw_ptr<content::BrowserContext> browser_context_;

  std::map<std::string, std::unique_ptr<ContainerCookieJar>> cookie_jars_;

  std::map<std::string, std::vector<CookieEvent>> cookie_events_;

  ContainerCookieConfig default_config_;

  bool debug_logging_enabled_ = false;

  base::ObserverList<ContainerCookieObserver> observers_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ContainerCookieManager> weak_factory_{this};
};

}  

#endif  
