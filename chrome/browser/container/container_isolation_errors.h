
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_ISOLATION_ERRORS_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_ISOLATION_ERRORS_H_

#include <string>

namespace tab_container {

enum class ContainerIsolationError {
  NONE = 0,
  PARTITION_MISMATCH,
  TAB_DESTROYED,
  PARTITION_DESTROYED,
  NO_CONTAINER,
  INVALID_WEB_CONTENTS,
  INVALID_STORAGE_PARTITION,
  RAPID_TAB_LIFECYCLE_VIOLATION,
  NAVIGATION_PARTITION_MISMATCH,
};

std::string GetContainerIsolationErrorMessage(ContainerIsolationError error);

std::string GetContainerIsolationErrorMessageWithContext(
    ContainerIsolationError error,
    const std::string& partition_id,
    const std::string& operation_type,
    const std::string& url);

}  

#endif  
