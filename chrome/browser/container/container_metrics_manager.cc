
#include "chrome/browser/container/container_metrics_manager.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/browser_context.h"

namespace tab_container {

namespace {

constexpr const char* kLogPrefix = "[ContainerMetricsManager]";

std::string MetricTypeToString(MetricType type) {
  switch (type) {
    case MetricType::kContainerCreated: return "container_created";
    case MetricType::kContainerDestroyed: return "container_destroyed";
    case MetricType::kContainerRestored: return "container_restored";
    case MetricType::kContainerSuspended: return "container_suspended";
    case MetricType::kContainerResumed: return "container_resumed";
    case MetricType::kContainerError: return "container_error";
    case MetricType::kNetworkRequestStarted: return "network_request_started";
    case MetricType::kNetworkRequestCompleted: return "network_request_completed";
    case MetricType::kNetworkRequestFailed: return "network_request_failed";
    case MetricType::kNetworkBytesReceived: return "network_bytes_received";
    case MetricType::kNetworkBytesSent: return "network_bytes_sent";
    case MetricType::kProxyConnected: return "proxy_connected";
    case MetricType::kProxyFailed: return "proxy_failed";
    case MetricType::kDnsResolved: return "dns_resolved";
    case MetricType::kDnsFailed: return "dns_failed";
    case MetricType::kCookieSet: return "cookie_set";
    case MetricType::kCookieBlocked: return "cookie_blocked";
    case MetricType::kStorageUsed: return "storage_used";
    case MetricType::kCacheHit: return "cache_hit";
    case MetricType::kCacheMiss: return "cache_miss";
    case MetricType::kThreatBlocked: return "threat_blocked";
    case MetricType::kPermissionGranted: return "permission_granted";
    case MetricType::kPermissionDenied: return "permission_denied";
    case MetricType::kCSPViolation: return "csp_violation";
    case MetricType::kFingerprintAttempt: return "fingerprint_attempt";
    case MetricType::kFingerprintBlocked: return "fingerprint_blocked";
    case MetricType::kPageLoadTime: return "page_load_time";
    case MetricType::kResourceLoadTime: return "resource_load_time";
    case MetricType::kMemoryUsage: return "memory_usage";
    case MetricType::kCpuUsage: return "cpu_usage";
  }
  return "unknown";
}

}  

void MetricTimeSeries::AddPoint(base::TimeTicks time, double value) {
  data_points.emplace_back(time, value);
  while (data_points.size() > max_points) {
    data_points.pop_front();
  }
}

double MetricTimeSeries::GetLatest() const {
  if (data_points.empty()) {
    return 0.0;
  }
  return data_points.back().second;
}

double MetricTimeSeries::GetAverage(base::TimeDelta window) const {
  if (data_points.empty()) {
    return 0.0;
  }

  base::TimeTicks cutoff = base::TimeTicks::Now() - window;
  double sum = 0;
  size_t count = 0;

  for (auto it = data_points.rbegin(); it != data_points.rend(); ++it) {
    if (it->first < cutoff) break;
    sum += it->second;
    count++;
  }

  return count > 0 ? sum / count : 0.0;
}

double MetricTimeSeries::GetMin(base::TimeDelta window) const {
  if (data_points.empty()) {
    return 0.0;
  }

  base::TimeTicks cutoff = base::TimeTicks::Now() - window;
  double min_val = std::numeric_limits<double>::max();

  for (auto it = data_points.rbegin(); it != data_points.rend(); ++it) {
    if (it->first < cutoff) break;
    min_val = std::min(min_val, it->second);
  }

  return min_val == std::numeric_limits<double>::max() ? 0.0 : min_val;
}

double MetricTimeSeries::GetMax(base::TimeDelta window) const {
  if (data_points.empty()) {
    return 0.0;
  }

  base::TimeTicks cutoff = base::TimeTicks::Now() - window;
  double max_val = std::numeric_limits<double>::lowest();

  for (auto it = data_points.rbegin(); it != data_points.rend(); ++it) {
    if (it->first < cutoff) break;
    max_val = std::max(max_val, it->second);
  }

  return max_val == std::numeric_limits<double>::lowest() ? 0.0 : max_val;
}

double MetricTimeSeries::GetSum(base::TimeDelta window) const {
  if (data_points.empty()) {
    return 0.0;
  }

  base::TimeTicks cutoff = base::TimeTicks::Now() - window;
  double sum = 0;

  for (auto it = data_points.rbegin(); it != data_points.rend(); ++it) {
    if (it->first < cutoff) break;
    sum += it->second;
  }

  return sum;
}

size_t MetricTimeSeries::GetCount(base::TimeDelta window) const {
  if (data_points.empty()) {
    return 0;
  }

  base::TimeTicks cutoff = base::TimeTicks::Now() - window;
  size_t count = 0;

  for (auto it = data_points.rbegin(); it != data_points.rend(); ++it) {
    if (it->first < cutoff) break;
    count++;
  }

  return count;
}

ContainerMetricsManager::ContainerMetricsManager(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;
}

ContainerMetricsManager::~ContainerMetricsManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ;
}

