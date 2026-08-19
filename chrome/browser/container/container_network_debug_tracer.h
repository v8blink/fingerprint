
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_NETWORK_DEBUG_TRACER_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_NETWORK_DEBUG_TRACER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/singleton.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/synchronization/lock.h"
#include "base/time/time.h"
#include "url/gurl.h"

namespace content {
class WebContents;
class StoragePartition;
}  

namespace network {
namespace mojom {
class NetworkContext;
}  
}  

namespace tab_container {

struct NetworkDebugTraceEvent {

  enum class EventType {

    kTabCreated,
    kContainerCreated,
    kPartitionCreated,
    kNetworkContextCreated,
    kProxyConfigured,

    kRequestStarted,
    kProxyResolved,
    kNetworkContextResolved,
    kConnectionStarted,
    kDnsLookup,
    kTlsHandshake,
    kRequestSent,
    kResponseHeadersReceived,
    kResponseBodyReceived,
    kRequestComplete,
    kRequestFailed,

    kRedirectReceived,
    kRedirectFollowed,

    kCacheHit,
    kCacheMiss,
    kCacheWrite,

    kServiceWorkerIntercept,
    kServiceWorkerFetch,
    kServiceWorkerResponse,

    kPartitionValidation,
    kNetworkContextValidation,
    kProxyEnforcement,
    kIsolationCheck,

    kPartitionMismatch,
    kNetworkContextMismatch,
    kProxyBypass,
    kIsolationViolation,

    kContainerDestroying,
    kPartitionDestroying,
    kNetworkContextDestroying,
    kTabClosed,
  };

  enum class Severity {
    kTrace,     
    kDebug,     
    kInfo,      
    kWarning,   
    kError,     
    kCritical,  
  };

  EventType type;
  Severity severity;
  base::TimeTicks timestamp;

  std::string request_id;       
  std::string container_id;
  std::string partition_id;
  std::string network_context_id;

  std::string url;
  std::string method;
  int status_code = 0;

  std::string proxy_server;
  bool proxy_expected = false;
  bool proxy_used = false;

  base::TimeTicks phase_start;
  base::TimeDelta phase_duration;

  std::string message;
  std::string stack_trace;

  bool validation_passed = true;
  std::string validation_error;

  std::string ToString() const;

  NetworkDebugTraceEvent() = default;
};

class NetworkDebugTraceObserver : public base::CheckedObserver {
 public:
  ~NetworkDebugTraceObserver() override = default;

  virtual void OnTraceEvent(const NetworkDebugTraceEvent& event) {}

  virtual void OnViolationDetected(const NetworkDebugTraceEvent& event) {}

  virtual void OnRequestFlowComplete(const std::string& request_id,
                                     bool success,
                                     const std::string& summary) {}
};

struct DebugTracerConfig {
  bool enabled = false;
  bool trace_all_requests = false;
  bool trace_proxy_only = false;
  bool trace_errors_only = false;
  bool include_headers = false;
  bool include_body_preview = false;
  bool include_stack_traces = false;
  size_t max_traces_per_container = 1000;
  size_t max_total_traces = 10000;
  std::vector<std::string> url_filters;  
  std::vector<std::string> container_filters;  

  DebugTracerConfig() = default;
};

class ContainerNetworkDebugTracer {
 public:
  static ContainerNetworkDebugTracer* GetInstance();

  ContainerNetworkDebugTracer(const ContainerNetworkDebugTracer&) = delete;
  ContainerNetworkDebugTracer& operator=(const ContainerNetworkDebugTracer&) = delete;

  void SetConfig(const DebugTracerConfig& config);
  DebugTracerConfig GetConfig() const;
  void Enable();
  void Disable();
  bool IsEnabled() const;

  void AddObserver(NetworkDebugTraceObserver* observer);
  void RemoveObserver(NetworkDebugTraceObserver* observer);

  void RecordEvent(const NetworkDebugTraceEvent& event);

  void TraceTabCreated(content::WebContents* web_contents,
                       const std::string& container_id);

  void TraceContainerCreated(const std::string& container_id,
                             const std::string& partition_id);

  void TracePartitionCreated(const std::string& partition_id,
                             content::StoragePartition* partition);

  void TraceNetworkContextCreated(const std::string& container_id,
                                  const std::string& network_context_id);

  void TraceProxyConfigured(const std::string& container_id,
                            const std::string& proxy_server);

  std::string TraceRequestStarted(const std::string& container_id,
                                  const GURL& url,
                                  const std::string& method);

  void TraceProxyResolved(const std::string& request_id,
                          const std::string& proxy_server,
                          bool is_direct);

  void TraceNetworkContextResolved(const std::string& request_id,
                                   const std::string& network_context_id,
                                   bool matched_expected);

  void TraceConnectionStarted(const std::string& request_id,
                              const std::string& remote_address);

  void TraceDnsLookup(const std::string& request_id,
                      const std::string& hostname,
                      const std::string& resolved_ip,
                      base::TimeDelta duration);

  void TraceTlsHandshake(const std::string& request_id,
                         const std::string& cipher_suite,
                         base::TimeDelta duration);

  void TraceRequestSent(const std::string& request_id,
                        size_t bytes_sent);

  void TraceResponseReceived(const std::string& request_id,
                             int status_code,
                             const std::string& status_text);

  void TraceRequestComplete(const std::string& request_id,
                            bool success,
                            base::TimeDelta total_duration);

  void TraceRequestFailed(const std::string& request_id,
                          int error_code,
                          const std::string& error_message);

  void TraceRedirect(const std::string& request_id,
                     const GURL& redirect_url,
                     int status_code);

  void TraceCacheHit(const std::string& request_id);
  void TraceCacheMiss(const std::string& request_id);

