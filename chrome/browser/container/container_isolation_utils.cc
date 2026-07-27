
#include "chrome/browser/container/container_isolation_utils.h"

#include <cstring>

#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/container/container_partition_tracker.h"
#include "chrome/browser/container/service_worker_container_isolation.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace tab_container {

namespace {
constexpr const char* kLogPrefix = "[ContainerIsolationUtils]";
constexpr const char* kPartitionIdPrefix = "tab_partition_";
}  

content::WebContents* GetWebContentsFromRenderFrameHost(
    content::RenderFrameHost* rfh) {
  if (!rfh) {
    return nullptr;
  }

  return content::WebContents::FromRenderFrameHost(rfh);
}

content::StoragePartition* GetStoragePartitionForWebContents(
    content::WebContents* web_contents) {
  if (!web_contents) {
    return nullptr;
  }

  content::BrowserContext* browser_context =
      web_contents->GetBrowserContext();
  if (!browser_context) {
    return nullptr;
  }

  return browser_context->GetStoragePartition(
      web_contents->GetSiteInstance());
}

bool IsUrlSafeForContainerIsolation(const GURL& url) {
  if (!url.is_valid()) {
    return false;
  }

  if (url.SchemeIs("chrome") || url.SchemeIs("chrome-extension") ||
      url.SchemeIs("about") || url.SchemeIs("data") ||
      url.SchemeIs("blob") || url.SchemeIs("file")) {
    return false;
  }

  return url.SchemeIsHTTPOrHTTPS() || url.SchemeIs("ws") ||
         url.SchemeIs("wss");
}

bool IsValidPartitionIdFormat(const std::string& partition_id) {
  if (partition_id.empty()) {
    return false;
  }

  if (!base::StartsWith(partition_id, kPartitionIdPrefix,
                        base::CompareCase::SENSITIVE)) {
    return false;
  }

  if (partition_id.length() <= strlen(kPartitionIdPrefix)) {
    return false;
  }

  return true;
}

std::string CreateContainerIsolationDebugString(
    content::WebContents* web_contents,
    content::StoragePartition* storage_partition,
    const std::string& operation,
    const GURL& url) {
  std::string debug_info;

  debug_info += "operation=" + operation;

  if (web_contents) {
    debug_info += ", web_contents=";
    debug_info += base::StringPrintf("%p", web_contents);
  } else {
    debug_info += ", web_contents=null";
  }

  if (storage_partition) {
    debug_info += ", storage_partition=";
    debug_info += base::StringPrintf("%p", storage_partition);

    ContainerPartitionTracker* tracker =
        ContainerPartitionTracker::GetInstance();
    std::string partition_id = tracker->GetPartitionId(storage_partition);
    if (!partition_id.empty()) {
      debug_info += ", partition_id=" + partition_id;
    }
  } else {
    debug_info += ", storage_partition=null";
  }

  if (url.is_valid()) {
    debug_info += ", url=" + url.spec();
  }

  if (web_contents) {
    std::string expected_partition_id =
        GetExpectedPartitionIdForWebContents(web_contents);
    if (!expected_partition_id.empty()) {
      debug_info += ", expected_partition_id=" + expected_partition_id;
    }
  }

  return debug_info;
}

std::string GetContainerIsolationTraceEventName(const std::string& operation) {
  return "ContainerIsolation::" + operation;
}

}  
