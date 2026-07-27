
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_PARTITION_TRACKER_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_PARTITION_TRACKER_H_

#include <map>
#include <set>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/singleton.h"
#include "base/synchronization/lock.h"
#include "base/time/time.h"
#include "content/public/browser/storage_partition.h"

namespace content {
class WebContents;
}  

namespace tab_container {

class ContainerPartitionTracker {
 public:
  static ContainerPartitionTracker* GetInstance();

  ContainerPartitionTracker(const ContainerPartitionTracker&) = delete;
  ContainerPartitionTracker& operator=(const ContainerPartitionTracker&) =
      delete;

  void RegisterPartition(content::StoragePartition* storage_partition,
                        const std::string& partition_id,
                        content::WebContents* web_contents);

  void UnregisterPartition(content::StoragePartition* storage_partition);

  std::string GetPartitionId(content::StoragePartition* storage_partition)
      const;

  content::WebContents* GetWebContents(
      content::StoragePartition* storage_partition) const;

  bool IsContainerPartition(content::StoragePartition* storage_partition) const;

  std::set<std::string> GetAllPartitionIds() const;

  void ClearAll();

 private:
  friend struct base::DefaultSingletonTraits<ContainerPartitionTracker>;

  ContainerPartitionTracker();
  ~ContainerPartitionTracker();

  struct PartitionInfo {
    std::string partition_id;
    raw_ptr<content::WebContents> web_contents;
    base::TimeTicks registered_at;

    PartitionInfo(const std::string& pid, content::WebContents* wc)
        : partition_id(pid), web_contents(wc), registered_at(base::TimeTicks::Now()) {}
  };

  mutable base::Lock lock_;
  std::map<content::StoragePartition*, PartitionInfo> partition_to_info_;
  std::map<std::string, content::StoragePartition*> partition_id_to_partition_;
};

}  

#endif  
