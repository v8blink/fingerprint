
#include "chrome/browser/container/container_network_context_manager.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/unguessable_token.h"
#include "chrome/browser/container/container_partition_tracker.h"
#include "chrome/browser/container/tab_container_manager.h"
#include "chrome/browser/container/tab_container_manager_factory.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "services/network/public/mojom/network_context.mojom.h"

namespace tab_container {

namespace {

constexpr const char* kLogPrefix = "[ContainerNetworkContextManager]";

std::string NetworkContextStateToString(NetworkContextState state) {
  switch (state) {
    case NetworkContextState::kNotCreated:
      return "NotCreated";
    case NetworkContextState::kCreating:
      return "Creating";
    case NetworkContextState::kConfiguring:
      return "Configuring";
    case NetworkContextState::kActive:
      return "Active";
    case NetworkContextState::kReconfiguring:
      return "Reconfiguring";
    case NetworkContextState::kDestroying:
      return "Destroying";
    case NetworkContextState::kDestroyed:
      return "Destroyed";
    case NetworkContextState::kError:
      return "Error";
  }
  return "Unknown";
}

std::string TraceTypeToString(NetworkRequestTrace::TraceType type) {
  switch (type) {
    case NetworkRequestTrace::TraceType::kNavigation:
      return "Navigation";
    case NetworkRequestTrace::TraceType::kSubresource:
      return "Subresource";
    case NetworkRequestTrace::TraceType::kRedirect:
      return "Redirect";
    case NetworkRequestTrace::TraceType::kServiceWorker:
      return "ServiceWorker";
    case NetworkRequestTrace::TraceType::kPrefetch:
      return "Prefetch";
    case NetworkRequestTrace::TraceType::kCached:
      return "Cached";
    case NetworkRequestTrace::TraceType::kWebSocket:
      return "WebSocket";
    case NetworkRequestTrace::TraceType::kFetch:
      return "Fetch";
  }
  return "Unknown";
}

std::string GenerateNetworkContextId() {
  return "nc_" + base::UnguessableToken::Create().ToString();
}

}  

ContainerNetworkContextManager::ContainerNetworkContextManager(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ;
}

ContainerNetworkContextManager::~ContainerNetworkContextManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DestroyAllNetworkContexts();

  ;
}

