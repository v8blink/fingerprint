
#ifndef CHROME_BROWSER_CONTAINER_STORAGE_PARTITION_HELPER_H_
#define CHROME_BROWSER_CONTAINER_STORAGE_PARTITION_HELPER_H_

#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"

namespace content {
class BrowserContext;
}  

namespace tab_container {

content::StoragePartitionConfig GetStoragePartitionConfigForWebContents(
    content::WebContents* web_contents,
    const content::StoragePartitionConfig& default_config);

content::StoragePartitionConfig GetStoragePartitionConfigWithContainer(
    content::BrowserContext* browser_context,
    content::WebContents* web_contents,
    const content::StoragePartitionConfig& default_config);

}  

#endif  
