
#include "chrome/browser/container/container_network_debug_tracer.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/singleton.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/unguessable_token.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"

namespace tab_container {

namespace {

std::string EventTypeToString(NetworkDebugTraceEvent::EventType type) {
  switch (type) {
    case NetworkDebugTraceEvent::EventType::kTabCreated:
      return "TabCreated";
    case NetworkDebugTraceEvent::EventType::kContainerCreated:
      return "ContainerCreated";
    case NetworkDebugTraceEvent::EventType::kPartitionCreated:
      return "PartitionCreated";
    case NetworkDebugTraceEvent::EventType::kNetworkContextCreated:
      return "NetworkContextCreated";
    case NetworkDebugTraceEvent::EventType::kProxyConfigured:
      return "ProxyConfigured";
    case NetworkDebugTraceEvent::EventType::kRequestStarted:
      return "RequestStarted";
    case NetworkDebugTraceEvent::EventType::kProxyResolved:
      return "ProxyResolved";
    case NetworkDebugTraceEvent::EventType::kNetworkContextResolved:
      return "NetworkContextResolved";
    case NetworkDebugTraceEvent::EventType::kConnectionStarted:
      return "ConnectionStarted";
    case NetworkDebugTraceEvent::EventType::kDnsLookup:
      return "DnsLookup";
    case NetworkDebugTraceEvent::EventType::kTlsHandshake:
      return "TlsHandshake";
    case NetworkDebugTraceEvent::EventType::kRequestSent:
      return "RequestSent";
    case NetworkDebugTraceEvent::EventType::kResponseHeadersReceived:
      return "ResponseHeadersReceived";
    case NetworkDebugTraceEvent::EventType::kResponseBodyReceived:
      return "ResponseBodyReceived";
    case NetworkDebugTraceEvent::EventType::kRequestComplete:
      return "RequestComplete";
    case NetworkDebugTraceEvent::EventType::kRequestFailed:
      return "RequestFailed";
    case NetworkDebugTraceEvent::EventType::kRedirectReceived:
      return "RedirectReceived";
    case NetworkDebugTraceEvent::EventType::kRedirectFollowed:
      return "RedirectFollowed";
    case NetworkDebugTraceEvent::EventType::kCacheHit:
      return "CacheHit";
    case NetworkDebugTraceEvent::EventType::kCacheMiss:
      return "CacheMiss";
    case NetworkDebugTraceEvent::EventType::kCacheWrite:
      return "CacheWrite";
    case NetworkDebugTraceEvent::EventType::kServiceWorkerIntercept:
      return "ServiceWorkerIntercept";
    case NetworkDebugTraceEvent::EventType::kServiceWorkerFetch:
      return "ServiceWorkerFetch";
    case NetworkDebugTraceEvent::EventType::kServiceWorkerResponse:
      return "ServiceWorkerResponse";
    case NetworkDebugTraceEvent::EventType::kPartitionValidation:
      return "PartitionValidation";
    case NetworkDebugTraceEvent::EventType::kNetworkContextValidation:
      return "NetworkContextValidation";
    case NetworkDebugTraceEvent::EventType::kProxyEnforcement:
      return "ProxyEnforcement";
    case NetworkDebugTraceEvent::EventType::kIsolationCheck:
      return "IsolationCheck";
    case NetworkDebugTraceEvent::EventType::kPartitionMismatch:
      return "PartitionMismatch";
    case NetworkDebugTraceEvent::EventType::kNetworkContextMismatch:
      return "NetworkContextMismatch";
    case NetworkDebugTraceEvent::EventType::kProxyBypass:
      return "ProxyBypass";
    case NetworkDebugTraceEvent::EventType::kIsolationViolation:
      return "IsolationViolation";
    case NetworkDebugTraceEvent::EventType::kContainerDestroying:
      return "ContainerDestroying";
    case NetworkDebugTraceEvent::EventType::kPartitionDestroying:
      return "PartitionDestroying";
    case NetworkDebugTraceEvent::EventType::kNetworkContextDestroying:
      return "NetworkContextDestroying";
    case NetworkDebugTraceEvent::EventType::kTabClosed:
      return "TabClosed";
  }
  return "Unknown";
}

std::string SeverityToString(NetworkDebugTraceEvent::Severity severity) {
  switch (severity) {
    case NetworkDebugTraceEvent::Severity::kTrace:
      return "TRACE";
    case NetworkDebugTraceEvent::Severity::kDebug:
      return "DEBUG";
    case NetworkDebugTraceEvent::Severity::kInfo:
      return "INFO";
    case NetworkDebugTraceEvent::Severity::kWarning:
      return "WARNING";
    case NetworkDebugTraceEvent::Severity::kError:
      return "ERROR";
    case NetworkDebugTraceEvent::Severity::kCritical:
      return "CRITICAL";
  }
  return "UNKNOWN";
}

bool IsViolationEvent(NetworkDebugTraceEvent::EventType type) {
  switch (type) {
    case NetworkDebugTraceEvent::EventType::kPartitionMismatch:
    case NetworkDebugTraceEvent::EventType::kNetworkContextMismatch:
    case NetworkDebugTraceEvent::EventType::kProxyBypass:
    case NetworkDebugTraceEvent::EventType::kIsolationViolation:
      return true;
    default:
      return false;
  }
}

}  

std::string NetworkDebugTraceEvent::ToString() const {
  std::stringstream ss;
  ss << "[" << SeverityToString(severity) << "] ";
  ss << EventTypeToString(type);

  if (!container_id.empty()) {
    ss << " container=" << container_id;
  }
  if (!request_id.empty()) {
    ss << " request=" << request_id;
  }
  if (!url.empty()) {
    ss << " url=" << url;
  }
  if (!proxy_server.empty()) {
    ss << " proxy=" << proxy_server;
  }
  if (status_code > 0) {
    ss << " status=" << status_code;
  }
  if (!message.empty()) {
    ss << " msg=" << message;
  }
  if (!validation_passed) {
    ss << " VALIDATION_FAILED: " << validation_error;
  }

  return ss.str();
}

ContainerNetworkDebugTracer* ContainerNetworkDebugTracer::GetInstance() {
  return base::Singleton<ContainerNetworkDebugTracer>::get();
}

ContainerNetworkDebugTracer::ContainerNetworkDebugTracer() {
  ;
}

ContainerNetworkDebugTracer::~ContainerNetworkDebugTracer() {
  ;
}

void ContainerNetworkDebugTracer::SetConfig(const DebugTracerConfig& config) {
  base::AutoLock lock(lock_);
  config_ = config;
  ;
}

DebugTracerConfig ContainerNetworkDebugTracer::GetConfig() const {
  base::AutoLock lock(lock_);
  return config_;
}

void ContainerNetworkDebugTracer::Enable() {
  base::AutoLock lock(lock_);
  config_.enabled = true;
  ;
}

void ContainerNetworkDebugTracer::Disable() {
  base::AutoLock lock(lock_);
  config_.enabled = false;
  ;
}

bool ContainerNetworkDebugTracer::IsEnabled() const {
  base::AutoLock lock(lock_);
  return config_.enabled;
}

void ContainerNetworkDebugTracer::AddObserver(
    NetworkDebugTraceObserver* observer) {
  observers_.AddObserver(observer);
}

void ContainerNetworkDebugTracer::RemoveObserver(
    NetworkDebugTraceObserver* observer) {
  observers_.RemoveObserver(observer);
}

void ContainerNetworkDebugTracer::RecordEvent(
    const NetworkDebugTraceEvent& event) {
  base::AutoLock lock(lock_);

  if (!config_.enabled) {
    return;
  }

  if (!ShouldTrace(event)) {
    return;
  }

  traces_.push_back(event);

  if (!event.request_id.empty()) {
    auto it = request_flows_.find(event.request_id);
    if (it != request_flows_.end()) {
      it->second->events.push_back(event);
    }
  }

  TrimTraces();

  if (IsViolationEvent(event.type) || !event.validation_passed) {
    ;
    NotifyViolation(event);
  } else {
    ;
  }

  NotifyObservers(event);
}

void ContainerNetworkDebugTracer::TraceTabCreated(
    content::WebContents* web_contents,
    const std::string& container_id) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kTabCreated;
  event.severity = NetworkDebugTraceEvent::Severity::kInfo;
  event.timestamp = base::TimeTicks::Now();
  event.container_id = container_id;
  event.message = "Tab created with WebContents=" + 
                  base::NumberToString(reinterpret_cast<uintptr_t>(web_contents));
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceContainerCreated(
    const std::string& container_id,
    const std::string& partition_id) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kContainerCreated;
  event.severity = NetworkDebugTraceEvent::Severity::kInfo;
  event.timestamp = base::TimeTicks::Now();
  event.container_id = container_id;
  event.partition_id = partition_id;
  event.message = "Container created with partition_id=" + partition_id;
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TracePartitionCreated(
    const std::string& partition_id,
    content::StoragePartition* partition) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kPartitionCreated;
  event.severity = NetworkDebugTraceEvent::Severity::kInfo;
  event.timestamp = base::TimeTicks::Now();
  event.partition_id = partition_id;
  event.message = "StoragePartition created at " + 
                  base::NumberToString(reinterpret_cast<uintptr_t>(partition));
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceNetworkContextCreated(
    const std::string& container_id,
    const std::string& network_context_id) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kNetworkContextCreated;
  event.severity = NetworkDebugTraceEvent::Severity::kInfo;
  event.timestamp = base::TimeTicks::Now();
  event.container_id = container_id;
  event.network_context_id = network_context_id;
  event.message = "NetworkContext created: " + network_context_id;
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceProxyConfigured(
    const std::string& container_id,
    const std::string& proxy_server) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kProxyConfigured;
  event.severity = NetworkDebugTraceEvent::Severity::kInfo;
  event.timestamp = base::TimeTicks::Now();
  event.container_id = container_id;
  event.proxy_server = proxy_server;
  event.message = "Proxy configured: " + proxy_server;
  RecordEvent(event);
}

