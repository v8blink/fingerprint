
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_PROXY_MANAGER_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_PROXY_MANAGER_H_

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
#include "base/timer/timer.h"
#include "net/base/auth.h"
#include "net/base/proxy_server.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
class WebContents;
}  

namespace net {
class ProxyInfo;
}  

namespace tab_container {

enum class ContainerProxyState {

  kNoProxy = 0,

  kConfigured = 1,

  kConnecting = 2,

  kConnected = 3,

  kAuthRequired = 4,

  kAuthenticating = 5,

  kDegraded = 6,

  kFailed = 7,

  kDisconnected = 8,

  kReconfiguring = 9,
};

struct ContainerProxyConfig {

  bool enabled = false;
  std::string proxy_scheme;  
  std::string proxy_host;
  int proxy_port = 0;

  struct AuthConfig {
    bool required = false;
    std::string username;
    std::string password;
    std::string auth_scheme;  
  };
  AuthConfig auth_config;

  std::vector<std::string> bypass_rules;
  bool bypass_local = true;

  struct PacConfig {
    bool enabled = false;
    std::string pac_url;
    std::string pac_script;  
  };
  PacConfig pac_config;

  struct AdvancedConfig {
    int connect_timeout_seconds = 30;
    int read_timeout_seconds = 60;
    int max_retries = 3;
    bool allow_direct_fallback = false;  
    bool dns_over_proxy = true;  
    bool enable_keep_alive = true;
    int keep_alive_timeout_seconds = 120;
  };
  AdvancedConfig advanced_config;

  std::string GetProxyServer() const;

  bool IsSocksProxy() const;

  std::string Serialize() const;
  static std::optional<ContainerProxyConfig> Deserialize(const std::string& data);

  ContainerProxyConfig() = default;
};

struct ProxyOperationResult {
  bool success = false;
  std::string error_code;
  std::string error_message;
  base::TimeDelta duration;

  int http_status_code = 0;

  bool auth_accepted = false;
  std::string auth_realm;

  ProxyOperationResult() = default;
};

struct ProxyRequestTrace {
  enum class TraceEvent {
    kProxyResolution,
    kProxyConnect,
    kProxyAuth,
    kProxyRetry,
    kProxyFallback,
    kDirectConnection,
    kRequestComplete,
    kRequestFailed,
  };

  TraceEvent event;
  base::TimeTicks timestamp;
  std::string container_id;
  std::string url;
  std::string proxy_server;
  std::string details;
  bool is_violation = false;  

  ProxyRequestTrace() = default;
};

class ContainerProxyObserver : public base::CheckedObserver {
 public:
  ~ContainerProxyObserver() override = default;

  virtual void OnProxyStateChanged(const std::string& container_id,
                                   ContainerProxyState old_state,
                                   ContainerProxyState new_state) {}

  virtual void OnProxyConnected(const std::string& container_id,
                                const std::string& proxy_server) {}

  virtual void OnProxyConnectionFailed(const std::string& container_id,
                                       const std::string& error) {}

  virtual void OnProxyAuthRequired(const std::string& container_id,
                                   const std::string& realm) {}

  virtual void OnProxyAuthFailed(const std::string& container_id,
                                 const std::string& error) {}

  virtual void OnProxyBypassDetected(const std::string& container_id,
                                     const std::string& url,
                                     const std::string& reason) {}

  virtual void OnProxyRequestTraced(const ProxyRequestTrace& trace) {}
};

class ContainerProxyManager {
 public:
  explicit ContainerProxyManager(content::BrowserContext* browser_context);
  ~ContainerProxyManager();

  ContainerProxyManager(const ContainerProxyManager&) = delete;
  ContainerProxyManager& operator=(const ContainerProxyManager&) = delete;

  void AddObserver(ContainerProxyObserver* observer);
  void RemoveObserver(ContainerProxyObserver* observer);

  bool SetProxyConfig(const std::string& container_id,
                      const ContainerProxyConfig& config);

  std::optional<ContainerProxyConfig> GetProxyConfig(
      const std::string& container_id) const;

  bool RemoveProxyConfig(const std::string& container_id);

  bool UpdateProxyServer(const std::string& container_id,
                         const std::string& proxy_scheme,
                         const std::string& proxy_host,
                         int proxy_port);

  bool UpdateProxyAuth(const std::string& container_id,
                       const std::string& username,
                       const std::string& password);

  bool UpdateProxyBypassRules(const std::string& container_id,
                              const std::vector<std::string>& rules);

  ContainerProxyState GetProxyState(const std::string& container_id) const;

  bool IsProxyEnabled(const std::string& container_id) const;

  bool IsProxyConnected(const std::string& container_id) const;

  using ConnectionTestCallback = base::OnceCallback<void(ProxyOperationResult)>;
  void TestProxyConnection(const std::string& container_id,
                           ConnectionTestCallback callback);

  void ReconnectProxy(const std::string& container_id);

  void DisconnectProxy(const std::string& container_id);

  bool ProvideProxyAuth(const std::string& container_id,
                        const std::string& username,
                        const std::string& password);

  struct PendingAuthChallenge {
    std::string container_id;
    std::string proxy_server;
    std::string realm;
    std::string scheme;
    base::TimeTicks received_at;
  };
  std::vector<PendingAuthChallenge> GetPendingAuthChallenges() const;

  void CancelProxyAuth(const std::string& container_id);

  struct ProxyResolutionResult {
    bool success = false;
    bool use_proxy = false;
    std::string proxy_server;
    std::string resolution_path;  
    bool is_direct_allowed = false;
    std::string error_message;
  };
  ProxyResolutionResult ResolveProxy(const std::string& container_id,
                                     const GURL& url);

