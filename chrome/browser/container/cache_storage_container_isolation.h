
#ifndef CHROME_BROWSER_CONTAINER_CACHE_STORAGE_CONTAINER_ISOLATION_H_
#define CHROME_BROWSER_CONTAINER_CACHE_STORAGE_CONTAINER_ISOLATION_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
class StoragePartition;
}  

namespace tab_container {

bool ValidateCacheStoragePartition(
    content::WebContents* web_contents,
    content::StoragePartition* storage_partition,
    const blink::StorageKey& storage_key);

bool ValidateCacheStorageAccessPartition(
    content::WebContents* web_contents,
    content::StoragePartition* storage_partition,
    const std::string& cache_name,
    const blink::StorageKey& storage_key);

void LogCacheStorageContainerIsolationEvent(
    const std::string& event_type,
    content::WebContents* web_contents,
    const std::string& partition_id,
    const std::string& cache_name,
    const std::string& details);

bool ValidateContainerPartitionForCacheStorageCleanup(
    content::WebContents* web_contents,
    content::StoragePartition* storage_partition);

}  

#endif  