void ContainerMetricsManager::AddObserver(ContainerMetricsObserver* observer) {
  observers_.AddObserver(observer);
}

void ContainerMetricsManager::RemoveObserver(
    ContainerMetricsObserver* observer) {
  observers_.RemoveObserver(observer);
}

void ContainerMetricsManager::RecordMetric(
    const std::string& container_id,
    MetricType type,
    double value,
    const std::map<std::string, std::string>& labels) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!enabled_) {
    return;
  }

  MetricDataPoint metric;
  metric.timestamp = base::TimeTicks::Now();
  metric.container_id = container_id;
  metric.type = type;
  metric.value = value;
  metric.labels = labels;

  ContainerMetricsInfo* info = GetOrCreateContainerInfo(container_id);
  info->recent_metrics.push_back(metric);
  while (info->recent_metrics.size() > max_data_points_) {
    info->recent_metrics.pop_front();
  }

  std::string metric_name = MetricTypeToString(type);
  info->time_series[metric_name].AddPoint(metric.timestamp, value);

  global_metrics_.push_back(metric);
  while (global_metrics_.size() > max_data_points_ * 10) {
    global_metrics_.pop_front();
  }

  info->last_activity = metric.timestamp;

  if (debug_logging_enabled_) {
    ;
  }

  NotifyMetricRecorded(metric);
  UpdateContainerSummary(container_id);
}

void ContainerMetricsManager::IncrementCounter(
    const std::string& container_id,
    MetricType type,
    double amount) {
  RecordMetric(container_id, type, amount);
}

void ContainerMetricsManager::RecordGauge(
    const std::string& container_id,
    MetricType type,
    double value) {
  RecordMetric(container_id, type, value);
}

void ContainerMetricsManager::RecordHistogram(
    const std::string& container_id,
    MetricType type,
    double value) {
  RecordMetric(container_id, type, value);
}

void ContainerMetricsManager::RecordTiming(
    const std::string& container_id,
    MetricType type,
    base::TimeDelta duration) {
  RecordMetric(container_id, type, duration.InMillisecondsF());
}

void ContainerMetricsManager::OnContainerCreated(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerMetricsInfo* info = GetOrCreateContainerInfo(container_id);
  info->created_at = base::TimeTicks::Now();
  info->is_active = true;

  RecordMetric(container_id, MetricType::kContainerCreated);

  global_summary_.total_containers_created++;
  global_summary_.active_containers++;

  if (global_summary_.active_containers > global_summary_.peak_active_containers) {
    global_summary_.peak_active_containers = global_summary_.active_containers;
  }

  if (global_summary_.first_container_created.is_null()) {
    global_summary_.first_container_created = base::TimeTicks::Now();
  }
  global_summary_.last_container_created = base::TimeTicks::Now();

  NotifyGlobalMetricsUpdated();
}