  void TraceServiceWorkerIntercept(const std::string& request_id,
                                   const std::string& sw_scope);

  void TracePartitionValidation(const std::string& request_id,
                                const std::string& expected_partition,
                                const std::string& actual_partition,
                                bool passed);

  void TraceNetworkContextValidation(const std::string& request_id,
                                     const std::string& expected_context,
                                     const std::string& actual_context,
                                     bool passed);

  void TraceProxyEnforcement(const std::string& request_id,
                             const std::string& expected_proxy,
                             const std::string& actual_proxy,
                             bool passed);

  void TraceIsolationViolation(const std::string& container_id,
                               const std::string& violation_type,
                               const std::string& details);

  void TraceContainerDestroying(const std::string& container_id);
  void TraceTabClosed(const std::string& container_id);

  std::vector<NetworkDebugTraceEvent> GetAllTraces() const;

  std::vector<NetworkDebugTraceEvent> GetTracesForContainer(
      const std::string& container_id) const;

  std::vector<NetworkDebugTraceEvent> GetTracesForRequest(
      const std::string& request_id) const;

  std::vector<NetworkDebugTraceEvent> GetViolations() const;

  std::vector<NetworkDebugTraceEvent> GetTracesInTimeRange(
      base::TimeTicks start,
      base::TimeTicks end) const;

  std::vector<NetworkDebugTraceEvent> GetErrors() const;

  void ClearTraces();

  void ClearTracesForContainer(const std::string& container_id);

  struct TraceSummary {
    size_t total_events = 0;
    size_t total_requests = 0;
    size_t successful_requests = 0;
    size_t failed_requests = 0;
    size_t proxy_violations = 0;
    size_t partition_violations = 0;
    size_t context_violations = 0;
    size_t cache_hits = 0;
    size_t cache_misses = 0;
    size_t sw_intercepts = 0;
    std::map<std::string, size_t> requests_per_container;
    std::map<std::string, size_t> errors_per_container;
    base::TimeDelta avg_request_duration;
    base::TimeDelta avg_dns_duration;
    base::TimeDelta avg_tls_duration;
  };
  TraceSummary GetSummary() const;

  std::string GetContainerReport(const std::string& container_id) const;

  std::string GetDiagnosticReport() const;

  std::string ExportToJson() const;

  void DumpToLog() const;

 private:
  friend struct base::DefaultSingletonTraits<ContainerNetworkDebugTracer>;

  ContainerNetworkDebugTracer();
  ~ContainerNetworkDebugTracer();

  struct RequestFlow {
    std::string request_id;
    std::string container_id;
    std::string partition_id;
    GURL url;
    std::string method;
    base::TimeTicks started_at;
    std::vector<NetworkDebugTraceEvent> events;
    bool complete = false;
  };

  std::string GenerateRequestId();

  bool ShouldTrace(const NetworkDebugTraceEvent& event) const;

  void NotifyObservers(const NetworkDebugTraceEvent& event);
  void NotifyViolation(const NetworkDebugTraceEvent& event);

  void TrimTraces();

  mutable base::Lock lock_;

  DebugTracerConfig config_;

  std::vector<NetworkDebugTraceEvent> traces_;

  std::map<std::string, std::unique_ptr<RequestFlow>> request_flows_;

  uint64_t request_id_counter_ = 0;

  base::ObserverList<NetworkDebugTraceObserver> observers_;
};

class ScopedNetworkTraceRequest {
 public:
  ScopedNetworkTraceRequest(const std::string& container_id,
                            const GURL& url,
                            const std::string& method);
  ~ScopedNetworkTraceRequest();

  std::string request_id() const { return request_id_; }

  void SetProxyResolved(const std::string& proxy_server, bool is_direct);
  void SetNetworkContext(const std::string& context_id, bool matched);
  void SetDnsResult(const std::string& ip, base::TimeDelta duration);
  void SetResponse(int status_code);
  void SetError(int error_code, const std::string& message);
  void MarkComplete(bool success);

 private:
  std::string request_id_;
  std::string container_id_;
  base::TimeTicks start_time_;
  bool completed_ = false;
  bool success_ = false;
};

class ScopedNetworkTracePhase {
 public:
  ScopedNetworkTracePhase(const std::string& request_id,
                          NetworkDebugTraceEvent::EventType type,
                          const std::string& description);
  ~ScopedNetworkTracePhase();

  void SetResult(bool success, const std::string& details = "");

 private:
  std::string request_id_;
  NetworkDebugTraceEvent::EventType type_;
  base::TimeTicks start_time_;
  bool completed_ = false;
};

#define TRACE_NETWORK_REQUEST_SCOPE(container_id, url, method) \
  tab_container::ScopedNetworkTraceRequest _trace_request_##__LINE__( \
      container_id, url, method)

#define TRACE_NETWORK_PHASE(request_id, type, description) \
  tab_container::ScopedNetworkTracePhase _trace_phase_##__LINE__( \
      request_id, type, description)

#define TRACE_CONTAINER_EVENT(container_id, message) \
  do { \
    if (tab_container::ContainerNetworkDebugTracer::GetInstance()->IsEnabled()) { \
      tab_container::NetworkDebugTraceEvent event; \
      event.type = tab_container::NetworkDebugTraceEvent::EventType::kInfo; \
      event.severity = tab_container::NetworkDebugTraceEvent::Severity::kInfo; \
      event.timestamp = base::TimeTicks::Now(); \
      event.container_id = container_id; \
      event.message = message; \
      tab_container::ContainerNetworkDebugTracer::GetInstance()->RecordEvent(event); \
    } \
  } while (0)

#define TRACE_VIOLATION(container_id, type, details) \
  tab_container::ContainerNetworkDebugTracer::GetInstance()->TraceIsolationViolation( \
      container_id, type, details)

}  

#endif  
