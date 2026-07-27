
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_NETWORK_CONTEXT_MANAGER_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_NETWORK_CONTEXT_MANAGER_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"

namespace content {
class BrowserContext;
class StoragePartition;
class WebContents;
}  

namespace network {
class SharedURLLoaderFactory;
namespace mojom {
class NetworkContext;
}  
}  

namespace tab_container {

enum class NetworkContextState {

  kNotCreated = 0,

  kCreating = 1,

  kConfiguring = 2,

  kActive = 3,

  kReconfiguring = 4,

  kDestroying = 5,

  kDestroyed = 6,

  kError = 7,
};

struct NetworkRequestTrace {
  enum class TraceType {
    kNavigation,
    kSubresource,
    kRedirect,
    kServiceWorker,
    kPrefetch,
    kCached,
    kWebSocket,
    kFetch,
  };

  TraceType type;
  base::TimeTicks timestamp;
  std::string container_id;
  std::string partition_id;
  std::string url;
  std::string method;
  std::string network_context_id;
  bool used_correct_context = true;
  std::string fallback_reason;  

  int status_code = 0;
  std::string response_headers;
  base::TimeDelta request_duration;

  NetworkRequestTrace() = default;
};

struct ContainerNetworkConfig {
  std::string container_id;
  std::string partition_id;

  struct ProxyConfig {
    bool enabled = false;
    std::string proxy_server;
    std::string bypass_rules;
    std::string pac_url;
    bool auto_detect = false;
  };
  ProxyConfig proxy_config;

  struct DnsConfig {
    bool secure_dns_enabled = false;
    std::string secure_dns_mode;
    std::vector<std::string> dns_over_https_servers;
    bool dns_prefetch_enabled = true;
  };
  DnsConfig dns_config;

  struct UserAgentConfig {
    std::string user_agent_override;
    std::string accept_language;
    bool reduce_user_agent = false;
  };
  UserAgentConfig user_agent_config;

  struct CertConfig {
    bool ignore_certificate_errors = false;
    std::vector<std::string> additional_root_certs;
    bool enable_certificate_transparency = true;
  };
  CertConfig cert_config;

  struct IsolationConfig {
    bool strict_isolation = true;
    bool block_cross_container_requests = true;
    bool allow_ambient_authentication = false;
  };
  IsolationConfig isolation_config;

  ContainerNetworkConfig() = default;
};

class ContainerNetworkContextObserver : public base::CheckedObserver {
 public:
  ~ContainerNetworkContextObserver() override = default;

  virtual void OnNetworkContextCreated(const std::string& container_id,
                                       const std::string& partition_id) {}

  virtual void OnNetworkContextStateChanged(
      const std::string& container_id,
      NetworkContextState old_state,
      NetworkContextState new_state) {}

  virtual void OnNetworkRequestTraced(const NetworkRequestTrace& trace) {}

  virtual void OnNetworkContextFallback(const std::string& container_id,
                                        const std::string& url,
                                        const std::string& reason) {}

  virtual void OnNetworkContextDestroyed(const std::string& container_id) {}

  virtual void OnNetworkConfigUpdated(const std::string& container_id,
                                      const ContainerNetworkConfig& config) {}
};

class ContainerNetworkContextManager {
 public:
  explicit ContainerNetworkContextManager(
      content::BrowserContext* browser_context);
  ~ContainerNetworkContextManager();

  ContainerNetworkContextManager(const ContainerNetworkContextManager&) = delete;
  ContainerNetworkContextManager& operator=(
      const ContainerNetworkContextManager&) = delete;

  void AddObserver(ContainerNetworkContextObserver* observer);
  void RemoveObserver(ContainerNetworkContextObserver* observer);

  bool CreateNetworkContextForContainer(
      const std::string& container_id,
      const std::string& partition_id,
      content::StoragePartition* storage_partition);

  bool CreateNetworkContextWithConfig(
      const std::string& container_id,
      const std::string& partition_id,
      content::StoragePartition* storage_partition,
      const ContainerNetworkConfig& config);

  network::mojom::NetworkContext* GetNetworkContext(
      const std::string& container_id);

  network::mojom::NetworkContext* GetNetworkContextForWebContents(
      content::WebContents* web_contents);

  network::mojom::URLLoaderFactory* GetURLLoaderFactory(
      const std::string& container_id);

  bool HasNetworkContext(const std::string& container_id) const;

  NetworkContextState GetNetworkContextState(
      const std::string& container_id) const;

  void DestroyNetworkContext(const std::string& container_id);

  void DestroyAllNetworkContexts();

  bool UpdateNetworkConfig(const std::string& container_id,
                           const ContainerNetworkConfig& config);

  std::optional<ContainerNetworkConfig> GetNetworkConfig(
      const std::string& container_id) const;

  bool SetProxyConfig(const std::string& container_id,
                      const ContainerNetworkConfig::ProxyConfig& proxy_config);

