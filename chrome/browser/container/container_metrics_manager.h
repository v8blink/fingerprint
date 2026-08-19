
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_METRICS_MANAGER_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_METRICS_MANAGER_H_

#include <deque>
#include <map>
#include <memory>
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

enum class MetricType {

  kContainerCreated,
  kContainerDestroyed,
  kContainerRestored,
  kContainerSuspended,
  kContainerResumed,
  kContainerError,

  kNetworkRequestStarted,
  kNetworkRequestCompleted,
  kNetworkRequestFailed,
  kNetworkBytesReceived,
  kNetworkBytesSent,
  kProxyConnected,
  kProxyFailed,
  kDnsResolved,
  kDnsFailed,

  kCookieSet,
  kCookieBlocked,
  kStorageUsed,
  kCacheHit,
  kCacheMiss,

  kThreatBlocked,
  kPermissionGranted,
  kPermissionDenied,
  kCSPViolation,

  kFingerprintAttempt,
  kFingerprintBlocked,

  kPageLoadTime,
  kResourceLoadTime,
  kMemoryUsage,
  kCpuUsage,
};

struct MetricDataPoint {
  base::TimeTicks timestamp;
  std::string container_id;
  MetricType type;
  double value;
  std::map<std::string, std::string> labels;

  MetricDataPoint() = default;
};

struct MetricTimeSeries {
  std::string metric_name;
  std::deque<std::pair<base::TimeTicks, double>> data_points;
  size_t max_points = 1000;

  void AddPoint(base::TimeTicks time, double value);
  double GetLatest() const;
  double GetAverage(base::TimeDelta window) const;
  double GetMin(base::TimeDelta window) const;
  double GetMax(base::TimeDelta window) const;
  double GetSum(base::TimeDelta window) const;
  size_t GetCount(base::TimeDelta window) const;
};

struct ContainerMetricsSummary {
  std::string container_id;
  base::TimeTicks created_at;
  base::TimeDelta lifetime;

  size_t restores = 0;
  size_t suspends = 0;
  size_t errors = 0;

  size_t network_requests = 0;
  size_t network_requests_completed = 0;
  size_t network_requests_failed = 0;
  size_t bytes_received = 0;
  size_t bytes_sent = 0;
  double avg_request_time_ms = 0;
  size_t proxy_connects = 0;
  size_t proxy_failures = 0;
  size_t dns_lookups = 0;
  size_t dns_failures = 0;

  size_t cookies_set = 0;
  size_t cookies_blocked = 0;
  size_t storage_bytes = 0;
  size_t cache_hits = 0;
  size_t cache_misses = 0;

  size_t threats_blocked = 0;
  size_t permissions_granted = 0;
  size_t permissions_denied = 0;
  size_t csp_violations = 0;

  size_t fingerprint_attempts = 0;
  size_t fingerprint_blocked = 0;

  double avg_page_load_ms = 0;
  double avg_resource_load_ms = 0;
  size_t peak_memory_bytes = 0;
  double avg_cpu_percent = 0;
};

struct GlobalMetricsSummary {
  size_t total_containers_created = 0;
  size_t total_containers_destroyed = 0;
  size_t active_containers = 0;
  size_t peak_active_containers = 0;

  size_t total_network_requests = 0;
  size_t total_bytes_received = 0;
  size_t total_bytes_sent = 0;

  size_t total_threats_blocked = 0;
  size_t total_fingerprint_blocked = 0;

  double avg_container_lifetime_seconds = 0;
  double avg_network_request_time_ms = 0;
  double avg_page_load_ms = 0;

  base::TimeTicks first_container_created;
  base::TimeTicks last_container_created;
};

class ContainerMetricsObserver : public base::CheckedObserver {
 public:
  ~ContainerMetricsObserver() override = default;

  virtual void OnMetricRecorded(const MetricDataPoint& metric) {}
  virtual void OnContainerMetricsUpdated(const std::string& container_id,
                                         const ContainerMetricsSummary& summary) {}
  virtual void OnGlobalMetricsUpdated(const GlobalMetricsSummary& summary) {}
};

class ContainerMetricsManager {
 public:
  explicit ContainerMetricsManager(content::BrowserContext* browser_context);
  ~ContainerMetricsManager();

  ContainerMetricsManager(const ContainerMetricsManager&) = delete;
  ContainerMetricsManager& operator=(const ContainerMetricsManager&) = delete;

  void AddObserver(ContainerMetricsObserver* observer);
  void RemoveObserver(ContainerMetricsObserver* observer);

  void RecordMetric(const std::string& container_id,
                    MetricType type,
                    double value = 1.0,
                    const std::map<std::string, std::string>& labels = {});

