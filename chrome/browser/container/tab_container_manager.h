
#ifndef CHROME_BROWSER_CONTAINER_TAB_CONTAINER_MANAGER_H_
#define CHROME_BROWSER_CONTAINER_TAB_CONTAINER_MANAGER_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "content/public/browser/web_contents.h"

namespace content {
class BrowserContext;
class StoragePartition;
class WebContents;
}  

namespace tab_container {
class ContainerNetworkContextManager;
class ContainerProxyManager;
class ContainerDnsManager;
class ContainerRestoreManager;
class ContainerFingerprintManager;
class ContainerSessionManager;
class ContainerCookieManager;
class ContainerSecurityManager;
class ContainerUserAgentManager;
class ContainerMetricsManager;
}  

enum class ContainerLifecycleState {

  kNotCreated = 0,

  kCreating = 1,

  kActive = 2,

  kMarkedForDestruction = 3,

  kDestroying = 4,

  kDestroyed = 5,

  kFailed = 6,

  kRestoring = 7,
};

struct ContainerCreationOptions {

  bool create_network_context = true;

  bool restore_proxy_config = true;

  bool restore_dns_config = true;

  std::string custom_partition_id;

  bool enable_debug_tracing = false;

  std::string profile_name;
  std::string profile_cookies;
  std::string profile_proxy;
  std::string profile_fingerprint_id;
  std::string profile_encryption_key;

  bool write_initial_encrypted_profile = false;

  ContainerCreationOptions();
  ContainerCreationOptions(const ContainerCreationOptions&);
  ContainerCreationOptions(ContainerCreationOptions&&);
  ContainerCreationOptions& operator=(const ContainerCreationOptions&);
  ContainerCreationOptions& operator=(ContainerCreationOptions&&);
  ~ContainerCreationOptions();
};

class TabContainerObserver : public base::CheckedObserver {
 public:
  ~TabContainerObserver() override = default;

  virtual void OnContainerCreated(content::WebContents* web_contents,
                                  const std::string& container_id,
                                  const std::string& partition_id) {}

  virtual void OnContainerStateChanged(const std::string& container_id,
                                       ContainerLifecycleState old_state,
                                       ContainerLifecycleState new_state) {}

  virtual void OnContainerWillBeDestroyed(const std::string& container_id) {}

  virtual void OnContainerDestroyed(const std::string& container_id,
                                    const std::string& partition_id) {}

  virtual void OnContainerError(const std::string& container_id,
                                const std::string& error_message) {}

  virtual void OnRapidContainerTurnover(size_t containers_created,
                                        size_t containers_destroyed,
                                        base::TimeDelta window) {}
};

class TabContainerManager {
 public:
  explicit TabContainerManager(content::BrowserContext* browser_context);
  ~TabContainerManager();

  TabContainerManager(const TabContainerManager&) = delete;
  TabContainerManager& operator=(const TabContainerManager&) = delete;

  void AddObserver(TabContainerObserver* observer);
  void RemoveObserver(TabContainerObserver* observer);

  std::string CreateContainerForTab(content::WebContents* web_contents);

  std::string CreateContainerForTab(content::WebContents* web_contents,
                                    const ContainerCreationOptions& options);

  std::string GetPartitionIdForTab(content::WebContents* web_contents) const;

  void MarkContainerForDestruction(content::WebContents* web_contents);

  bool DestroyContainerForTab(content::WebContents* web_contents);

  bool IsPartitionIdInUse(const std::string& partition_id) const;

  std::string GetContainerIdForTab(content::WebContents* web_contents) const;

  size_t GetActiveContainerCount() const;

  ContainerLifecycleState GetContainerState(
      content::WebContents* web_contents) const;
  ContainerLifecycleState GetContainerStateById(
      const std::string& container_id) const;

  std::vector<std::string> GetAllContainerIds() const;

  content::WebContents* GetWebContentsForContainer(
      const std::string& container_id) const;

  struct ValidationResult {
    ValidationResult();
    ValidationResult(const ValidationResult&);
    ValidationResult(ValidationResult&&);
    ValidationResult& operator=(const ValidationResult&);
    ValidationResult& operator=(ValidationResult&&);
    ~ValidationResult();

    bool passed = false;
    std::vector<std::string> orphaned_partitions;
    std::vector<std::string> inconsistent_containers;
    std::vector<std::string> warnings;
  };
  ValidationResult ValidateAllContainers() const;

  size_t CleanupOrphanedPartitions();

  bool IsPartitionIdDestroyed(const std::string& partition_id) const;