void ContainerMetricsManager::OnContainerDestroyed(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_metrics_.find(container_id);
  if (it != container_metrics_.end()) {
    it->second->destroyed_at = base::TimeTicks::Now();
    it->second->is_active = false;

    base::TimeDelta lifetime = 
        it->second->destroyed_at - it->second->created_at;
    it->second->summary.lifetime = lifetime;

    RecordMetric(container_id, MetricType::kContainerDestroyed);

    global_summary_.total_containers_destroyed++;
    if (global_summary_.active_containers > 0) {
      global_summary_.active_containers--;
    }

    size_t destroyed = global_summary_.total_containers_destroyed;
    double current_avg = global_summary_.avg_container_lifetime_seconds;
    global_summary_.avg_container_lifetime_seconds = 
        (current_avg * (destroyed - 1) + lifetime.InSecondsF()) / destroyed;

    NotifyContainerMetricsUpdated(container_id);
    NotifyGlobalMetricsUpdated();
  }
}

void ContainerMetricsManager::OnContainerRestored(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerMetricsInfo* info = GetOrCreateContainerInfo(container_id);
  info->summary.restores++;

  RecordMetric(container_id, MetricType::kContainerRestored);
}

void ContainerMetricsManager::OnContainerSuspended(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerMetricsInfo* info = GetOrCreateContainerInfo(container_id);
  info->summary.suspends++;

  RecordMetric(container_id, MetricType::kContainerSuspended);
}

void ContainerMetricsManager::OnContainerResumed(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  RecordMetric(container_id, MetricType::kContainerResumed);
}

void ContainerMetricsManager::RecordNetworkRequestStart(
    const std::string& container_id,
    const std::string& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerMetricsInfo* info = GetOrCreateContainerInfo(container_id);
  info->pending_requests++;
  info->summary.network_requests++;

  RecordMetric(container_id, MetricType::kNetworkRequestStarted, 1.0,
               {{"url", url}});

  global_summary_.total_network_requests++;
}

void ContainerMetricsManager::RecordNetworkRequestComplete(
    const std::string& container_id,
    const std::string& url,
    int status_code,
    size_t bytes_received,
    base::TimeDelta duration) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerMetricsInfo* info = GetOrCreateContainerInfo(container_id);
  if (info->pending_requests > 0) {
    info->pending_requests--;
  }

  info->summary.network_requests_completed++;
  info->summary.bytes_received += bytes_received;

  size_t completed = info->summary.network_requests_completed;
  double current_avg = info->summary.avg_request_time_ms;
  info->summary.avg_request_time_ms = 
      (current_avg * (completed - 1) + duration.InMillisecondsF()) / completed;

  RecordMetric(container_id, MetricType::kNetworkRequestCompleted,
               duration.InMillisecondsF(),
               {{"url", url}, {"status", std::to_string(status_code)}});
  RecordMetric(container_id, MetricType::kNetworkBytesReceived,
               static_cast<double>(bytes_received));

  global_summary_.total_bytes_received += bytes_received;

  size_t total = global_summary_.total_network_requests;
  double global_avg = global_summary_.avg_network_request_time_ms;
  global_summary_.avg_network_request_time_ms =
      (global_avg * (total - 1) + duration.InMillisecondsF()) / total;
}

void ContainerMetricsManager::RecordNetworkRequestFailed(
    const std::string& container_id,
    const std::string& url,
    const std::string& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerMetricsInfo* info = GetOrCreateContainerInfo(container_id);
  if (info->pending_requests > 0) {
    info->pending_requests--;
  }

  info->summary.network_requests_failed++;

  RecordMetric(container_id, MetricType::kNetworkRequestFailed, 1.0,
               {{"url", url}, {"error", error}});
}

void ContainerMetricsManager::RecordProxyConnection(
    const std::string& container_id,
    const std::string& proxy,
    bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerMetricsInfo* info = GetOrCreateContainerInfo(container_id);

  if (success) {
    info->summary.proxy_connects++;
    RecordMetric(container_id, MetricType::kProxyConnected, 1.0,
                 {{"proxy", proxy}});
  } else {
    info->summary.proxy_failures++;
    RecordMetric(container_id, MetricType::kProxyFailed, 1.0,
                 {{"proxy", proxy}});
  }
}