  ProxyResolutionResult ResolveProxyForWebContents(
      content::WebContents* web_contents,
      const GURL& url);

  struct ProxyEnforcementResult {
    bool valid = false;
    bool proxy_expected = false;
    bool proxy_used = false;
    std::string expected_proxy;
    std::string actual_proxy;
    std::string violation_reason;
  };
  ProxyEnforcementResult ValidateProxyUsage(
      const std::string& container_id,
      const GURL& url,
      const std::string& used_proxy);

  ProxyEnforcementResult ValidateProxyUsageForWebContents(
      content::WebContents* web_contents,
      const GURL& url,
      const std::string& used_proxy);

  void ReportDirectConnectionViolation(const std::string& container_id,
                                       const GURL& url,
                                       const std::string& reason);

  bool ShouldBlockDirectConnection(const std::string& container_id) const;

  void OnProxyConnectionFailure(const std::string& container_id,
                                int error_code,
                                const std::string& error_message);

  void OnProxyAuthFailure(const std::string& container_id,
                          int error_code,
                          const std::string& error_message);

  void OnProxyRequestFailure(const std::string& container_id,
                             const GURL& url,
                             int error_code,
                             const std::string& error_message);

  struct FailureStats {
    size_t connection_failures = 0;
    size_t auth_failures = 0;
    size_t request_failures = 0;
    size_t bypass_violations = 0;
    base::TimeTicks last_failure;
    std::string last_error;
  };
  FailureStats GetFailureStats(const std::string& container_id) const;

  void SetTracingEnabled(bool enabled);

  void RecordTrace(const ProxyRequestTrace& trace);

  std::vector<ProxyRequestTrace> GetRecentTraces(size_t count = 100) const;

  std::vector<ProxyRequestTrace> GetTracesForContainer(
      const std::string& container_id,
      size_t count = 100) const;

  std::vector<ProxyRequestTrace> GetViolationTraces() const;

  void ClearTraces();

  void SetDebugLoggingEnabled(bool enabled);

  std::string GetDiagnosticReport() const;

  void DumpStateToLog() const;

  struct Statistics {
    size_t total_containers_configured = 0;
    size_t active_proxy_connections = 0;
    size_t total_requests_proxied = 0;
    size_t total_bypass_violations = 0;
    size_t total_auth_challenges = 0;
    size_t total_connection_failures = 0;
    base::TimeTicks last_activity;
  };
  Statistics GetStatistics() const;

 private:

  struct ContainerProxyInfo {
    std::string container_id;
    ContainerProxyConfig config;
    ContainerProxyState state = ContainerProxyState::kNoProxy;
    base::TimeTicks state_changed_at;
    base::TimeTicks last_activity;
    FailureStats failure_stats;
    std::optional<PendingAuthChallenge> pending_auth;

    bool connection_tested = false;
    ProxyOperationResult last_connection_result;

    ContainerProxyInfo() = default;
  };

  void TransitionState(ContainerProxyInfo* info, ContainerProxyState new_state);

  bool ShouldBypassProxy(const ContainerProxyInfo* info, const GURL& url) const;
  bool MatchesBypassRule(const std::string& rule, const GURL& url) const;

  void DoConnectionTest(const std::string& container_id);
  void OnConnectionTestComplete(const std::string& container_id,
                                ProxyOperationResult result);

  void NotifyStateChanged(const std::string& container_id,
                          ContainerProxyState old_state,
                          ContainerProxyState new_state);
  void NotifyConnected(const std::string& container_id,
                       const std::string& proxy_server);
  void NotifyConnectionFailed(const std::string& container_id,
                              const std::string& error);
  void NotifyAuthRequired(const std::string& container_id,
                          const std::string& realm);
  void NotifyBypassDetected(const std::string& container_id,
                            const std::string& url,
                            const std::string& reason);

  void LogProxyOperation(const std::string& container_id,
                         const std::string& operation,
                         const std::string& details);

  raw_ptr<content::BrowserContext> browser_context_;

  std::map<std::string, std::unique_ptr<ContainerProxyInfo>> container_proxies_;

  std::vector<ProxyRequestTrace> traces_;
  size_t max_traces_ = 1000;

  bool tracing_enabled_ = false;
  bool debug_logging_enabled_ = false;

  mutable Statistics stats_;

  std::map<std::string, ConnectionTestCallback> pending_connection_tests_;

  base::ObserverList<ContainerProxyObserver> observers_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ContainerProxyManager> weak_factory_{this};
};

class ProxyEnforcementValidator {
 public:
  ProxyEnforcementValidator(ContainerProxyManager* manager,
                            const std::string& container_id,
                            const GURL& url);
  ~ProxyEnforcementValidator();

  void OnProxyResolved(const std::string& proxy_server);

  void OnRequestSent(const std::string& actual_proxy);

  void OnRequestComplete(bool success);

  bool HasViolation() const { return has_violation_; }
  std::string GetViolationReason() const { return violation_reason_; }

 private:
  raw_ptr<ContainerProxyManager> manager_;
  std::string container_id_;
  GURL url_;
  std::string expected_proxy_;
  std::string actual_proxy_;
  bool has_violation_ = false;
  std::string violation_reason_;
  bool completed_ = false;
};

#define VALIDATE_PROXY_USAGE(manager, container_id, url) \
  tab_container::ProxyEnforcementValidator _proxy_validator_##__LINE__( \
      manager, container_id, url)

}  

#endif  