void ContainerNetworkContextManager::AddObserver(
    ContainerNetworkContextObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void ContainerNetworkContextManager::RemoveObserver(
    ContainerNetworkContextObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

bool ContainerNetworkContextManager::CreateNetworkContextForContainer(
    const std::string& container_id,
    const std::string& partition_id,
    content::StoragePartition* storage_partition) {
  return CreateNetworkContextWithConfig(container_id, partition_id,
                                        storage_partition,
                                        ContainerNetworkConfig());
}

bool ContainerNetworkContextManager::CreateNetworkContextWithConfig(
    const std::string& container_id,
    const std::string& partition_id,
    content::StoragePartition* storage_partition,
    const ContainerNetworkConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  if (container_contexts_.find(container_id) != container_contexts_.end()) {
    ;
    return false;
  }

  if (!storage_partition) {
    ;
    return false;
  }

  auto info = std::make_unique<ContainerNetworkContextInfo>();
  info->container_id = container_id;
  info->partition_id = partition_id;
  info->config = config;
  info->config.container_id = container_id;
  info->config.partition_id = partition_id;
  info->storage_partition = storage_partition;
  info->created_at = base::TimeTicks::Now();
  info->network_context_id = GenerateNetworkContextId();

  TransitionState(info.get(), NetworkContextState::kCreating);

  LogContextOperation(container_id, "Create", 
                      "Initiating NetworkContext creation");

  network::mojom::NetworkContextParamsPtr params = 
      CreateNetworkContextParams(config);

  TransitionState(info.get(), NetworkContextState::kConfiguring);

  LogContextOperation(container_id, "Configure", 
                      "Applying network configuration");

  if (config.proxy_config.enabled) {
    ApplyProxyConfig(info.get());
  }

  ApplyDnsConfig(info.get());

  TransitionState(info.get(), NetworkContextState::kActive);

  partition_to_container_[partition_id] = container_id;
  container_contexts_[container_id] = std::move(info);

  stats_.total_contexts_created++;
  stats_.active_contexts++;
  stats_.last_activity = base::TimeTicks::Now();

  NotifyContextCreated(container_id, partition_id);

  ;

  return true;
}

network::mojom::NetworkContext* 
ContainerNetworkContextManager::GetNetworkContext(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_contexts_.find(container_id);
  if (it == container_contexts_.end()) {
    ;
    return nullptr;
  }

  ContainerNetworkContextInfo* info = it->second.get();

  if (info->state != NetworkContextState::kActive) {
    ;
    return nullptr;
  }

  info->last_used = base::TimeTicks::Now();
  stats_.last_activity = info->last_used;

  if (info->storage_partition) {
    return info->storage_partition->GetNetworkContext();
  }

  return nullptr;
}

network::mojom::NetworkContext*
ContainerNetworkContextManager::GetNetworkContextForWebContents(
    content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!web_contents) {
    return nullptr;
  }

  content::BrowserContext* browser_context = web_contents->GetBrowserContext();
  if (!browser_context) {
    return nullptr;
  }

  TabContainerManager* container_manager = GetForBrowserContext(browser_context);
  if (!container_manager) {
    return nullptr;
  }

  std::string partition_id = 
      container_manager->GetPartitionIdForTab(web_contents);
  if (partition_id.empty()) {
    ;
    return nullptr;
  }

  auto part_it = partition_to_container_.find(partition_id);
  if (part_it == partition_to_container_.end()) {
    ;
    return nullptr;
  }

  return GetNetworkContext(part_it->second);
}

network::mojom::URLLoaderFactory*
ContainerNetworkContextManager::GetURLLoaderFactory(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_contexts_.find(container_id);
  if (it == container_contexts_.end()) {
    return nullptr;
  }

  ContainerNetworkContextInfo* info = it->second.get();

  if (info->state != NetworkContextState::kActive) {
    return nullptr;
  }

  return nullptr;
}

bool ContainerNetworkContextManager::HasNetworkContext(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return container_contexts_.find(container_id) != container_contexts_.end();
}

NetworkContextState ContainerNetworkContextManager::GetNetworkContextState(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_contexts_.find(container_id);
  if (it == container_contexts_.end()) {
    return NetworkContextState::kNotCreated;
  }

  return it->second->state;
}

void ContainerNetworkContextManager::DestroyNetworkContext(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_contexts_.find(container_id);
  if (it == container_contexts_.end()) {
    ;
    return;
  }

  ContainerNetworkContextInfo* info = it->second.get();

  ;

  TransitionState(info, NetworkContextState::kDestroying);

  LogContextOperation(container_id, "Destroy", "Destroying NetworkContext");

  partition_to_container_.erase(info->partition_id);

  info->network_context.reset();
  info->url_loader_factory.reset();

  TransitionState(info, NetworkContextState::kDestroyed);

  for (auto& observer : observers_) {
    observer.OnNetworkContextDestroyed(container_id);
  }

  container_contexts_.erase(it);

  stats_.active_contexts--;
  stats_.contexts_destroyed++;
  stats_.last_activity = base::TimeTicks::Now();

  ;
}

void ContainerNetworkContextManager::DestroyAllNetworkContexts() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  std::vector<std::string> container_ids;
  for (const auto& pair : container_contexts_) {
    container_ids.push_back(pair.first);
  }

  for (const auto& container_id : container_ids) {
    DestroyNetworkContext(container_id);
  }
}

bool ContainerNetworkContextManager::UpdateNetworkConfig(
    const std::string& container_id,
    const ContainerNetworkConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_contexts_.find(container_id);
  if (it == container_contexts_.end()) {
    ;
    return false;
  }

  ContainerNetworkContextInfo* info = it->second.get();

  const NetworkContextState prev_state = info->state;
  ;

  TransitionState(info, NetworkContextState::kReconfiguring);

  info->config = config;
  info->config.container_id = container_id;
  info->config.partition_id = info->partition_id;

  if (config.proxy_config.enabled) {
    ApplyProxyConfig(info);
  }

  ApplyDnsConfig(info);

  TransitionState(info, NetworkContextState::kActive);

  for (auto& observer : observers_) {
    observer.OnNetworkConfigUpdated(container_id, config);
  }

  stats_.config_updates++;
  stats_.last_activity = base::TimeTicks::Now();

  LogContextOperation(container_id, "ConfigUpdate", 
                      "Network configuration updated");

  return true;
}

std::optional<ContainerNetworkConfig>
ContainerNetworkContextManager::GetNetworkConfig(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_contexts_.find(container_id);
  if (it == container_contexts_.end()) {
    return std::nullopt;
  }

  return it->second->config;
}

bool ContainerNetworkContextManager::SetProxyConfig(
    const std::string& container_id,
    const ContainerNetworkConfig::ProxyConfig& proxy_config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_contexts_.find(container_id);
  if (it == container_contexts_.end()) {
    return false;
  }

  ContainerNetworkContextInfo* info = it->second.get();
  info->config.proxy_config = proxy_config;
  ApplyProxyConfig(info);

  ;

  return true;
}

