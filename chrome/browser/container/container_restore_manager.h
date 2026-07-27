
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_RESTORE_MANAGER_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_RESTORE_MANAGER_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/unguessable_token.h"

namespace content {
class BrowserContext;
class StoragePartition;
class WebContents;
}  

namespace tab_container {

enum class ContainerRestoreState {

  kUninitialized = 0,

  kRestoreStarted = 1,

  kValidating = 2,

  kRestoringPartition = 3,

  kConfiguringNetwork = 4,

  kApplyingProxy = 5,

  kRestoreComplete = 6,

  kRestoreFailed = 7,

  kOrphaned = 8,

  kCleaningUp = 9,

  kDestroyed = 10,
};

struct ContainerRestoreResult {
  bool success = false;
  ContainerRestoreState final_state = ContainerRestoreState::kUninitialized;
  std::string partition_id;
  std::string container_id;
  std::string error_message;
  base::TimeTicks restore_started_at;
  base::TimeTicks restore_completed_at;
  base::TimeDelta total_restore_duration;

  struct PhaseTimings {
    base::TimeDelta validation_duration;
    base::TimeDelta partition_creation_duration;
    base::TimeDelta network_config_duration;
    base::TimeDelta proxy_setup_duration;
  };
  PhaseTimings phase_timings;

  bool validation_complete = false;
  bool partition_created = false;
  bool network_configured = false;
  bool proxy_applied = false;

  std::vector<std::string> diagnostic_messages;

  ContainerRestoreResult() = default;
  ContainerRestoreResult(const ContainerRestoreResult&) = default;
  ContainerRestoreResult& operator=(const ContainerRestoreResult&) = default;
  ~ContainerRestoreResult() = default;
};

struct ContainerPersistentState {
  std::string container_id;
  std::string partition_id;
  base::Time created_time;
  base::Time last_active_time;

  struct NetworkConfig {
    std::string proxy_server;
    std::string proxy_bypass_rules;
    bool proxy_enabled = false;
    std::string user_agent_override;
  };
  NetworkConfig network_config;

  struct PartitionConfig {
    bool is_off_the_record = false;
    std::string partition_domain;
    std::string partition_name;
  };
  PartitionConfig partition_config;

  std::string Serialize() const;
  static std::optional<ContainerPersistentState> Deserialize(
      const std::string& data);

  ContainerPersistentState() = default;
  ContainerPersistentState(const ContainerPersistentState&) = default;
  ContainerPersistentState& operator=(const ContainerPersistentState&) = default;
  ~ContainerPersistentState() = default;
};

class ContainerRestoreObserver : public base::CheckedObserver {
 public:
  ~ContainerRestoreObserver() override = default;

  virtual void OnContainerRestoreStarted(const std::string& container_id,
                                         const std::string& partition_id) {}

  virtual void OnContainerRestoreStateChanged(
      const std::string& container_id,
      ContainerRestoreState old_state,
      ContainerRestoreState new_state) {}

  virtual void OnContainerRestoreComplete(
      const ContainerRestoreResult& result) {}

  virtual void OnOrphanedPartitionDetected(const std::string& partition_id,
                                           const std::string& reason) {}

  virtual void OnOrphanedPartitionCleanupStarted(
      const std::string& partition_id) {}

  virtual void OnOrphanedPartitionCleanupComplete(
      const std::string& partition_id,
      bool success) {}

  virtual void OnCrashRecoveryStarted(size_t num_containers_to_restore) {}

  virtual void OnCrashRecoveryComplete(size_t restored_count,
                                       size_t failed_count,
                                       size_t orphaned_count) {}
};

class ContainerRestoreManager {
 public:
  explicit ContainerRestoreManager(content::BrowserContext* browser_context);
  ~ContainerRestoreManager();

  ContainerRestoreManager(const ContainerRestoreManager&) = delete;
  ContainerRestoreManager& operator=(const ContainerRestoreManager&) = delete;

  void AddObserver(ContainerRestoreObserver* observer);
  void RemoveObserver(ContainerRestoreObserver* observer);

  using RestoreCallback = base::OnceCallback<void(ContainerRestoreResult)>;
  void RestoreContainer(const ContainerPersistentState& state,
                        content::WebContents* web_contents,
                        RestoreCallback callback);

  using BatchRestoreCallback = base::OnceCallback<void(
      std::vector<ContainerRestoreResult>)>;
  void RestoreContainers(
      const std::vector<ContainerPersistentState>& states,
      const std::vector<content::WebContents*>& web_contents_list,
      BatchRestoreCallback callback);

  bool CancelRestore(const std::string& container_id);

  void CancelAllRestores();

  ContainerRestoreState GetRestoreState(
      const std::string& container_id) const;