void ContainerMetricsManager::RecordDnsResolution(
    const std::string& container_id,
    const std::string& host,
    bool success,
    base::TimeDelta duration) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerMetricsInfo* info = GetOrCreateContainerInfo(container_id);

  if (success) {
    info->summary.dns_lookups++;
    RecordMetric(container_id, MetricType::kDnsResolved,
                 duration.InMillisecondsF(), {{"host", host}});
  } else {
    info->summary.dns_failures++;
    RecordMetric(container_id, MetricType::kDnsFailed, 1.0,
                 {{"host", host}});
  }
}

void ContainerMetricsManager::RecordSecurityEvent(
    const std::string& container_id,
    const std::string& event_type,
    const std::string& details) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  MetricType type = MetricType::kContainerError;
  if (event_type == "permission_granted") {
    type = MetricType::kPermissionGranted;
  } else if (event_type == "permission_denied") {
    type = MetricType::kPermissionDenied;
  } else if (event_type == "csp_violation") {
    type = MetricType::kCSPViolation;
  }

  RecordMetric(container_id, type, 1.0, {{"details", details}});
}

void ContainerMetricsManager::RecordThreatBlocked(
    const std::string& container_id,
    const std::string& threat_type,
    const std::string& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerMetricsInfo* info = GetOrCreateContainerInfo(container_id);
  info->summary.threats_blocked++;

  RecordMetric(container_id, MetricType::kThreatBlocked, 1.0,
               {{"threat_type", threat_type}, {"url", url}});

  global_summary_.total_threats_blocked++;
}

void ContainerMetricsManager::RecordFingerprintEvent(
    const std::string& container_id,
    const std::string& technique,
    bool blocked) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerMetricsInfo* info = GetOrCreateContainerInfo(container_id);
  info->summary.fingerprint_attempts++;

  if (blocked) {
    info->summary.fingerprint_blocked++;
    RecordMetric(container_id, MetricType::kFingerprintBlocked, 1.0,
                 {{"technique", technique}});
    global_summary_.total_fingerprint_blocked++;
  } else {
    RecordMetric(container_id, MetricType::kFingerprintAttempt, 1.0,
                 {{"technique", technique}});
  }
}

void ContainerMetricsManager::RecordPageLoad(
    const std::string& container_id,
    const std::string& url,
    base::TimeDelta load_time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerMetricsInfo* info = GetOrCreateContainerInfo(container_id);

  double current = info->summary.avg_page_load_ms;
  double new_time = load_time.InMillisecondsF();
  info->summary.avg_page_load_ms = (current * 0.9 + new_time * 0.1);

  RecordMetric(container_id, MetricType::kPageLoadTime, new_time,
               {{"url", url}});

  double global_current = global_summary_.avg_page_load_ms;
  global_summary_.avg_page_load_ms = (global_current * 0.9 + new_time * 0.1);
}

void ContainerMetricsManager::RecordMemoryUsage(
    const std::string& container_id,
    size_t bytes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerMetricsInfo* info = GetOrCreateContainerInfo(container_id);

  if (bytes > info->summary.peak_memory_bytes) {
    info->summary.peak_memory_bytes = bytes;
  }

  RecordMetric(container_id, MetricType::kMemoryUsage,
               static_cast<double>(bytes));
}

void ContainerMetricsManager::RecordCpuUsage(
    const std::string& container_id,
    double percent) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerMetricsInfo* info = GetOrCreateContainerInfo(container_id);

  info->summary.avg_cpu_percent = 
      (info->summary.avg_cpu_percent * 0.9 + percent * 0.1);

  RecordMetric(container_id, MetricType::kCpuUsage, percent);
}

ContainerMetricsSummary ContainerMetricsManager::GetContainerSummary(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_metrics_.find(container_id);
  if (it == container_metrics_.end()) {
    ContainerMetricsSummary empty;
    empty.container_id = container_id;
    return empty;
  }

  return it->second->summary;
}

GlobalMetricsSummary ContainerMetricsManager::GetGlobalSummary() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return global_summary_;
}