bool ContainerNetworkContextManager::SetDnsConfig(
    const std::string& container_id,
    const ContainerNetworkConfig::DnsConfig& dns_config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_contexts_.find(container_id);
  if (it == container_contexts_.end()) {
    return false;
  }

  ContainerNetworkContextInfo* info = it->second.get();
  info->config.dns_config = dns_config;
  ApplyDnsConfig(info);

  ;

  return true;
}

bool ContainerNetworkContextManager::SetUserAgentConfig(
    const std::string& container_id,
    const ContainerNetworkConfig::UserAgentConfig& user_agent_config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_contexts_.find(container_id);
  if (it == container_contexts_.end()) {
    return false;
  }

  ContainerNetworkContextInfo* info = it->second.get();
  info->config.user_agent_config = user_agent_config;

  ;

  return true;
}

ContainerNetworkContextManager::NetworkContextResolution
ContainerNetworkContextManager::ResolveNetworkContext(
    content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  NetworkContextResolution resolution;
  resolution.resolution_path = "";

  if (!web_contents) {
    resolution.resolution_path = "ERROR: WebContents is null";
    return resolution;
  }

  resolution.resolution_path += "WebContents -> ";

  content::BrowserContext* browser_context = web_contents->GetBrowserContext();
  if (!browser_context) {
    resolution.resolution_path += "ERROR: No BrowserContext";
    return resolution;
  }

  resolution.resolution_path += "BrowserContext -> ";

  TabContainerManager* container_manager = GetForBrowserContext(browser_context);
  if (!container_manager) {
    resolution.resolution_path += "ERROR: No TabContainerManager";
    resolution.used_fallback = true;
    resolution.fallback_reason = "TabContainerManager not found";
    return resolution;
  }

  resolution.resolution_path += "TabContainerManager -> ";

  std::string partition_id = 
      container_manager->GetPartitionIdForTab(web_contents);
  if (partition_id.empty()) {
    resolution.resolution_path += "ERROR: No partition_id";
    resolution.used_fallback = true;
    resolution.fallback_reason = "No partition_id for tab";
    return resolution;
  }

  resolution.partition_id = partition_id;
  resolution.resolution_path += "partition_id=" + partition_id + " -> ";

  auto part_it = partition_to_container_.find(partition_id);
  if (part_it == partition_to_container_.end()) {
    resolution.resolution_path += "ERROR: No container mapping";
    resolution.used_fallback = true;
    resolution.fallback_reason = "No container mapping for partition";
    return resolution;
  }

  resolution.container_id = part_it->second;
  resolution.resolution_path += "container_id=" + resolution.container_id + " -> ";

  auto ctx_it = container_contexts_.find(resolution.container_id);
  if (ctx_it == container_contexts_.end()) {
    resolution.resolution_path += "ERROR: No NetworkContext";
    resolution.used_fallback = true;
    resolution.fallback_reason = "NetworkContext not created";
    return resolution;
  }

  ContainerNetworkContextInfo* info = ctx_it->second.get();

  if (info->state != NetworkContextState::kActive) {
    resolution.resolution_path += "ERROR: Context not active (state=" + 
                                  NetworkContextStateToString(info->state) + ")";
    resolution.used_fallback = true;
    resolution.fallback_reason = "NetworkContext not active";
    return resolution;
  }

  resolution.resolution_path += "NetworkContext (id=" + 
                                info->network_context_id + ")";

  if (info->storage_partition) {
    resolution.context = info->storage_partition->GetNetworkContext();
    resolution.success = true;
  } else {
    resolution.resolution_path += " -> ERROR: No StoragePartition";
    resolution.used_fallback = true;
    resolution.fallback_reason = "StoragePartition not available";
  }

  if (debug_logging_enabled_) {
    LogResolution(base::NumberToString(reinterpret_cast<uintptr_t>(web_contents)),
                  resolution);
  }

  if (resolution.used_fallback) {
    NotifyFallback(resolution.container_id, "", resolution.fallback_reason);
  }

  return resolution;
}

