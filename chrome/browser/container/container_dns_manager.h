
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_DNS_MANAGER_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_DNS_MANAGER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "net/dns/dns_config.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
class WebContents;
}  

namespace tab_container {

struct ContainerDnsConfig {
  std::string container_id;

  enum class DnsMode {
    kSystem,           
    kSecure,           
    kAutomatic,        
    kOff,              
    kCustom,           
    kProxyDns,         
  };
  DnsMode mode = DnsMode::kSystem;

  struct DohConfig {
    bool enabled = false;
    std::vector<std::string> servers;  
    std::string template_uri;          
    bool fallback_without_doh = false; 
  };
  DohConfig doh_config;

  struct CustomDnsConfig {
    std::vector<std::string> nameservers;  
    std::vector<std::string> search_domains;
    int timeout_ms = 5000;
    int attempts = 2;
  };
  CustomDnsConfig custom_dns_config;

  struct ProxyDnsConfig {
    bool route_dns_through_proxy = false;  
    std::string proxy_server;              
  };
  ProxyDnsConfig proxy_dns_config;

  struct CacheConfig {
    bool enabled = true;
    int max_entries = 1000;
    int ttl_seconds = 300;  
    int negative_ttl_seconds = 60;  
  };
  CacheConfig cache_config;

  struct PrefetchConfig {
    bool enabled = true;
    int max_concurrent = 8;
    bool allow_stale_on_failure = false;
  };
  PrefetchConfig prefetch_config;

  struct PrivacyConfig {
    bool randomize_case = false;  
    bool pad_queries = false;     
    int padding_block_size = 128;
    bool disable_ipv6 = false;    
    bool disable_additional = false;  
  };
  PrivacyConfig privacy_config;

  bool IsValid() const;

  std::string Serialize() const;
  static std::optional<ContainerDnsConfig> Deserialize(const std::string& data);

  ContainerDnsConfig() = default;
};

struct DnsResolutionResult {
  bool success = false;
  std::string hostname;
  std::vector<std::string> resolved_addresses;
  std::string error_message;
  int error_code = 0;
  base::TimeDelta resolution_time;

  enum class Source {
    kCache,
    kSystem,
    kDoH,
    kCustom,
    kProxy,
  };
  Source source = Source::kSystem;

  std::string container_id;
  std::string dns_server_used;
  bool used_secure_dns = false;

  DnsResolutionResult() = default;
};

class ContainerDnsObserver : public base::CheckedObserver {
 public:
  ~ContainerDnsObserver() override = default;

  virtual void OnDnsConfigUpdated(const std::string& container_id,
                                  const ContainerDnsConfig& config) {}

  virtual void OnDnsResolutionComplete(const DnsResolutionResult& result) {}

  virtual void OnDnsResolutionFailed(const std::string& container_id,
                                     const std::string& hostname,
                                     const std::string& error) {}

  virtual void OnSecureDnsUpgrade(const std::string& container_id,
                                  const std::string& hostname) {}

  virtual void OnDnsFallback(const std::string& container_id,
                             const std::string& hostname,
                             const std::string& reason) {}
};

class ContainerDnsManager {
 public:
  explicit ContainerDnsManager(content::BrowserContext* browser_context);
  ~ContainerDnsManager();

  ContainerDnsManager(const ContainerDnsManager&) = delete;
  ContainerDnsManager& operator=(const ContainerDnsManager&) = delete;

  void AddObserver(ContainerDnsObserver* observer);
  void RemoveObserver(ContainerDnsObserver* observer);

  bool SetDnsConfig(const std::string& container_id,
                    const ContainerDnsConfig& config);

  std::optional<ContainerDnsConfig> GetDnsConfig(
      const std::string& container_id) const;

  bool RemoveDnsConfig(const std::string& container_id);

  bool SetDnsMode(const std::string& container_id,
                  ContainerDnsConfig::DnsMode mode);

  bool EnableSecureDns(const std::string& container_id,
                       const std::vector<std::string>& doh_servers);

  bool DisableSecureDns(const std::string& container_id);

  bool SetCustomDns(const std::string& container_id,
                    const std::vector<std::string>& nameservers);

  bool EnableProxyDns(const std::string& container_id,
                      const std::string& proxy_server);

  using DnsCallback = base::OnceCallback<void(DnsResolutionResult)>;
  void ResolveHostname(const std::string& container_id,
                       const std::string& hostname,
                       DnsCallback callback);

  void ResolveHostnameForWebContents(content::WebContents* web_contents,
                                     const std::string& hostname,
                                     DnsCallback callback);

  void ClearDnsCache(const std::string& container_id);

  void ClearAllDnsCaches();

  struct CacheStats {
    size_t entries = 0;
    size_t hits = 0;
    size_t misses = 0;
    size_t evictions = 0;
    base::TimeDelta avg_ttl;
  };
  CacheStats GetCacheStats(const std::string& container_id) const;

  void PrefetchDns(const std::string& container_id,
                   const std::string& hostname);

  void PrefetchDnsBatch(const std::string& container_id,
                        const std::vector<std::string>& hostnames);

  void CancelPrefetch(const std::string& container_id);

  struct DnsValidationResult {
    bool valid = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
  };
  DnsValidationResult ValidateDnsConfig(const std::string& container_id) const;

  using DnsTestCallback = base::OnceCallback<void(bool success, 
                                                  const std::string& details)>;
  void TestDnsResolution(const std::string& container_id,
                         const std::string& test_hostname,
                         DnsTestCallback callback);

  std::string GetDiagnosticReport() const;

  void DumpStateToLog() const;

  struct Statistics {
    size_t containers_configured = 0;
    size_t total_resolutions = 0;
    size_t successful_resolutions = 0;
    size_t failed_resolutions = 0;
    size_t secure_dns_resolutions = 0;
    size_t proxy_dns_resolutions = 0;
    size_t fallback_resolutions = 0;
    base::TimeDelta avg_resolution_time;
    base::TimeTicks last_activity;
  };
  Statistics GetStatistics() const;

 private:

  struct ContainerDnsInfo {
    std::string container_id;
    ContainerDnsConfig config;
    CacheStats cache_stats;
    base::TimeTicks last_resolution;
    size_t resolution_count = 0;
    size_t failure_count = 0;

    ContainerDnsInfo() = default;
  };

  void DoResolve(const std::string& container_id,
                 const std::string& hostname,
                 DnsCallback callback);

  DnsResolutionResult::Source DetermineResolutionSource(
      const ContainerDnsConfig& config) const;

  void NotifyConfigUpdated(const std::string& container_id,
                           const ContainerDnsConfig& config);
  void NotifyResolutionComplete(const DnsResolutionResult& result);
  void NotifyResolutionFailed(const std::string& container_id,
                              const std::string& hostname,
                              const std::string& error);

  raw_ptr<content::BrowserContext> browser_context_;

  std::map<std::string, std::unique_ptr<ContainerDnsInfo>> container_dns_;

  mutable Statistics stats_;

  base::ObserverList<ContainerDnsObserver> observers_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ContainerDnsManager> weak_factory_{this};
};

}  

#endif  