std::vector<MetricDataPoint> ContainerMetricsManager::GetRecentMetrics(
    const std::string& container_id,
    size_t count) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_metrics_.find(container_id);
  if (it == container_metrics_.end()) {
    return {};
  }

  const auto& metrics = it->second->recent_metrics;
  if (metrics.size() <= count) {
    return std::vector<MetricDataPoint>(metrics.begin(), metrics.end());
  }

  return std::vector<MetricDataPoint>(
      metrics.end() - count, metrics.end());
}

std::vector<MetricDataPoint> ContainerMetricsManager::GetMetricsByType(
    const std::string& container_id,
    MetricType type,
    size_t count) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_metrics_.find(container_id);
  if (it == container_metrics_.end()) {
    return {};
  }

  std::vector<MetricDataPoint> result;
  for (auto rit = it->second->recent_metrics.rbegin();
       rit != it->second->recent_metrics.rend() && result.size() < count;
       ++rit) {
    if (rit->type == type) {
      result.push_back(*rit);
    }
  }

  std::reverse(result.begin(), result.end());
  return result;
}

const MetricTimeSeries* ContainerMetricsManager::GetTimeSeries(
    const std::string& container_id,
    const std::string& metric_name) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_metrics_.find(container_id);
  if (it == container_metrics_.end()) {
    return nullptr;
  }

  auto ts_it = it->second->time_series.find(metric_name);
  if (ts_it == it->second->time_series.end()) {
    return nullptr;
  }

  return &ts_it->second;
}

std::vector<std::string> ContainerMetricsManager::GetAllContainerIds() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<std::string> result;
  for (const auto& pair : container_metrics_) {
    result.push_back(pair.first);
  }
  return result;
}

double ContainerMetricsManager::GetAggregatedMetric(
    MetricType type,
    const std::string& aggregation) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::string metric_name = MetricTypeToString(type);
  std::vector<double> values;

  for (const auto& pair : container_metrics_) {
    auto ts_it = pair.second->time_series.find(metric_name);
    if (ts_it != pair.second->time_series.end() && 
        !ts_it->second.data_points.empty()) {
      values.push_back(ts_it->second.GetLatest());
    }
  }

  if (values.empty()) {
    return 0.0;
  }

  if (aggregation == "sum") {
    return std::accumulate(values.begin(), values.end(), 0.0);
  } else if (aggregation == "avg" || aggregation == "average") {
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
  } else if (aggregation == "min") {
    return *std::min_element(values.begin(), values.end());
  } else if (aggregation == "max") {
    return *std::max_element(values.begin(), values.end());
  } else if (aggregation == "count") {
    return static_cast<double>(values.size());
  }

  return std::accumulate(values.begin(), values.end(), 0.0);
}

double ContainerMetricsManager::GetMetricRate(
    const std::string& container_id,
    MetricType type,
    base::TimeDelta window) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_metrics_.find(container_id);
  if (it == container_metrics_.end()) {
    return 0.0;
  }

  std::string metric_name = MetricTypeToString(type);
  auto ts_it = it->second->time_series.find(metric_name);
  if (ts_it == it->second->time_series.end()) {
    return 0.0;
  }

  size_t count = ts_it->second.GetCount(window);
  double seconds = window.InSecondsF();

  return seconds > 0 ? count / seconds : 0.0;
}

double ContainerMetricsManager::GetPercentile(
    const std::string& container_id,
    MetricType type,
    double percentile) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_metrics_.find(container_id);
  if (it == container_metrics_.end()) {
    return 0.0;
  }

  std::string metric_name = MetricTypeToString(type);
  auto ts_it = it->second->time_series.find(metric_name);
  if (ts_it == it->second->time_series.end() || 
      ts_it->second.data_points.empty()) {
    return 0.0;
  }

  std::vector<double> values;
  for (const auto& point : ts_it->second.data_points) {
    values.push_back(point.second);
  }

  std::sort(values.begin(), values.end());

  size_t index = static_cast<size_t>(
      std::ceil(percentile / 100.0 * values.size())) - 1;
  index = std::min(index, values.size() - 1);

  return values[index];
}