  void IncrementCounter(const std::string& container_id,
                        MetricType type,
                        double amount = 1.0);

  void RecordGauge(const std::string& container_id,
                   MetricType type,
                   double value);

  void RecordHistogram(const std::string& container_id,
                       MetricType type,
                       double value);

  void RecordTiming(const std::string& container_id,
                    MetricType type,
                    base::TimeDelta duration);

  void OnContainerCreated(const std::string& container_id);

  void OnContainerDestroyed(const std::string& container_id);

  void OnContainerRestored(const std::string& container_id);

  void OnContainerSuspended(const std::string& container_id);

  void OnContainerResumed(const std::string& container_id);

  void RecordNetworkRequestStart(const std::string& container_id,
                                 const std::string& url);

  void RecordNetworkRequestComplete(const std::string& container_id,
                                    const std::string& url,
                                    int status_code,
                                    size_t bytes_received,
                                    base::TimeDelta duration);

  void RecordNetworkRequestFailed(const std::string& container_id,
                                  const std::string& url,
                                  const std::string& error);

  void RecordProxyConnection(const std::string& container_id,
                             const std::string& proxy,
                             bool success);

  void RecordDnsResolution(const std::string& container_id,
                           const std::string& host,
                           bool success,
                           base::TimeDelta duration);

  void RecordSecurityEvent(const std::string& container_id,
                           const std::string& event_type,
                           const std::string& details);

  void RecordThreatBlocked(const std::string& container_id,
                           const std::string& threat_type,
                           const std::string& url);

  void RecordFingerprintEvent(const std::string& container_id,
                              const std::string& technique,
                              bool blocked);

  void RecordPageLoad(const std::string& container_id,
                      const std::string& url,
                      base::TimeDelta load_time);

  void RecordMemoryUsage(const std::string& container_id,
                         size_t bytes);

  void RecordCpuUsage(const std::string& container_id,
                      double percent);

  ContainerMetricsSummary GetContainerSummary(
      const std::string& container_id) const;

  GlobalMetricsSummary GetGlobalSummary() const;

  std::vector<MetricDataPoint> GetRecentMetrics(
      const std::string& container_id,
      size_t count = 100) const;

  std::vector<MetricDataPoint> GetMetricsByType(
      const std::string& container_id,
      MetricType type,
      size_t count = 100) const;

  const MetricTimeSeries* GetTimeSeries(
      const std::string& container_id,
      const std::string& metric_name) const;

  std::vector<std::string> GetAllContainerIds() const;

  double GetAggregatedMetric(MetricType type,
                             const std::string& aggregation = "sum") const;

  double GetMetricRate(const std::string& container_id,
                       MetricType type,
                       base::TimeDelta window) const;

  double GetPercentile(const std::string& container_id,
                       MetricType type,
                       double percentile) const;

  std::string ExportToJson() const;

  std::string ExportToPrometheus() const;

  std::string GetDiagnosticReport() const;

  void DumpStateToLog() const;

  void SetEnabled(bool enabled);
  bool IsEnabled() const;

  void SetRetentionPeriod(base::TimeDelta period);

  void SetMaxDataPoints(size_t max_points);

  void SetDebugLoggingEnabled(bool enabled);

 private:

  struct ContainerMetricsInfo {
    std::string container_id;
    base::TimeTicks created_at;
    base::TimeTicks destroyed_at;
    bool is_active = true;

    ContainerMetricsSummary summary;
    std::deque<MetricDataPoint> recent_metrics;
    std::map<std::string, MetricTimeSeries> time_series;

    size_t pending_requests = 0;
    base::TimeTicks last_activity;
  };

  ContainerMetricsInfo* GetOrCreateContainerInfo(
      const std::string& container_id);

  void UpdateContainerSummary(const std::string& container_id);
  void UpdateGlobalSummary();

  void CleanupOldData();

  void NotifyMetricRecorded(const MetricDataPoint& metric);
  void NotifyContainerMetricsUpdated(const std::string& container_id);
  void NotifyGlobalMetricsUpdated();

  raw_ptr<content::BrowserContext> browser_context_;

  std::map<std::string, std::unique_ptr<ContainerMetricsInfo>> container_metrics_;

  GlobalMetricsSummary global_summary_;
  std::deque<MetricDataPoint> global_metrics_;

  bool enabled_ = true;
  base::TimeDelta retention_period_ = base::Hours(24);
  size_t max_data_points_ = 1000;
  bool debug_logging_enabled_ = false;

  base::ObserverList<ContainerMetricsObserver> observers_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ContainerMetricsManager> weak_factory_{this};
};

}  

#endif  
