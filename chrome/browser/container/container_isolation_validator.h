
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_ISOLATION_VALIDATOR_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_ISOLATION_VALIDATOR_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
}  

namespace tab_container {

class ContainerIsolationValidator {
 public:
  ContainerIsolationValidator();
  ~ContainerIsolationValidator();

  enum class ValidationResult {
    VALID,              
    INVALID_PARTITION, 
    NO_CONTAINER,       
    TAB_DESTROYED,      
    PARTITION_DESTROYED 
  };

  struct ValidationContext {
    raw_ptr<content::WebContents> web_contents = nullptr;
    raw_ptr<content::StoragePartition> storage_partition = nullptr;
    GURL url;
    blink::StorageKey storage_key;
    std::string operation_type;  
  };

  ValidationResult ValidateOperation(const ValidationContext& context);

  ValidationResult ValidatePartitionMatch(
      content::WebContents* web_contents,
      content::StoragePartition* storage_partition);

  bool IsWebContentsValid(content::WebContents* web_contents) const;

  bool IsPartitionInUse(const std::string& partition_id) const;

  std::string GetErrorMessage(ValidationResult result,
                              const ValidationContext& context) const;

 private:

  std::string GetExpectedPartitionId(content::WebContents* web_contents) const;

  std::string GetActualPartitionId(
      content::StoragePartition* storage_partition) const;
};

}  

#endif  