  bool SetDnsConfig(const std::string& container_id,
                    const ContainerNetworkConfig::DnsConfig& dns_config);

  bool SetUserAgentConfig(
      const std::string& container_id,
      const ContainerNetworkConfig::UserAgentConfig& user_agent_config);

  struct NetworkContextResolution {
    bool success = false;
    std::string container_id;
    std::string partition_id;
    network::mojom::NetworkContext* context = nullptr;
    std::string resolution_path;  
    bool used_fallback = false;
    std::string fallback_reason;
  };
  NetworkContextResolution ResolveNetworkContext(
      content::WebContents* web_contents);

  struct RequestValidation {
    bool valid = false;
    std::string expected_container_id;
    std::string actual_container_id;
    std::string validation_message;
  };
  RequestValidation ValidateRequest(
      content::WebContents* web_contents,
      network::mojom::NetworkContext* used_context);

  void SetRequestTracingEnabled(bool enabled);

  void RecordRequestTrace(const NetworkRequestTrace& trace);

  std::vector<NetworkRequestTrace> GetRecentTraces(size_t count = 100) const;

  std::vector<NetworkRequestTrace> GetTracesForContainer(
      const std::string& container_id,
      size_t count = 100) const;

  std::vector<NetworkRequestTrace> GetFallbackTraces() const;

  void ClearTraces();

  void SetDebugLoggingEnabled(bool enabled);

  void DumpNetworkContextState() const;

  std::string GetDiagnosticReport() const;

  struct Statistics {
    size_t total_contexts_created = 0;
    size_t active_contexts = 0;
    size_t contexts_destroyed = 0;
    size_t total_requests_traced = 0;
    size_t fallback_requests = 0;
    size_t config_updates = 0;
    base::TimeTicks last_activity;
  };
  Statistics GetStatistics() const;

 private:

  struct ContainerNetworkContextInfo {
    std::string container_id;
    std::string partition_id;
    NetworkContextState state = NetworkContextState::kNotCreated;
    ContainerNetworkConfig config;
    raw_ptr<content::StoragePartition> storage_partition = nullptr;
    mojo::Remote<network::mojom::NetworkContext> network_context;
    mojo::Remote<network::mojom::URLLoaderFactory> url_loader_factory;
    base::TimeTicks created_at;
    base::TimeTicks last_used;
    std::string network_context_id;  

    ContainerNetworkContextInfo() = default;
  };

  network::mojom::NetworkContextParamsPtr CreateNetworkContextParams(
      const ContainerNetworkConfig& config);

  void ApplyProxyConfig(ContainerNetworkContextInfo* info);

  void ApplyDnsConfig(ContainerNetworkContextInfo* info);

  void TransitionState(ContainerNetworkContextInfo* info,
                       NetworkContextState new_state);

  void NotifyContextCreated(const std::string& container_id,
                            const std::string& partition_id);
  void NotifyStateChanged(const std::string& container_id,
                          NetworkContextState old_state,
                          NetworkContextState new_state);
  void NotifyRequestTraced(const NetworkRequestTrace& trace);
  void NotifyFallback(const std::string& container_id,
                      const std::string& url,
                      const std::string& reason);

  void LogContextOperation(const std::string& container_id,
                           const std::string& operation,
                           const std::string& details);
  void LogResolution(const std::string& web_contents_id,
                     const NetworkContextResolution& resolution);

  raw_ptr<content::BrowserContext> browser_context_;

  std::map<std::string, std::unique_ptr<ContainerNetworkContextInfo>>
      container_contexts_;

  std::map<std::string, std::string> partition_to_container_;

  std::vector<NetworkRequestTrace> request_traces_;
  size_t max_traces_ = 1000;

  bool request_tracing_enabled_ = false;
  bool debug_logging_enabled_ = false;

  mutable Statistics stats_;

  base::ObserverList<ContainerNetworkContextObserver> observers_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ContainerNetworkContextManager> weak_factory_{this};
};

class NetworkContextTracer {
 public:
  NetworkContextTracer(ContainerNetworkContextManager* manager,
                       const std::string& container_id,
                       const std::string& url,
                       NetworkRequestTrace::TraceType type);
  ~NetworkContextTracer();

  void SetMethod(const std::string& method);
  void SetStatusCode(int status_code);
  void SetResponseHeaders(const std::string& headers);
  void MarkFallback(const std::string& reason);
  void Complete();

 private:
  raw_ptr<ContainerNetworkContextManager> manager_;
  NetworkRequestTrace trace_;
  base::TimeTicks start_time_;
  bool completed_ = false;
};

#define TRACE_NETWORK_REQUEST(manager, container_id, url, type) \
  tab_container::NetworkContextTracer _tracer_##__LINE__( \
      manager, container_id, url, type)

}  

#endif  