std::string ContainerMetricsManager::ExportToJson() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::DictValue root;

  base::DictValue global;
  global.Set("total_containers_created", 
             static_cast<int>(global_summary_.total_containers_created));
  global.Set("total_containers_destroyed",
             static_cast<int>(global_summary_.total_containers_destroyed));
  global.Set("active_containers",
             static_cast<int>(global_summary_.active_containers));
  global.Set("peak_active_containers",
             static_cast<int>(global_summary_.peak_active_containers));
  global.Set("total_network_requests",
             static_cast<int>(global_summary_.total_network_requests));
  global.Set("total_bytes_received",
             static_cast<double>(global_summary_.total_bytes_received));
  global.Set("total_threats_blocked",
             static_cast<int>(global_summary_.total_threats_blocked));
  global.Set("avg_network_request_time_ms",
             global_summary_.avg_network_request_time_ms);
  root.Set("global", std::move(global));

  base::ListValue containers;
  for (const auto& pair : container_metrics_) {
    const auto& summary = pair.second->summary;
    base::DictValue container;
    container.Set("container_id", pair.first);
    container.Set("network_requests", static_cast<int>(summary.network_requests));
    container.Set("bytes_received", static_cast<double>(summary.bytes_received));
    container.Set("threats_blocked", static_cast<int>(summary.threats_blocked));
    container.Set("avg_page_load_ms", summary.avg_page_load_ms);
    container.Set("is_active", pair.second->is_active);
    containers.Append(std::move(container));
  }
  root.Set("containers", std::move(containers));

  std::string output;
  base::JSONWriter::WriteWithOptions(
      root, base::JSONWriter::OPTIONS_PRETTY_PRINT, &output);
  return output;
}

std::string ContainerMetricsManager::ExportToPrometheus() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::stringstream output;

  output << "# HELP container_total_created Total containers created\n";
  output << "# TYPE container_total_created counter\n";
  output << "container_total_created " 
         << global_summary_.total_containers_created << "\n\n";

  output << "# HELP container_active Active containers\n";
  output << "# TYPE container_active gauge\n";
  output << "container_active " << global_summary_.active_containers << "\n\n";

  output << "# HELP network_requests_total Total network requests\n";
  output << "# TYPE network_requests_total counter\n";
  output << "network_requests_total " 
         << global_summary_.total_network_requests << "\n\n";

  output << "# HELP network_bytes_received_total Total bytes received\n";
  output << "# TYPE network_bytes_received_total counter\n";
  output << "network_bytes_received_total " 
         << global_summary_.total_bytes_received << "\n\n";

  output << "# HELP threats_blocked_total Total threats blocked\n";
  output << "# TYPE threats_blocked_total counter\n";
  output << "threats_blocked_total " 
         << global_summary_.total_threats_blocked << "\n\n";

  for (const auto& pair : container_metrics_) {
    const std::string& cid = pair.first;
    const auto& summary = pair.second->summary;

    output << "container_network_requests{container=\"" << cid << "\"} "
           << summary.network_requests << "\n";
    output << "container_bytes_received{container=\"" << cid << "\"} "
           << summary.bytes_received << "\n";
    output << "container_threats_blocked{container=\"" << cid << "\"} "
           << summary.threats_blocked << "\n";
  }

  return output.str();
}

