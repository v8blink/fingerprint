
#include "chrome/browser/container/container_partition_tracker.h"

#include "base/logging.h"
#include "base/memory/singleton.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"

namespace tab_container {

namespace {
constexpr const char* kLogPrefix = "[ContainerPartitionTracker]";
}  

ContainerPartitionTracker* ContainerPartitionTracker::GetInstance() {
  return base::Singleton<ContainerPartitionTracker>::get();
}

ContainerPartitionTracker::ContainerPartitionTracker() = default;

ContainerPartitionTracker::~ContainerPartitionTracker() = default;

void ContainerPartitionTracker::RegisterPartition(
    content::StoragePartition* storage_partition,
    const std::string& partition_id,
    content::WebContents* web_contents) {
  base::AutoLock lock(lock_);

  if (!storage_partition || partition_id.empty()) {
    ;
    return;
  }

  auto existing = partition_to_info_.find(storage_partition);
  if (existing != partition_to_info_.end()) {
    ;
    existing->second.partition_id = partition_id;
    existing->second.web_contents = web_contents;
    partition_id_to_partition_[partition_id] = storage_partition;
    return;
  }

  partition_to_info_.emplace(storage_partition,
                             PartitionInfo(partition_id, web_contents));
  partition_id_to_partition_[partition_id] = storage_partition;

  ;
}

void ContainerPartitionTracker::UnregisterPartition(
    content::StoragePartition* storage_partition) {
  base::AutoLock lock(lock_);

  auto it = partition_to_info_.find(storage_partition);
  if (it == partition_to_info_.end()) {
    ;
    return;
  }

  const std::string partition_id = it->second.partition_id;
  partition_id_to_partition_.erase(partition_id);
  partition_to_info_.erase(it);

  ;
}

std::string ContainerPartitionTracker::GetPartitionId(
    content::StoragePartition* storage_partition) const {
  base::AutoLock lock(lock_);

  auto it = partition_to_info_.find(storage_partition);
  if (it == partition_to_info_.end()) {
    return std::string();
  }

  return it->second.partition_id;
}

content::WebContents* ContainerPartitionTracker::GetWebContents(
    content::StoragePartition* storage_partition) const {
  base::AutoLock lock(lock_);

  auto it = partition_to_info_.find(storage_partition);
  if (it == partition_to_info_.end()) {
    return nullptr;
  }

  return it->second.web_contents;
}

bool ContainerPartitionTracker::IsContainerPartition(
    content::StoragePartition* storage_partition) const {
  base::AutoLock lock(lock_);
  return partition_to_info_.find(storage_partition) != partition_to_info_.end();
}

std::set<std::string> ContainerPartitionTracker::GetAllPartitionIds() const {
  base::AutoLock lock(lock_);

  std::set<std::string> partition_ids;
  for (const auto& pair : partition_to_info_) {
    partition_ids.insert(pair.second.partition_id);
  }
  return partition_ids;
}

void ContainerPartitionTracker::ClearAll() {
  base::AutoLock lock(lock_);
  partition_to_info_.clear();
  partition_id_to_partition_.clear();
}

}  