std::string ContainerNetworkDebugTracer::TraceRequestStarted(
    const std::string& container_id,
    const GURL& url,
    const std::string& method) {
  std::string request_id = GenerateRequestId();

  {
    base::AutoLock lock(lock_);
    auto flow = std::make_unique<RequestFlow>();
    flow->request_id = request_id;
    flow->container_id = container_id;
    flow->url = url;
    flow->method = method;
    flow->started_at = base::TimeTicks::Now();
    request_flows_[request_id] = std::move(flow);
  }

  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kRequestStarted;
  event.severity = NetworkDebugTraceEvent::Severity::kDebug;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.container_id = container_id;
  event.url = url.spec();
  event.method = method;
  event.phase_start = base::TimeTicks::Now();
  event.message = method + " " + url.spec();
  RecordEvent(event);

  return request_id;
}

void ContainerNetworkDebugTracer::TraceProxyResolved(
    const std::string& request_id,
    const std::string& proxy_server,
    bool is_direct) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kProxyResolved;
  event.severity = NetworkDebugTraceEvent::Severity::kDebug;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.proxy_server = is_direct ? "DIRECT" : proxy_server;
  event.proxy_used = !is_direct;
  event.message = is_direct ? "Resolved to DIRECT" : "Resolved to " + proxy_server;
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceNetworkContextResolved(
    const std::string& request_id,
    const std::string& network_context_id,
    bool matched_expected) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kNetworkContextResolved;
  event.severity = matched_expected ? NetworkDebugTraceEvent::Severity::kDebug
                                    : NetworkDebugTraceEvent::Severity::kError;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.network_context_id = network_context_id;
  event.validation_passed = matched_expected;
  event.message = "NetworkContext resolved: " + network_context_id;
  if (!matched_expected) {
    event.validation_error = "NetworkContext mismatch detected";
  }
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceConnectionStarted(
    const std::string& request_id,
    const std::string& remote_address) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kConnectionStarted;
  event.severity = NetworkDebugTraceEvent::Severity::kTrace;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.message = "Connecting to " + remote_address;
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceDnsLookup(
    const std::string& request_id,
    const std::string& hostname,
    const std::string& resolved_ip,
    base::TimeDelta duration) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kDnsLookup;
  event.severity = NetworkDebugTraceEvent::Severity::kTrace;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.phase_duration = duration;
  event.message = base::StringPrintf("DNS: %s -> %s (%.2fms)",
                                     hostname.c_str(),
                                     resolved_ip.c_str(),
                                     duration.InMillisecondsF());
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceTlsHandshake(
    const std::string& request_id,
    const std::string& cipher_suite,
    base::TimeDelta duration) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kTlsHandshake;
  event.severity = NetworkDebugTraceEvent::Severity::kTrace;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.phase_duration = duration;
  event.message = base::StringPrintf("TLS: %s (%.2fms)",
                                     cipher_suite.c_str(),
                                     duration.InMillisecondsF());
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceRequestSent(
    const std::string& request_id,
    size_t bytes_sent) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kRequestSent;
  event.severity = NetworkDebugTraceEvent::Severity::kTrace;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.message = "Request sent: " + base::NumberToString(bytes_sent) + " bytes";
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceResponseReceived(
    const std::string& request_id,
    int status_code,
    const std::string& status_text) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kResponseHeadersReceived;
  event.severity = NetworkDebugTraceEvent::Severity::kDebug;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.status_code = status_code;
  event.message = base::StringPrintf("Response: %d %s", 
                                     status_code, status_text.c_str());
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceRequestComplete(
    const std::string& request_id,
    bool success,
    base::TimeDelta total_duration) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kRequestComplete;
  event.severity = success ? NetworkDebugTraceEvent::Severity::kInfo
                          : NetworkDebugTraceEvent::Severity::kWarning;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.phase_duration = total_duration;
  event.message = base::StringPrintf("Request %s in %.2fms",
                                     success ? "completed" : "failed",
                                     total_duration.InMillisecondsF());
  RecordEvent(event);

  {
    base::AutoLock lock(lock_);
    auto it = request_flows_.find(request_id);
    if (it != request_flows_.end()) {
      it->second->complete = true;

      std::stringstream summary;
      summary << "Request " << request_id << " to " << it->second->url.spec();
      summary << " completed " << (success ? "successfully" : "with error");
      summary << " in " << total_duration.InMillisecondsF() << "ms";

      for (auto& observer : observers_) {
        observer.OnRequestFlowComplete(request_id, success, summary.str());
      }
    }
  }
}