  size_t GetDestroyedPartitionIdCount() const;

  content::StoragePartition* GetStoragePartitionForContainer(
      const std::string& container_id);

  content::StoragePartition* GetStoragePartitionForWebContents(
      content::WebContents* web_contents);

  void RegisterStoragePartition(const std::string& container_id,
                                content::StoragePartition* partition);

  void OnRapidTabCreation(content::WebContents* web_contents);

  void OnRapidTabClosure(content::WebContents* web_contents);

  struct TurnoverStats {
    size_t containers_created_last_minute = 0;
    size_t containers_destroyed_last_minute = 0;
    base::TimeTicks last_creation;
    base::TimeTicks last_destruction;
  };
  TurnoverStats GetTurnoverStats() const;

  tab_container::ContainerNetworkContextManager* GetNetworkContextManager();

  tab_container::ContainerProxyManager* GetProxyManager();

  tab_container::ContainerDnsManager* GetDnsManager();

  tab_container::ContainerRestoreManager* GetRestoreManager();

  tab_container::ContainerFingerprintManager* GetFingerprintManager();

  tab_container::ContainerSessionManager* GetSessionManager();

  tab_container::ContainerCookieManager* GetCookieManager();

  tab_container::ContainerSecurityManager* GetSecurityManager();

  tab_container::ContainerUserAgentManager* GetUserAgentManager();

  tab_container::ContainerMetricsManager* GetMetricsManager();

  void SetDebugLoggingEnabled(bool enabled);

  std::string GetDiagnosticReport() const;

  void DumpStateToLog() const;

 private:
  struct ContainerInfo {
    std::string container_id;
    std::string partition_id;
    ContainerLifecycleState state = ContainerLifecycleState::kNotCreated;
    bool marked_for_destruction = false;
    base::TimeTicks created_at;
    base::TimeTicks state_changed_at;
    raw_ptr<content::StoragePartition> storage_partition = nullptr;

    ContainerCreationOptions creation_options;

    std::string last_error;

    ContainerInfo(const std::string& cid, const std::string& pid);
    ~ContainerInfo();
  };

  std::string GenerateContainerId();

  std::string GeneratePartitionId();

  bool ValidatePartitionIdNotReused(const std::string& partition_id) const;

  void TransitionContainerState(ContainerInfo* info,
                                ContainerLifecycleState new_state);

  void NotifyContainerCreated(content::WebContents* web_contents,
                              const std::string& container_id,
                              const std::string& partition_id);
  void NotifyContainerStateChanged(const std::string& container_id,
                                   ContainerLifecycleState old_state,
                                   ContainerLifecycleState new_state);
  void NotifyContainerWillBeDestroyed(const std::string& container_id);
  void NotifyContainerDestroyed(const std::string& container_id,
                                const std::string& partition_id);
  void NotifyContainerError(const std::string& container_id,
                            const std::string& error);

  void InitializeSubManagers();

  void CleanupSubManagerResources(const std::string& container_id);

  raw_ptr<content::BrowserContext> browser_context_;

  std::map<const content::WebContents*, std::unique_ptr<ContainerInfo>>
      tab_to_container_;

  std::map<std::string, const content::WebContents*> container_to_tab_;

  std::map<std::string, const content::WebContents*> partition_to_tab_;

  std::set<std::string> destroyed_partition_ids_;

  int container_id_counter_ = 0;

  std::vector<base::TimeTicks> recent_creations_;
  std::vector<base::TimeTicks> recent_destructions_;

  std::unique_ptr<tab_container::ContainerNetworkContextManager>
      network_context_manager_;
  std::unique_ptr<tab_container::ContainerProxyManager> proxy_manager_;
  std::unique_ptr<tab_container::ContainerDnsManager> dns_manager_;
  std::unique_ptr<tab_container::ContainerRestoreManager> restore_manager_;
  std::unique_ptr<tab_container::ContainerFingerprintManager> fingerprint_manager_;
  std::unique_ptr<tab_container::ContainerSessionManager> session_manager_;
  std::unique_ptr<tab_container::ContainerCookieManager> cookie_manager_;
  std::unique_ptr<tab_container::ContainerSecurityManager> security_manager_;
  std::unique_ptr<tab_container::ContainerUserAgentManager> useragent_manager_;
  std::unique_ptr<tab_container::ContainerMetricsManager> metrics_manager_;

  bool debug_logging_enabled_ = false;

  base::ObserverList<TabContainerObserver> observers_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<TabContainerManager> weak_factory_{this};
};

#endif  