ContainerNetworkContextManager::RequestValidation
ContainerNetworkContextManager::ValidateRequest(
    content::WebContents* web_contents,
    network::mojom::NetworkContext* used_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  RequestValidation validation;

  if (!web_contents) {
    validation.validation_message = "WebContents is null";
    return validation;
  }

  NetworkContextResolution resolution = ResolveNetworkContext(web_contents);

  if (!resolution.success) {
    validation.validation_message = "Failed to resolve expected NetworkContext: " +
                                    resolution.fallback_reason;
    return validation;
  }

  validation.expected_container_id = resolution.container_id;

  if (resolution.context == used_context) {
    validation.valid = true;
    validation.actual_container_id = resolution.container_id;
    validation.validation_message = "Request using correct NetworkContext";
  } else {
    validation.valid = false;
    validation.validation_message = 
        "Request using WRONG NetworkContext! Expected: " +
        resolution.container_id;

    for (const auto& pair : container_contexts_) {
      if (pair.second->storage_partition &&
          pair.second->storage_partition->GetNetworkContext() == used_context) {
        validation.actual_container_id = pair.first;
        validation.validation_message += 
            ", Actual: " + validation.actual_container_id;
        break;
      }
    }

    if (validation.actual_container_id.empty()) {
      validation.validation_message += ", Actual: UNKNOWN (default context?)";
    }

    ;
  }

  return validation;
}

void ContainerNetworkContextManager::SetRequestTracingEnabled(bool enabled) {
  request_tracing_enabled_ = enabled;
  ;
}

void ContainerNetworkContextManager::RecordRequestTrace(
    const NetworkRequestTrace& trace) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!request_tracing_enabled_) {
    return;
  }

  request_traces_.push_back(trace);
  stats_.total_requests_traced++;

  if (!trace.used_correct_context) {
    stats_.fallback_requests++;
  }

  if (request_traces_.size() > max_traces_) {
    request_traces_.erase(request_traces_.begin(),
                          request_traces_.begin() + 
                          (request_traces_.size() - max_traces_));
  }

  NotifyRequestTraced(trace);

  if (debug_logging_enabled_) {
    ;
  }
}

std::vector<NetworkRequestTrace>
ContainerNetworkContextManager::GetRecentTraces(size_t count) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (request_traces_.size() <= count) {
    return request_traces_;
  }

  return std::vector<NetworkRequestTrace>(
      request_traces_.end() - count, request_traces_.end());
}

std::vector<NetworkRequestTrace>
ContainerNetworkContextManager::GetTracesForContainer(
    const std::string& container_id,
    size_t count) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<NetworkRequestTrace> result;
  for (auto it = request_traces_.rbegin(); 
       it != request_traces_.rend() && result.size() < count; 
       ++it) {
    if (it->container_id == container_id) {
      result.push_back(*it);
    }
  }

  std::reverse(result.begin(), result.end());
  return result;
}

std::vector<NetworkRequestTrace>
ContainerNetworkContextManager::GetFallbackTraces() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<NetworkRequestTrace> result;
  for (const auto& trace : request_traces_) {
    if (!trace.used_correct_context) {
      result.push_back(trace);
    }
  }
  return result;
}

void ContainerNetworkContextManager::ClearTraces() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  request_traces_.clear();
}

void ContainerNetworkContextManager::SetDebugLoggingEnabled(bool enabled) {
  debug_logging_enabled_ = enabled;
  ;
}

void ContainerNetworkContextManager::DumpNetworkContextState() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ;
}

std::string ContainerNetworkContextManager::GetDiagnosticReport() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::stringstream report;
  report << "=== ContainerNetworkContextManager Diagnostic Report ===\n";
  report << "Browser Context: " << browser_context_ << "\n\n";

  report << "Statistics:\n";
  report << "  Total Contexts Created: " << stats_.total_contexts_created << "\n";
  report << "  Active Contexts: " << stats_.active_contexts << "\n";
  report << "  Contexts Destroyed: " << stats_.contexts_destroyed << "\n";
  report << "  Total Requests Traced: " << stats_.total_requests_traced << "\n";
  report << "  Fallback Requests: " << stats_.fallback_requests << "\n";
  report << "  Config Updates: " << stats_.config_updates << "\n\n";

  report << "Active NetworkContexts: " << container_contexts_.size() << "\n";
  for (const auto& pair : container_contexts_) {
    const ContainerNetworkContextInfo* info = pair.second.get();
    report << "  - " << pair.first << ":\n";
    report << "      Partition ID: " << info->partition_id << "\n";
    report << "      Context ID: " << info->network_context_id << "\n";
    report << "      State: " << NetworkContextStateToString(info->state) << "\n";
    report << "      Proxy Enabled: " 
           << (info->config.proxy_config.enabled ? "Yes" : "No") << "\n";
    if (info->config.proxy_config.enabled) {
      report << "      Proxy Server: " 
             << info->config.proxy_config.proxy_server << "\n";
    }
  }

  report << "\nPartition to Container Mapping: " 
         << partition_to_container_.size() << "\n";
  for (const auto& pair : partition_to_container_) {
    report << "  - " << pair.first << " -> " << pair.second << "\n";
  }

  report << "\nRecent Traces: " << request_traces_.size() << " stored\n";
  size_t fallback_count = 0;
  for (const auto& trace : request_traces_) {
    if (!trace.used_correct_context) fallback_count++;
  }
  report << "  Fallback traces: " << fallback_count << "\n";

  return report.str();
}

