
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_ISOLATION_MONITOR_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_ISOLATION_MONITOR_H_

#include <map>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "url/gurl.h"

namespace content {
class WebContents;
}  

namespace tab_container {

struct IsolationEvent {
  enum Type {
    REGISTRATION_VALID,
    REGISTRATION_MISMATCH,
    ACTIVATION_VALID,
    ACTIVATION_MISMATCH,
    FETCH_VALID,
    FETCH_MISMATCH,
    CACHE_STORAGE_VALID,
    CACHE_STORAGE_MISMATCH,
    TAB_CLOSE_DURING_OPERATION,
    NAVIGATION_PARTITION_CHECK,
  };

  Type type;
  base::TimeTicks timestamp;
  std::string partition_id;
  GURL url;
  std::string details;
  raw_ptr<content::WebContents> web_contents;

  IsolationEvent(Type t, const std::string& pid, const GURL& u,
                 const std::string& d, content::WebContents* wc)
      : type(t),
        timestamp(base::TimeTicks::Now()),
        partition_id(pid),
        url(u),
        details(d),
        web_contents(wc) {}
};

class ContainerIsolationMonitor {
 public:
  static ContainerIsolationMonitor* GetInstance();

  ContainerIsolationMonitor(const ContainerIsolationMonitor&) = delete;
  ContainerIsolationMonitor& operator=(const ContainerIsolationMonitor&) =
      delete;

  void RecordEvent(const IsolationEvent& event);

  std::vector<IsolationEvent> GetRecentEvents(size_t count = 100) const;
  std::vector<IsolationEvent> GetEventsInTimeWindow(
      base::TimeDelta window) const;

  struct Statistics {
    size_t total_events = 0;
    size_t valid_events = 0;
    size_t mismatch_events = 0;
    size_t registration_events = 0;
    size_t activation_events = 0;
    size_t fetch_events = 0;
    size_t cache_storage_events = 0;
  };
  Statistics GetStatistics() const;

  void ClearEvents();

 private:
  ContainerIsolationMonitor();
  ~ContainerIsolationMonitor();

  mutable std::vector<IsolationEvent> events_;
  mutable size_t max_events_ = 1000;  
};

}  

#endif  
