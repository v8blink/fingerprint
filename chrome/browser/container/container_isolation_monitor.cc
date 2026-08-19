
#include "chrome/browser/container/container_isolation_monitor.h"

#include <algorithm>

#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/time/time.h"

namespace tab_container {

ContainerIsolationMonitor* ContainerIsolationMonitor::GetInstance() {
  static base::NoDestructor<ContainerIsolationMonitor> instance;
  return instance.get();
}

ContainerIsolationMonitor::ContainerIsolationMonitor() = default;

ContainerIsolationMonitor::~ContainerIsolationMonitor() = default;

void ContainerIsolationMonitor::RecordEvent(const IsolationEvent& event) {
  events_.push_back(event);

  if (events_.size() > max_events_) {
    events_.erase(events_.begin(),
                  events_.begin() + (events_.size() - max_events_));
  }

  if (event.type == IsolationEvent::REGISTRATION_MISMATCH ||
      event.type == IsolationEvent::ACTIVATION_MISMATCH ||
      event.type == IsolationEvent::FETCH_MISMATCH ||
      event.type == IsolationEvent::CACHE_STORAGE_MISMATCH) {
    ;
  }
}

std::vector<IsolationEvent> ContainerIsolationMonitor::GetRecentEvents(
    size_t count) const {
  size_t start = events_.size() > count ? events_.size() - count : 0;
  return std::vector<IsolationEvent>(events_.begin() + start, events_.end());
}

std::vector<IsolationEvent> ContainerIsolationMonitor::GetEventsInTimeWindow(
    base::TimeDelta window) const {
  base::TimeTicks cutoff = base::TimeTicks::Now() - window;
  std::vector<IsolationEvent> result;
  for (const auto& event : events_) {
    if (event.timestamp >= cutoff) {
      result.push_back(event);
    }
  }
  return result;
}

ContainerIsolationMonitor::Statistics
ContainerIsolationMonitor::GetStatistics() const {
  Statistics stats;
  stats.total_events = events_.size();

  for (const auto& event : events_) {
    switch (event.type) {
      case IsolationEvent::REGISTRATION_VALID:
        stats.valid_events++;
        stats.registration_events++;
        break;
      case IsolationEvent::REGISTRATION_MISMATCH:
        stats.mismatch_events++;
        stats.registration_events++;
        break;
      case IsolationEvent::ACTIVATION_VALID:
        stats.valid_events++;
        stats.activation_events++;
        break;
      case IsolationEvent::ACTIVATION_MISMATCH:
        stats.mismatch_events++;
        stats.activation_events++;
        break;
      case IsolationEvent::FETCH_VALID:
        stats.valid_events++;
        stats.fetch_events++;
        break;
      case IsolationEvent::FETCH_MISMATCH:
        stats.mismatch_events++;
        stats.fetch_events++;
        break;
      case IsolationEvent::CACHE_STORAGE_VALID:
        stats.valid_events++;
        stats.cache_storage_events++;
        break;
      case IsolationEvent::CACHE_STORAGE_MISMATCH:
        stats.mismatch_events++;
        stats.cache_storage_events++;
        break;
      default:
        break;
    }
  }

  return stats;
}

void ContainerIsolationMonitor::ClearEvents() {
  events_.clear();
}

}  
