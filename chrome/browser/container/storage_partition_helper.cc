
#include "chrome/browser/container/storage_partition_helper.h"

#include "base/logging.h"
#include "chrome/browser/container/tab_container_manager_factory.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"

namespace tab_container {

namespace {
constexpr const char* kLogPrefix = "[StoragePartitionHelper]";
}  

content::StoragePartitionConfig GetStoragePartitionConfigForWebContents(
    content::WebContents* web_contents,
    const content::StoragePartitionConfig& default_config) {
  if (!web_contents) {
    ;
    return default_config;
  }

  content::BrowserContext* browser_context =
      web_contents->GetBrowserContext();
  if (!browser_context) {
    ;
    return default_config;
  }

  TabContainerManager* container_manager =
      tab_container::GetForBrowserContext(browser_context);
  if (!container_manager) {
    ;
    return default_config;
  }

  std::string partition_id =
      container_manager->GetPartitionIdForTab(web_contents);
  if (partition_id.empty()) {
    ;
    return default_config;
  }

  if (!container_manager->IsPartitionIdInUse(partition_id)) {
    ;
    return default_config;
  }

  content::StoragePartitionConfig container_config =
      content::StoragePartitionConfig::Create(
          browser_context, "tab_container", partition_id,
          browser_context->IsOffTheRecord());

  ;

#ifdef CHROME_BROWSER_CONTAINER_CONTAINER_PARTITION_TRACKER_H_

  ;
#endif

  return container_config;
}

content::StoragePartitionConfig GetStoragePartitionConfigWithContainer(
    content::BrowserContext* browser_context,
    content::WebContents* web_contents,
    const content::StoragePartitionConfig& default_config) {
  if (!web_contents) {
    return default_config;
  }

  return GetStoragePartitionConfigForWebContents(web_contents, default_config);
}

}  
