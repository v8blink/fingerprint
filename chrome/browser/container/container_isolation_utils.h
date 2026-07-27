
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_ISOLATION_UTILS_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_ISOLATION_UTILS_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
class RenderFrameHost;
}  

namespace tab_container {

content::WebContents* GetWebContentsFromRenderFrameHost(
    content::RenderFrameHost* rfh);

content::StoragePartition* GetStoragePartitionForWebContents(
    content::WebContents* web_contents);

bool IsUrlSafeForContainerIsolation(const GURL& url);

bool IsValidPartitionIdFormat(const std::string& partition_id);

std::string CreateContainerIsolationDebugString(
    content::WebContents* web_contents,
    content::StoragePartition* storage_partition,
    const std::string& operation,
    const GURL& url);

std::string GetContainerIsolationTraceEventName(const std::string& operation);

}  

#endif  
