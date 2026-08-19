
#ifndef CHROME_BROWSER_CONTAINER_SERVICE_WORKER_CONTAINER_ISOLATION_H_
#define CHROME_BROWSER_CONTAINER_SERVICE_WORKER_CONTAINER_ISOLATION_H_

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

bool ValidateServiceWorkerRegistrationPartition(
    content::WebContents* web_contents,
    content::StoragePartition* storage_partition,
    const GURL& scope,
    const blink::StorageKey& storage_key);

bool ValidateServiceWorkerActivationPartition(
    content::WebContents* web_contents,
    content::StoragePartition* storage_partition,
    const GURL& scope,
    const blink::StorageKey& storage_key);

bool ValidateServiceWorkerFetchPartition(
    content::WebContents* web_contents,
    content::StoragePartition* storage_partition,
    const GURL& request_url,
    const blink::StorageKey& storage_key);

std::string GetExpectedPartitionIdForWebContents(
    content::WebContents* web_contents);

std::string GetPartitionIdFromStoragePartition(
    content::StoragePartition* storage_partition);

bool ValidatePartitionIdsMatch(const std::string& expected_partition_id,
                               const std::string& actual_partition_id);

bool ShouldAllowServiceWorkerRegistration(
    content::WebContents* web_contents,
    content::StoragePartition* storage_partition,
    const GURL& scope,
    const blink::StorageKey& storage_key);

void LogServiceWorkerContainerIsolationEvent(
    const std::string& event_type,
    content::WebContents* web_contents,
    const std::string& partition_id,
    const GURL& url,
    const std::string& details);

bool ValidateContainerPartitionForRapidTabLifecycle(
    content::WebContents* web_contents,
    content::StoragePartition* storage_partition);

bool ValidateContainerPartitionForNavigation(
    content::WebContents* web_contents,
    content::StoragePartition* storage_partition,
    const GURL& navigation_url);

}  

#endif  