std::string ContainerMetricsManager::GetDiagnosticReport() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::stringstream report;
  report << "=== ContainerMetricsManager Diagnostic Report ===\n\n";

  report << "Global Summary:\n";
  report << "  Total Containers Created: " 
         << global_summary_.total_containers_created << "\n";
  report << "  Total Containers Destroyed: " 
         << global_summary_.total_containers_destroyed << "\n";
  report << "  Active Containers: " << global_summary_.active_containers << "\n";
  report << "  Peak Active: " << global_summary_.peak_active_containers << "\n";
  report << "  Total Network Requests: " 
         << global_summary_.total_network_requests << "\n";
  report << "  Total Bytes Received: " 
         << global_summary_.total_bytes_received << "\n";
  report << "  Total Threats Blocked: " 
         << global_summary_.total_threats_blocked << "\n";
  report << "  Avg Container Lifetime: " 
         << global_summary_.avg_container_lifetime_seconds << " seconds\n";
  report << "  Avg Network Request Time: " 
         << global_summary_.avg_network_request_time_ms << " ms\n\n";

  report << "Containers: " << container_metrics_.size() << "\n";
  for (const auto& pair : container_metrics_) {
    const auto* info = pair.second.get();
    report << "  - " << pair.first;
    if (!info->is_active) report << " (inactive)";
    report << ":\n";
    report << "      Network Requests: " << info->summary.network_requests << "\n";
    report << "      Bytes Received: " << info->summary.bytes_received << "\n";
    report << "      Threats Blocked: " << info->summary.threats_blocked << "\n";
    report << "      Avg Page Load: " << info->summary.avg_page_load_ms << " ms\n";
    report << "      Recent Metrics: " << info->recent_metrics.size() << "\n";
  }

  return report.str();
}

void ContainerMetricsManager::DumpStateToLog() const {
  ;
}

void ContainerMetricsManager::SetEnabled(bool enabled) {
  enabled_ = enabled;
}

bool ContainerMetricsManager::IsEnabled() const {
  return enabled_;
}

void ContainerMetricsManager::SetRetentionPeriod(base::TimeDelta period) {
  retention_period_ = period;
}

void ContainerMetricsManager::SetMaxDataPoints(size_t max_points) {
  max_data_points_ = max_points;
}

void ContainerMetricsManager::SetDebugLoggingEnabled(bool enabled) {
  debug_logging_enabled_ = enabled;
}

ContainerMetricsManager::ContainerMetricsInfo*
ContainerMetricsManager::GetOrCreateContainerInfo(
    const std::string& container_id) {
  auto it = container_metrics_.find(container_id);
  if (it != container_metrics_.end()) {
    return it->second.get();
  }

  auto info = std::make_unique<ContainerMetricsInfo>();
  info->container_id = container_id;
  info->created_at = base::TimeTicks::Now();
  info->summary.container_id = container_id;
  info->summary.created_at = base::TimeTicks::Now();

  auto* ptr = info.get();
  container_metrics_[container_id] = std::move(info);

  return ptr;
}

void ContainerMetricsManager::UpdateContainerSummary(
    const std::string& container_id) {

  NotifyContainerMetricsUpdated(container_id);
}

void ContainerMetricsManager::UpdateGlobalSummary() {

  NotifyGlobalMetricsUpdated();
}

void ContainerMetricsManager::CleanupOldData() {
  base::TimeTicks cutoff = base::TimeTicks::Now() - retention_period_;

  for (auto& pair : container_metrics_) {
    auto& metrics = pair.second->recent_metrics;
    while (!metrics.empty() && metrics.front().timestamp < cutoff) {
      metrics.pop_front();
    }

    for (auto& ts_pair : pair.second->time_series) {
      auto& data = ts_pair.second.data_points;
      while (!data.empty() && data.front().first < cutoff) {
        data.pop_front();
      }
    }
  }

  while (!global_metrics_.empty() && 
         global_metrics_.front().timestamp < cutoff) {
    global_metrics_.pop_front();
  }
}

void ContainerMetricsManager::NotifyMetricRecorded(
    const MetricDataPoint& metric) {
  for (auto& observer : observers_) {
    observer.OnMetricRecorded(metric);
  }
}

void ContainerMetricsManager::NotifyContainerMetricsUpdated(
    const std::string& container_id) {
  auto it = container_metrics_.find(container_id);
  if (it == container_metrics_.end()) {
    return;
  }

  for (auto& observer : observers_) {
    observer.OnContainerMetricsUpdated(container_id, it->second->summary);
  }
}

void ContainerMetricsManager::NotifyGlobalMetricsUpdated() {
  for (auto& observer : observers_) {
    observer.OnGlobalMetricsUpdated(global_summary_);
  }
}

}  
