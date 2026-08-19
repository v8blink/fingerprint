
#include "chrome/browser/container/container_isolation_errors.h"

#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"

namespace tab_container {

std::string GetContainerIsolationErrorMessage(ContainerIsolationError error) {
  switch (error) {
    case ContainerIsolationError::NONE:
      return "No error";
    case ContainerIsolationError::PARTITION_MISMATCH:
      return "Container partition mismatch detected";
    case ContainerIsolationError::TAB_DESTROYED:
      return "Tab is being destroyed";
    case ContainerIsolationError::PARTITION_DESTROYED:
      return "Container partition has been destroyed";
    case ContainerIsolationError::NO_CONTAINER:
      return "No container found";
    case ContainerIsolationError::INVALID_WEB_CONTENTS:
      return "Invalid WebContents";
    case ContainerIsolationError::INVALID_STORAGE_PARTITION:
      return "Invalid StoragePartition";
    case ContainerIsolationError::RAPID_TAB_LIFECYCLE_VIOLATION:
      return "Rapid tab lifecycle violation detected";
    case ContainerIsolationError::NAVIGATION_PARTITION_MISMATCH:
      return "Navigation partition mismatch";
  }
  return "Unknown error";
}

std::string GetContainerIsolationErrorMessageWithContext(
    ContainerIsolationError error,
    const std::string& partition_id,
    const std::string& operation_type,
    const std::string& url) {
  std::string base_message = GetContainerIsolationErrorMessage(error);

  if (error == ContainerIsolationError::NONE) {
    return base_message;
  }

  std::string context;
  if (!partition_id.empty()) {
    context += "partition_id=" + partition_id;
  }
  if (!operation_type.empty()) {
    if (!context.empty()) {
      context += ", ";
    }
    context += "operation=" + operation_type;
  }
  if (!url.empty()) {
    if (!context.empty()) {
      context += ", ";
    }
    context += "url=" + url;
  }

  if (!context.empty()) {
    return base::StringPrintf("%s (%s)", base_message.c_str(),
                              context.c_str());
  }

  return base_message;
}

}  