  bool IsRestoreInProgress(const std::string& container_id) const;

  std::vector<std::string> GetContainersInRestore() const;

  void InitiateCrashRecovery();

  bool IsCrashRecoveryInProgress() const;

  struct CrashRecoveryStats {
    size_t total_persisted_containers = 0;
    size_t containers_restored = 0;
    size_t containers_failed = 0;
    size_t orphaned_partitions_found = 0;
    size_t orphaned_partitions_cleaned = 0;
    base::TimeTicks recovery_started_at;
    base::TimeTicks recovery_completed_at;
    bool recovery_complete = false;
  };
  CrashRecoveryStats GetCrashRecoveryStats() const;

  std::vector<std::string> ScanForOrphanedPartitions();

  void MarkPartitionAsOrphaned(const std::string& partition_id,
                               const std::string& reason);

  void CleanupOrphanedPartition(const std::string& partition_id);

  void CleanupAllOrphanedPartitions();

  bool IsPartitionOrphaned(const std::string& partition_id) const;

  std::set<std::string> GetOrphanedPartitionIds() const;

  bool SaveContainerState(const std::string& container_id,
                          const ContainerPersistentState& state);

  std::optional<ContainerPersistentState> LoadContainerState(
      const std::string& container_id) const;

  bool DeleteContainerState(const std::string& container_id);

  std::vector<std::string> GetPersistedContainerIds() const;

  bool ValidatePartitionIdSafe(const std::string& partition_id) const;

  bool ValidateContainerState(const std::string& container_id) const;

  struct IntegrityCheckResult {
    bool passed = false;
    std::vector<std::string> orphaned_partitions;
    std::vector<std::string> inconsistent_containers;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
  };
  IntegrityCheckResult PerformIntegrityCheck() const;

  void SetDebugLoggingEnabled(bool enabled);

  std::string GetDiagnosticReport() const;

  void DumpStateToLog() const;

 private:

  struct RestoreOperation {
    std::string container_id;
    std::string partition_id;
    ContainerRestoreState state;
    ContainerPersistentState persistent_state;
    raw_ptr<content::WebContents> web_contents;
    RestoreCallback callback;
    base::TimeTicks started_at;
    std::vector<std::string> diagnostic_log;
    bool cancelled = false;

    ContainerRestoreResult BuildResult() const;
  };

  void StartRestoreOperation(std::unique_ptr<RestoreOperation> operation);
  void DoValidationPhase(RestoreOperation* operation);
  void DoPartitionCreationPhase(RestoreOperation* operation);
  void DoNetworkConfigurationPhase(RestoreOperation* operation);
  void DoProxySetupPhase(RestoreOperation* operation);
  void CompleteRestoreOperation(RestoreOperation* operation, bool success,
                                const std::string& error_message = "");

  void TransitionRestoreState(RestoreOperation* operation,
                              ContainerRestoreState new_state);

  void DoCrashRecoveryPhase1_ScanPartitions();
  void DoCrashRecoveryPhase2_LoadPersistedState();
  void DoCrashRecoveryPhase3_RestoreContainers();
  void DoCrashRecoveryPhase4_CleanupOrphans();
  void CompleteCrashRecovery();

  void NotifyRestoreStarted(const std::string& container_id,
                            const std::string& partition_id);
  void NotifyRestoreStateChanged(const std::string& container_id,
                                 ContainerRestoreState old_state,
                                 ContainerRestoreState new_state);
  void NotifyRestoreComplete(const ContainerRestoreResult& result);
  void NotifyOrphanedPartitionDetected(const std::string& partition_id,
                                       const std::string& reason);

  void LogRestorePhase(const RestoreOperation* operation,
                       const std::string& phase,
                       const std::string& message);
  void LogRestoreError(const RestoreOperation* operation,
                       const std::string& error);

  raw_ptr<content::BrowserContext> browser_context_;

  std::map<std::string, std::unique_ptr<RestoreOperation>> active_restores_;

  std::map<std::string, std::string> orphaned_partitions_;

  std::map<std::string, ContainerPersistentState> persisted_states_;

  bool crash_recovery_in_progress_ = false;
  CrashRecoveryStats crash_recovery_stats_;
  std::vector<ContainerRestoreResult> crash_recovery_results_;

  struct BatchRestoreContext {
    std::vector<std::string> container_ids;
    std::vector<ContainerRestoreResult> results;
    BatchRestoreCallback callback;
    size_t completed_count = 0;
  };
  std::unique_ptr<BatchRestoreContext> batch_restore_context_;

  bool debug_logging_enabled_ = false;

  base::ObserverList<ContainerRestoreObserver> observers_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ContainerRestoreManager> weak_factory_{this};
};

}  

#endif  
