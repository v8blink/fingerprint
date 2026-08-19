
#include "chrome/browser/container/container_isolation_validator.h"

#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/container/container_partition_tracker.h"
#include "chrome/browser/container/service_worker_container_isolation.h"
#include "chrome/browser/container/tab_container_manager_factory.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"

namespace tab_container {

namespace {
constexpr const char* kLogPrefix = "[ContainerIsolationValidator]";
}  

ContainerIsolationValidator::ContainerIsolationValidator() = default;

ContainerIsolationValidator::~ContainerIsolationValidator() = default;

ContainerIsolationValidator::ValidationResult
ContainerIsolationValidator::ValidateOperation(
    const ValidationContext& context) {
  if (!context.web_contents) {
    ;
    return ValidationResult::NO_CONTAINER;
  }

  if (!IsWebContentsValid(context.web_contents)) {
    ;
    return ValidationResult::TAB_DESTROYED;
  }

  return ValidatePartitionMatch(context.web_contents,
                                context.storage_partition);
}

ContainerIsolationValidator::ValidationResult
ContainerIsolationValidator::ValidatePartitionMatch(
    content::WebContents* web_contents,
    content::StoragePartition* storage_partition) {
  if (!web_contents) {
    return ValidationResult::NO_CONTAINER;
  }

  std::string expected_partition_id = GetExpectedPartitionId(web_contents);
  if (expected_partition_id.empty()) {

    return ValidationResult::NO_CONTAINER;
  }

  if (!IsPartitionInUse(expected_partition_id)) {
    ;
    return ValidationResult::PARTITION_DESTROYED;
  }

  if (!storage_partition) {

    return ValidationResult::NO_CONTAINER;
  }

  std::string actual_partition_id = GetActualPartitionId(storage_partition);
  if (actual_partition_id.empty()) {

    return ValidationResult::NO_CONTAINER;
  }

  if (expected_partition_id != actual_partition_id) {
    ;
    return ValidationResult::INVALID_PARTITION;
  }

  return ValidationResult::VALID;
}

bool ContainerIsolationValidator::IsWebContentsValid(
    content::WebContents* web_contents) const {
  if (!web_contents) {
    return false;
  }

  if (web_contents->IsBeingDestroyed()) {
    return false;
  }

  content::BrowserContext* browser_context =
      web_contents->GetBrowserContext();
  if (!browser_context) {
    return false;
  }

  return true;
}

bool ContainerIsolationValidator::IsPartitionInUse(
    const std::string& partition_id) const {
  if (partition_id.empty()) {
    return false;
  }

  return true;
}

std::string ContainerIsolationValidator::GetErrorMessage(
    ValidationResult result,
    const ValidationContext& context) const {
  switch (result) {
    case ValidationResult::VALID:
      return "Validation passed";
    case ValidationResult::INVALID_PARTITION:
      return base::StringPrintf(
          "Partition mismatch for operation '%s' on URL '%s'",
          context.operation_type.c_str(), context.url.spec().c_str());
    case ValidationResult::NO_CONTAINER:
      return "No container found (backward compatibility mode)";
    case ValidationResult::TAB_DESTROYED:
      return "Tab is being destroyed";
    case ValidationResult::PARTITION_DESTROYED:
      return "Partition has been destroyed";
  }
  return "Unknown validation result";
}

std::string ContainerIsolationValidator::GetExpectedPartitionId(
    content::WebContents* web_contents) const {
  return tab_container::GetExpectedPartitionIdForWebContents(web_contents);
}

std::string ContainerIsolationValidator::GetActualPartitionId(
    content::StoragePartition* storage_partition) const {
  return tab_container::GetPartitionIdFromStoragePartition(storage_partition);
}

}  