ContainerNetworkContextManager::Statistics
ContainerNetworkContextManager::GetStatistics() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return stats_;
}

network::mojom::NetworkContextParamsPtr
ContainerNetworkContextManager::CreateNetworkContextParams(
    const ContainerNetworkConfig& config) {

  auto params = network::mojom::NetworkContextParams::New();

  if (!config.user_agent_config.user_agent_override.empty()) {
    params->user_agent = config.user_agent_config.user_agent_override;
  }

  if (!config.user_agent_config.accept_language.empty()) {
    params->accept_language = config.user_agent_config.accept_language;
  }

  params->enable_certificate_reporting = 
      config.cert_config.enable_certificate_transparency;

  return params;
}

void ContainerNetworkContextManager::ApplyProxyConfig(
    ContainerNetworkContextInfo* info) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const auto& proxy_config = info->config.proxy_config;

  ;

  if (!proxy_config.enabled) {
    return;
  }

  LogContextOperation(info->container_id, "ApplyProxy",
                      "Proxy configuration applied: " + proxy_config.proxy_server);
}

void ContainerNetworkContextManager::ApplyDnsConfig(
    ContainerNetworkContextInfo* info) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const auto& dns_config = info->config.dns_config;

  ;

  LogContextOperation(info->container_id, "ApplyDns",
                      "DNS configuration applied");
}

void ContainerNetworkContextManager::TransitionState(
    ContainerNetworkContextInfo* info,
    NetworkContextState new_state) {
  NetworkContextState old_state = info->state;
  info->state = new_state;

  if (debug_logging_enabled_) {
    ;
  }

  NotifyStateChanged(info->container_id, old_state, new_state);
}

void ContainerNetworkContextManager::NotifyContextCreated(
    const std::string& container_id,
    const std::string& partition_id) {
  for (auto& observer : observers_) {
    observer.OnNetworkContextCreated(container_id, partition_id);
  }
}

void ContainerNetworkContextManager::NotifyStateChanged(
    const std::string& container_id,
    NetworkContextState old_state,
    NetworkContextState new_state) {
  for (auto& observer : observers_) {
    observer.OnNetworkContextStateChanged(container_id, old_state, new_state);
  }
}

void ContainerNetworkContextManager::NotifyRequestTraced(
    const NetworkRequestTrace& trace) {
  for (auto& observer : observers_) {
    observer.OnNetworkRequestTraced(trace);
  }
}

void ContainerNetworkContextManager::NotifyFallback(
    const std::string& container_id,
    const std::string& url,
    const std::string& reason) {
  for (auto& observer : observers_) {
    observer.OnNetworkContextFallback(container_id, url, reason);
  }
}

void ContainerNetworkContextManager::LogContextOperation(
    const std::string& container_id,
    const std::string& operation,
    const std::string& details) {
  if (debug_logging_enabled_) {
    ;
  }
}

void ContainerNetworkContextManager::LogResolution(
    const std::string& web_contents_id,
    const NetworkContextResolution& resolution) {
  ;

  if (resolution.used_fallback) {
    ;
  }
}

NetworkContextTracer::NetworkContextTracer(
    ContainerNetworkContextManager* manager,
    const std::string& container_id,
    const std::string& url,
    NetworkRequestTrace::TraceType type)
    : manager_(manager), start_time_(base::TimeTicks::Now()) {
  trace_.type = type;
  trace_.timestamp = start_time_;
  trace_.container_id = container_id;
  trace_.url = url;
}

NetworkContextTracer::~NetworkContextTracer() {
  if (!completed_) {
    Complete();
  }
}

void NetworkContextTracer::SetMethod(const std::string& method) {
  trace_.method = method;
}

void NetworkContextTracer::SetStatusCode(int status_code) {
  trace_.status_code = status_code;
}

void NetworkContextTracer::SetResponseHeaders(const std::string& headers) {
  trace_.response_headers = headers;
}

void NetworkContextTracer::MarkFallback(const std::string& reason) {
  trace_.used_correct_context = false;
  trace_.fallback_reason = reason;
}

void NetworkContextTracer::Complete() {
  if (completed_) return;
  completed_ = true;

  trace_.request_duration = base::TimeTicks::Now() - start_time_;

  if (manager_) {
    manager_->RecordRequestTrace(trace_);
  }
}

}  