void ContainerNetworkDebugTracer::TraceRequestFailed(
    const std::string& request_id,
    int error_code,
    const std::string& error_message) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kRequestFailed;
  event.severity = NetworkDebugTraceEvent::Severity::kError;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.status_code = error_code;
  event.message = "Request failed: " + error_message + 
                  " (code=" + base::NumberToString(error_code) + ")";
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceRedirect(
    const std::string& request_id,
    const GURL& redirect_url,
    int status_code) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kRedirectReceived;
  event.severity = NetworkDebugTraceEvent::Severity::kDebug;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.url = redirect_url.spec();
  event.status_code = status_code;
  event.message = base::StringPrintf("Redirect %d -> %s", 
                                     status_code, redirect_url.spec().c_str());
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceCacheHit(const std::string& request_id) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kCacheHit;
  event.severity = NetworkDebugTraceEvent::Severity::kTrace;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.message = "Cache hit";
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceCacheMiss(const std::string& request_id) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kCacheMiss;
  event.severity = NetworkDebugTraceEvent::Severity::kTrace;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.message = "Cache miss";
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceServiceWorkerIntercept(
    const std::string& request_id,
    const std::string& sw_scope) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kServiceWorkerIntercept;
  event.severity = NetworkDebugTraceEvent::Severity::kDebug;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.message = "ServiceWorker intercept: scope=" + sw_scope;
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TracePartitionValidation(
    const std::string& request_id,
    const std::string& expected_partition,
    const std::string& actual_partition,
    bool passed) {
  NetworkDebugTraceEvent event;
  event.type = passed ? NetworkDebugTraceEvent::EventType::kPartitionValidation
                      : NetworkDebugTraceEvent::EventType::kPartitionMismatch;
  event.severity = passed ? NetworkDebugTraceEvent::Severity::kDebug
                         : NetworkDebugTraceEvent::Severity::kCritical;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.partition_id = actual_partition;
  event.validation_passed = passed;
  event.message = passed ? "Partition validation passed"
                        : "PARTITION MISMATCH: expected=" + expected_partition +
                          ", actual=" + actual_partition;
  if (!passed) {
    event.validation_error = "Expected partition " + expected_partition + 
                             " but got " + actual_partition;
  }
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceNetworkContextValidation(
    const std::string& request_id,
    const std::string& expected_context,
    const std::string& actual_context,
    bool passed) {
  NetworkDebugTraceEvent event;
  event.type = passed ? NetworkDebugTraceEvent::EventType::kNetworkContextValidation
                      : NetworkDebugTraceEvent::EventType::kNetworkContextMismatch;
  event.severity = passed ? NetworkDebugTraceEvent::Severity::kDebug
                         : NetworkDebugTraceEvent::Severity::kCritical;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.network_context_id = actual_context;
  event.validation_passed = passed;
  event.message = passed ? "NetworkContext validation passed"
                        : "NETWORKCONTEXT MISMATCH: expected=" + expected_context +
                          ", actual=" + actual_context;
  if (!passed) {
    event.validation_error = "Expected context " + expected_context + 
                             " but got " + actual_context;
  }
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceProxyEnforcement(
    const std::string& request_id,
    const std::string& expected_proxy,
    const std::string& actual_proxy,
    bool passed) {
  NetworkDebugTraceEvent event;
  event.type = passed ? NetworkDebugTraceEvent::EventType::kProxyEnforcement
                      : NetworkDebugTraceEvent::EventType::kProxyBypass;
  event.severity = passed ? NetworkDebugTraceEvent::Severity::kDebug
                         : NetworkDebugTraceEvent::Severity::kCritical;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id;
  event.proxy_server = actual_proxy;
  event.proxy_expected = !expected_proxy.empty();
  event.proxy_used = !actual_proxy.empty() && actual_proxy != "DIRECT";
  event.validation_passed = passed;
  event.message = passed ? "Proxy enforcement passed"
                        : "PROXY BYPASS: expected=" + expected_proxy +
                          ", actual=" + actual_proxy;
  if (!passed) {
    event.validation_error = "Expected proxy " + expected_proxy + 
                             " but got " + actual_proxy;
  }
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceIsolationViolation(
    const std::string& container_id,
    const std::string& violation_type,
    const std::string& details) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kIsolationViolation;
  event.severity = NetworkDebugTraceEvent::Severity::kCritical;
  event.timestamp = base::TimeTicks::Now();
  event.container_id = container_id;
  event.validation_passed = false;
  event.validation_error = violation_type + ": " + details;
  event.message = "ISOLATION VIOLATION [" + violation_type + "]: " + details;
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceContainerDestroying(
    const std::string& container_id) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kContainerDestroying;
  event.severity = NetworkDebugTraceEvent::Severity::kInfo;
  event.timestamp = base::TimeTicks::Now();
  event.container_id = container_id;
  event.message = "Container being destroyed";
  RecordEvent(event);
}

void ContainerNetworkDebugTracer::TraceTabClosed(
    const std::string& container_id) {
  NetworkDebugTraceEvent event;
  event.type = NetworkDebugTraceEvent::EventType::kTabClosed;
  event.severity = NetworkDebugTraceEvent::Severity::kInfo;
  event.timestamp = base::TimeTicks::Now();
  event.container_id = container_id;
  event.message = "Tab closed";
  RecordEvent(event);
}

std::vector<NetworkDebugTraceEvent> 
ContainerNetworkDebugTracer::GetAllTraces() const {
  base::AutoLock lock(lock_);
  return traces_;
}

std::vector<NetworkDebugTraceEvent>
ContainerNetworkDebugTracer::GetTracesForContainer(
    const std::string& container_id) const {
  base::AutoLock lock(lock_);

  std::vector<NetworkDebugTraceEvent> result;
  for (const auto& trace : traces_) {
    if (trace.container_id == container_id) {
      result.push_back(trace);
    }
  }
  return result;
}

std::vector<NetworkDebugTraceEvent>
ContainerNetworkDebugTracer::GetTracesForRequest(
    const std::string& request_id) const {
  base::AutoLock lock(lock_);

  std::vector<NetworkDebugTraceEvent> result;
  for (const auto& trace : traces_) {
    if (trace.request_id == request_id) {
      result.push_back(trace);
    }
  }
  return result;
}

std::vector<NetworkDebugTraceEvent>
ContainerNetworkDebugTracer::GetViolations() const {
  base::AutoLock lock(lock_);

  std::vector<NetworkDebugTraceEvent> result;
  for (const auto& trace : traces_) {
    if (!trace.validation_passed || IsViolationEvent(trace.type)) {
      result.push_back(trace);
    }
  }
  return result;
}

std::vector<NetworkDebugTraceEvent>
ContainerNetworkDebugTracer::GetTracesInTimeRange(
    base::TimeTicks start,
    base::TimeTicks end) const {
  base::AutoLock lock(lock_);

  std::vector<NetworkDebugTraceEvent> result;
  for (const auto& trace : traces_) {
    if (trace.timestamp >= start && trace.timestamp <= end) {
      result.push_back(trace);
    }
  }
  return result;
}

std::vector<NetworkDebugTraceEvent>
ContainerNetworkDebugTracer::GetErrors() const {
  base::AutoLock lock(lock_);

  std::vector<NetworkDebugTraceEvent> result;
  for (const auto& trace : traces_) {
    if (trace.severity >= NetworkDebugTraceEvent::Severity::kError) {
      result.push_back(trace);
    }
  }
  return result;
}

void ContainerNetworkDebugTracer::ClearTraces() {
  base::AutoLock lock(lock_);
  traces_.clear();
  request_flows_.clear();
  ;
}

void ContainerNetworkDebugTracer::ClearTracesForContainer(
    const std::string& container_id) {
  base::AutoLock lock(lock_);

  traces_.erase(
      std::remove_if(traces_.begin(), traces_.end(),
                     [&container_id](const NetworkDebugTraceEvent& event) {
                       return event.container_id == container_id;
                     }),
      traces_.end());

  for (auto it = request_flows_.begin(); it != request_flows_.end();) {
    if (it->second->container_id == container_id) {
      it = request_flows_.erase(it);
    } else {
      ++it;
    }
  }

  ;
}

ContainerNetworkDebugTracer::TraceSummary
ContainerNetworkDebugTracer::GetSummary() const {
  base::AutoLock lock(lock_);

  TraceSummary summary;
  summary.total_events = traces_.size();

  base::TimeDelta total_request_duration;
  base::TimeDelta total_dns_duration;
  base::TimeDelta total_tls_duration;
  size_t request_count = 0;
  size_t dns_count = 0;
  size_t tls_count = 0;

  for (const auto& trace : traces_) {
    switch (trace.type) {
      case NetworkDebugTraceEvent::EventType::kRequestStarted:
        summary.total_requests++;
        summary.requests_per_container[trace.container_id]++;
        break;
      case NetworkDebugTraceEvent::EventType::kRequestComplete:
        summary.successful_requests++;
        if (!trace.phase_duration.is_zero()) {
          total_request_duration += trace.phase_duration;
          request_count++;
        }
        break;
      case NetworkDebugTraceEvent::EventType::kRequestFailed:
        summary.failed_requests++;
        summary.errors_per_container[trace.container_id]++;
        break;
      case NetworkDebugTraceEvent::EventType::kProxyBypass:
        summary.proxy_violations++;
        break;
      case NetworkDebugTraceEvent::EventType::kPartitionMismatch:
        summary.partition_violations++;
        break;
      case NetworkDebugTraceEvent::EventType::kNetworkContextMismatch:
        summary.context_violations++;
        break;
      case NetworkDebugTraceEvent::EventType::kCacheHit:
        summary.cache_hits++;
        break;
      case NetworkDebugTraceEvent::EventType::kCacheMiss:
        summary.cache_misses++;
        break;
      case NetworkDebugTraceEvent::EventType::kServiceWorkerIntercept:
        summary.sw_intercepts++;
        break;
      case NetworkDebugTraceEvent::EventType::kDnsLookup:
        if (!trace.phase_duration.is_zero()) {
          total_dns_duration += trace.phase_duration;
          dns_count++;
        }
        break;
      case NetworkDebugTraceEvent::EventType::kTlsHandshake:
        if (!trace.phase_duration.is_zero()) {
          total_tls_duration += trace.phase_duration;
          tls_count++;
        }
        break;
      default:
        break;
    }
  }

  if (request_count > 0) {
    summary.avg_request_duration = total_request_duration / request_count;
  }
  if (dns_count > 0) {
    summary.avg_dns_duration = total_dns_duration / dns_count;
  }
  if (tls_count > 0) {
    summary.avg_tls_duration = total_tls_duration / tls_count;
  }

  return summary;
}

std::string ContainerNetworkDebugTracer::GetContainerReport(
    const std::string& container_id) const {
  std::vector<NetworkDebugTraceEvent> container_traces = 
      GetTracesForContainer(container_id);

  std::stringstream report;
  report << "=== Container Report: " << container_id << " ===\n";
  report << "Total traces: " << container_traces.size() << "\n\n";

  std::map<NetworkDebugTraceEvent::EventType, size_t> type_counts;
  size_t violations = 0;

  for (const auto& trace : container_traces) {
    type_counts[trace.type]++;
    if (!trace.validation_passed || IsViolationEvent(trace.type)) {
      violations++;
    }
  }

  report << "Event counts:\n";
  for (const auto& pair : type_counts) {
    report << "  " << EventTypeToString(pair.first) << ": " << pair.second << "\n";
  }

  report << "\nViolations: " << violations << "\n";

  if (violations > 0) {
    report << "\nViolation details:\n";
    for (const auto& trace : container_traces) {
      if (!trace.validation_passed || IsViolationEvent(trace.type)) {
        report << "  - " << trace.ToString() << "\n";
      }
    }
  }

  return report.str();
}

std::string ContainerNetworkDebugTracer::GetDiagnosticReport() const {
  base::AutoLock lock(lock_);

  TraceSummary summary = GetSummary();

  std::stringstream report;
  report << "=== ContainerNetworkDebugTracer Diagnostic Report ===\n";
  report << "Enabled: " << (config_.enabled ? "Yes" : "No") << "\n\n";

  report << "Summary:\n";
  report << "  Total Events: " << summary.total_events << "\n";
  report << "  Total Requests: " << summary.total_requests << "\n";
  report << "  Successful: " << summary.successful_requests << "\n";
  report << "  Failed: " << summary.failed_requests << "\n";
  report << "  Proxy Violations: " << summary.proxy_violations << "\n";
  report << "  Partition Violations: " << summary.partition_violations << "\n";
  report << "  Context Violations: " << summary.context_violations << "\n";
  report << "  Cache Hits: " << summary.cache_hits << "\n";
  report << "  Cache Misses: " << summary.cache_misses << "\n";
  report << "  SW Intercepts: " << summary.sw_intercepts << "\n\n";

  report << "Timing:\n";
  report << "  Avg Request Duration: " 
         << summary.avg_request_duration.InMillisecondsF() << "ms\n";
  report << "  Avg DNS Duration: " 
         << summary.avg_dns_duration.InMillisecondsF() << "ms\n";
  report << "  Avg TLS Duration: " 
         << summary.avg_tls_duration.InMillisecondsF() << "ms\n\n";

  report << "Per-Container Stats:\n";
  for (const auto& pair : summary.requests_per_container) {
    report << "  " << pair.first << ": " << pair.second << " requests";
    auto error_it = summary.errors_per_container.find(pair.first);
    if (error_it != summary.errors_per_container.end()) {
      report << ", " << error_it->second << " errors";
    }
    report << "\n";
  }

  return report.str();
}

std::string ContainerNetworkDebugTracer::ExportToJson() const {
  base::AutoLock lock(lock_);

  base::ListValue traces_list;
  for (const auto& trace : traces_) {
    base::DictValue trace_dict;
    trace_dict.Set("type", EventTypeToString(trace.type));
    trace_dict.Set("severity", SeverityToString(trace.severity));
    trace_dict.Set("timestamp", base::NumberToString(
        trace.timestamp.since_origin().InMicroseconds()));
    trace_dict.Set("request_id", trace.request_id);
    trace_dict.Set("container_id", trace.container_id);
    trace_dict.Set("partition_id", trace.partition_id);
    trace_dict.Set("network_context_id", trace.network_context_id);
    trace_dict.Set("url", trace.url);
    trace_dict.Set("method", trace.method);
    trace_dict.Set("status_code", trace.status_code);
    trace_dict.Set("proxy_server", trace.proxy_server);
    trace_dict.Set("proxy_expected", trace.proxy_expected);
    trace_dict.Set("proxy_used", trace.proxy_used);
    trace_dict.Set("message", trace.message);
    trace_dict.Set("validation_passed", trace.validation_passed);
    trace_dict.Set("validation_error", trace.validation_error);
    traces_list.Append(std::move(trace_dict));
  }

  base::DictValue root;
  root.Set("traces", std::move(traces_list));
  root.Set("trace_count", static_cast<int>(traces_.size()));

  std::string json;
  base::JSONWriter::WriteWithOptions(
      root, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  return json;
}

void ContainerNetworkDebugTracer::DumpToLog() const {
  ;
}

std::string ContainerNetworkDebugTracer::GenerateRequestId() {
  base::AutoLock lock(lock_);
  return "req_" + base::NumberToString(++request_id_counter_) + "_" +
         base::UnguessableToken::Create().ToString().substr(0, 8);
}

bool ContainerNetworkDebugTracer::ShouldTrace(
    const NetworkDebugTraceEvent& event) const {

  if (!event.validation_passed || IsViolationEvent(event.type)) {
    return true;
  }

  if (config_.trace_errors_only) {
    return event.severity >= NetworkDebugTraceEvent::Severity::kError;
  }

  if (config_.trace_proxy_only) {
    return event.proxy_expected || event.proxy_used ||
           event.type == NetworkDebugTraceEvent::EventType::kProxyConfigured ||
           event.type == NetworkDebugTraceEvent::EventType::kProxyResolved ||
           event.type == NetworkDebugTraceEvent::EventType::kProxyEnforcement ||
           event.type == NetworkDebugTraceEvent::EventType::kProxyBypass;
  }

  if (!config_.url_filters.empty() && !event.url.empty()) {
    bool matches = false;
    for (const auto& filter : config_.url_filters) {
      if (event.url.find(filter) != std::string::npos) {
        matches = true;
        break;
      }
    }
    if (!matches) return false;
  }

  if (!config_.container_filters.empty() && !event.container_id.empty()) {
    bool matches = false;
    for (const auto& filter : config_.container_filters) {
      if (event.container_id == filter) {
        matches = true;
        break;
      }
    }
    if (!matches) return false;
  }

  return config_.trace_all_requests;
}

void ContainerNetworkDebugTracer::NotifyObservers(
    const NetworkDebugTraceEvent& event) {
  for (auto& observer : observers_) {
    observer.OnTraceEvent(event);
  }
}

void ContainerNetworkDebugTracer::NotifyViolation(
    const NetworkDebugTraceEvent& event) {
  for (auto& observer : observers_) {
    observer.OnViolationDetected(event);
  }
}

void ContainerNetworkDebugTracer::TrimTraces() {
  if (traces_.size() > config_.max_total_traces) {
    size_t to_remove = traces_.size() - config_.max_total_traces;
    traces_.erase(traces_.begin(), traces_.begin() + to_remove);
  }
}

ScopedNetworkTraceRequest::ScopedNetworkTraceRequest(
    const std::string& container_id,
    const GURL& url,
    const std::string& method)
    : container_id_(container_id),
      start_time_(base::TimeTicks::Now()) {
  request_id_ = ContainerNetworkDebugTracer::GetInstance()->TraceRequestStarted(
      container_id, url, method);
}

ScopedNetworkTraceRequest::~ScopedNetworkTraceRequest() {
  if (!completed_) {
    MarkComplete(success_);
  }
}

void ScopedNetworkTraceRequest::SetProxyResolved(
    const std::string& proxy_server, bool is_direct) {
  ContainerNetworkDebugTracer::GetInstance()->TraceProxyResolved(
      request_id_, proxy_server, is_direct);
}

void ScopedNetworkTraceRequest::SetNetworkContext(
    const std::string& context_id, bool matched) {
  ContainerNetworkDebugTracer::GetInstance()->TraceNetworkContextResolved(
      request_id_, context_id, matched);
}

void ScopedNetworkTraceRequest::SetDnsResult(
    const std::string& ip, base::TimeDelta duration) {
  ContainerNetworkDebugTracer::GetInstance()->TraceDnsLookup(
      request_id_, "", ip, duration);
}

void ScopedNetworkTraceRequest::SetResponse(int status_code) {
  ContainerNetworkDebugTracer::GetInstance()->TraceResponseReceived(
      request_id_, status_code, "");
}

void ScopedNetworkTraceRequest::SetError(
    int error_code, const std::string& message) {
  ContainerNetworkDebugTracer::GetInstance()->TraceRequestFailed(
      request_id_, error_code, message);
  success_ = false;
}

void ScopedNetworkTraceRequest::MarkComplete(bool success) {
  if (completed_) return;
  completed_ = true;
  success_ = success;

  base::TimeDelta duration = base::TimeTicks::Now() - start_time_;
  ContainerNetworkDebugTracer::GetInstance()->TraceRequestComplete(
      request_id_, success, duration);
}

ScopedNetworkTracePhase::ScopedNetworkTracePhase(
    const std::string& request_id,
    NetworkDebugTraceEvent::EventType type,
    const std::string& description)
    : request_id_(request_id),
      type_(type),
      start_time_(base::TimeTicks::Now()) {
  NetworkDebugTraceEvent event;
  event.type = type_;
  event.severity = NetworkDebugTraceEvent::Severity::kTrace;
  event.timestamp = start_time_;
  event.request_id = request_id_;
  event.message = description + " (started)";
  event.phase_start = start_time_;
  ContainerNetworkDebugTracer::GetInstance()->RecordEvent(event);
}

ScopedNetworkTracePhase::~ScopedNetworkTracePhase() {
  if (!completed_) {
    SetResult(true);
  }
}

void ScopedNetworkTracePhase::SetResult(
    bool success, const std::string& details) {
  if (completed_) return;
  completed_ = true;

  NetworkDebugTraceEvent event;
  event.type = type_;
  event.severity = success ? NetworkDebugTraceEvent::Severity::kTrace
                          : NetworkDebugTraceEvent::Severity::kWarning;
  event.timestamp = base::TimeTicks::Now();
  event.request_id = request_id_;
  event.phase_start = start_time_;
  event.phase_duration = base::TimeTicks::Now() - start_time_;
  event.message = (success ? "Phase complete" : "Phase failed") + 
                  (details.empty() ? "" : ": " + details);
  ContainerNetworkDebugTracer::GetInstance()->RecordEvent(event);
}

}  
