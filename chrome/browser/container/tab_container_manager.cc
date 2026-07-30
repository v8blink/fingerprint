
#include "chrome/browser/container/tab_container_manager.h"

#include <algorithm>
#include <sstream>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "chrome/browser/container/container_cookie_manager.h"
#include "chrome/browser/container/container_dns_manager.h"
#include "chrome/browser/container/container_fingerprint_manager.h"
#include "chrome/browser/container/container_partition_tracker.h"
#include "chrome/browser/container/container_metrics_manager.h"
#include "chrome/browser/container/container_network_context_manager.h"
#include "chrome/browser/container/container_network_debug_tracer.h"
#include "chrome/browser/container/container_proxy_manager.h"
#include "chrome/browser/container/container_restore_manager.h"
#include "chrome/browser/container/container_security_manager.h"
#include "chrome/browser/container/container_session_manager.h"
#include "chrome/browser/container/container_useragent_manager.h"
#include "chrome/browser/container/createProfiles.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"

namespace {

constexpr size_t kMaxContainersPerMinute = 100;
constexpr base::TimeDelta kTurnoverWindow = base::Minutes(1);

std::string LifecycleStateToString(ContainerLifecycleState state) {
  switch (state) {
    case ContainerLifecycleState::kNotCreated:
      return "NotCreated";
    case ContainerLifecycleState::kCreating:
      return "Creating";
    case ContainerLifecycleState::kActive:
      return "Active";
    case ContainerLifecycleState::kMarkedForDestruction:
      return "MarkedForDestruction";
    case ContainerLifecycleState::kDestroying:
      return "Destroying";
    case ContainerLifecycleState::kDestroyed:
      return "Destroyed";
    case ContainerLifecycleState::kFailed:
      return "Failed";
    case ContainerLifecycleState::kRestoring:
      return "Restoring";
  }
  return "Unknown";
}

void LogContainerCreation(const content::WebContents* web_contents,
                          const std::string& container_id,
                          const std::string& partition_id) {
  ;
}

void LogContainerDestruction(const content::WebContents* web_contents,
                             const std::string& container_id,
                             const std::string& partition_id) {
  ;
}

void LogPartitionResolution(const content::WebContents* web_contents,
                            const std::string& partition_id) {
  ;
}

void LogContainerStateTransition(const content::WebContents* web_contents,
                                 const std::string& container_id,
                                 const std::string& old_state,
                                 const std::string& new_state) {
  ;
}

}  

ContainerCreationOptions::ContainerCreationOptions() = default;
ContainerCreationOptions::ContainerCreationOptions(
    const ContainerCreationOptions&) = default;
ContainerCreationOptions::ContainerCreationOptions(
    ContainerCreationOptions&&) = default;
ContainerCreationOptions& ContainerCreationOptions::operator=(
    const ContainerCreationOptions&) = default;
ContainerCreationOptions& ContainerCreationOptions::operator=(
    ContainerCreationOptions&&) = default;
ContainerCreationOptions::~ContainerCreationOptions() = default;

TabContainerManager::ValidationResult::ValidationResult() = default;
TabContainerManager::ValidationResult::ValidationResult(
    const ValidationResult&) = default;
TabContainerManager::ValidationResult::ValidationResult(ValidationResult&&) =
    default;
TabContainerManager::ValidationResult&
TabContainerManager::ValidationResult::operator=(const ValidationResult&) =
    default;
TabContainerManager::ValidationResult&
TabContainerManager::ValidationResult::operator=(ValidationResult&&) = default;
TabContainerManager::ValidationResult::~ValidationResult() = default;

TabContainerManager::ContainerInfo::ContainerInfo(const std::string& cid,
                                                  const std::string& pid)
    : container_id(cid), partition_id(pid) {}
TabContainerManager::ContainerInfo::~ContainerInfo() = default;

TabContainerManager::TabContainerManager(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ;

  InitializeSubManagers();
}

TabContainerManager::~TabContainerManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  for (const auto& pair : tab_to_container_) {
    if (pair.second) {
      CleanupSubManagerResources(pair.second->container_id);
    }
  }

  ;
}

void TabContainerManager::AddObserver(TabContainerObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void TabContainerManager::RemoveObserver(TabContainerObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

std::string TabContainerManager::CreateContainerForTab(
    content::WebContents* web_contents) {
  return CreateContainerForTab(web_contents, ContainerCreationOptions());
}

std::string TabContainerManager::CreateContainerForTab(
    content::WebContents* web_contents,
    const ContainerCreationOptions& options) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(web_contents);

  ;

  base::TimeTicks now = base::TimeTicks::Now();
  recent_creations_.push_back(now);

  recent_creations_.erase(
      std::remove_if(recent_creations_.begin(), recent_creations_.end(),
                     [now](base::TimeTicks t) { 
                       return (now - t) > kTurnoverWindow; 
                     }),
      recent_creations_.end());

  if (recent_creations_.size() > kMaxContainersPerMinute) {
    ;
    for (auto& observer : observers_) {
      observer.OnRapidContainerTurnover(
          recent_creations_.size(), recent_destructions_.size(), kTurnoverWindow);
    }
  }

  if (tab_to_container_.find(web_contents) != tab_to_container_.end()) {
    auto* existing_info = tab_to_container_[web_contents].get();
    ;
    if (existing_info) {
      ;
      return existing_info->partition_id;
    }
    return std::string();
  }

  std::string container_id = GenerateContainerId();
  std::string partition_id = options.custom_partition_id.empty()
                             ? GeneratePartitionId()
                             : options.custom_partition_id;

  ;

  if (!ValidatePartitionIdNotReused(partition_id)) {
    ;

    partition_id = GeneratePartitionId();
    if (!ValidatePartitionIdNotReused(partition_id)) {
      ;
      NotifyContainerError(container_id, "Failed to generate unique partition ID");
      return std::string();
    }
  }

  auto container_info =
      std::make_unique<ContainerInfo>(container_id, partition_id);
  container_info->created_at = base::TimeTicks::Now();
  container_info->state_changed_at = container_info->created_at;
  container_info->creation_options = options;

  TransitionContainerState(container_info.get(), 
                           ContainerLifecycleState::kCreating);

  ContainerInfo* info_ptr = container_info.get();
  tab_to_container_[web_contents] = std::move(container_info);
  container_to_tab_[container_id] = web_contents;
  partition_to_tab_[partition_id] = web_contents;

  ;

  LogContainerCreation(web_contents, container_id, partition_id);

  if (options.create_network_context && network_context_manager_) {
    content::StoragePartition* partition = 
        GetStoragePartitionForWebContents(web_contents);
    if (partition) {
      network_context_manager_->CreateNetworkContextForContainer(
          container_id, partition_id, partition);
      info_ptr->storage_partition = partition;
    }
  }

  if (options.enable_debug_tracing) {
    tab_container::ContainerNetworkDebugTracer::GetInstance()->
        TraceContainerCreated(container_id, partition_id);
  }

  if (!options.write_initial_encrypted_profile) {
    ;
  } else if (options.profile_encryption_key.empty()) {
    ;
  } else if (browser_context_) {
    std::string profile_write_error;
    const std::string profile_name =
        options.profile_name.empty() ? container_id : options.profile_name;
    const std::string fingerprint_id =
        options.profile_fingerprint_id.empty() ? container_id
                                               : options.profile_fingerprint_id;

    const bool profile_written = tab_container::CreateEncryptedProfileJson(
        tab_container::GetTanyaProfilesBasePath(browser_context_->GetPath()),
        profile_name, container_id, options.profile_cookies, options.profile_proxy,
        fingerprint_id, options.profile_encryption_key, &profile_write_error);

    if (!profile_written) {
      ;
    } else {
      ;
    }
  } else {
    ;
  }

  TransitionContainerState(info_ptr, ContainerLifecycleState::kActive);

  NotifyContainerCreated(web_contents, container_id, partition_id);

  ;

  return partition_id;
}

std::string TabContainerManager::GetPartitionIdForTab(
    content::WebContents* web_contents) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(web_contents);

  auto it = tab_to_container_.find(web_contents);
  if (it == tab_to_container_.end()) {
    ;
    return std::string();
  }

  const std::string& partition_id = it->second->partition_id;
  LogPartitionResolution(web_contents, partition_id);
  return partition_id;
}

void TabContainerManager::MarkContainerForDestruction(
    content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(web_contents);

  auto it = tab_to_container_.find(web_contents);
  if (it == tab_to_container_.end()) {
    ;
    return;
  }

  ContainerInfo* container_info = it->second.get();
  if (container_info->marked_for_destruction) {
    ;
    return;
  }

  container_info->marked_for_destruction = true;
  TransitionContainerState(container_info,
                           ContainerLifecycleState::kMarkedForDestruction);

  NotifyContainerWillBeDestroyed(container_info->container_id);

  ;
}

bool TabContainerManager::DestroyContainerForTab(
    content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(web_contents);

  base::TimeTicks now = base::TimeTicks::Now();
  recent_destructions_.push_back(now);

  recent_destructions_.erase(
      std::remove_if(recent_destructions_.begin(), recent_destructions_.end(),
                     [now](base::TimeTicks t) { 
                       return (now - t) > kTurnoverWindow; 
                     }),
      recent_destructions_.end());

  auto it = tab_to_container_.find(web_contents);
  if (it == tab_to_container_.end()) {
    ;
    return false;
  }

  ContainerInfo* container_info = it->second.get();
  const std::string container_id = container_info->container_id;
  const std::string partition_id = container_info->partition_id;

  TransitionContainerState(container_info, ContainerLifecycleState::kDestroying);

  if (destroyed_partition_ids_.find(partition_id) !=
      destroyed_partition_ids_.end()) {
    ;

  }

  CleanupSubManagerResources(container_id);

  ;

  destroyed_partition_ids_.insert(partition_id);

  tab_container::ContainerNetworkDebugTracer::GetInstance()->
      TraceContainerDestroying(container_id);

  container_to_tab_.erase(container_id);
  partition_to_tab_.erase(partition_id);
  tab_to_container_.erase(it);

  LogContainerDestruction(web_contents, container_id, partition_id);

  NotifyContainerDestroyed(container_id, partition_id);

  ;

  return true;
}

bool TabContainerManager::IsPartitionIdInUse(
    const std::string& partition_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return partition_to_tab_.find(partition_id) != partition_to_tab_.end();
}

std::string TabContainerManager::GetContainerIdForTab(
    content::WebContents* web_contents) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(web_contents);

  auto it = tab_to_container_.find(web_contents);
  if (it == tab_to_container_.end()) {
    return std::string();
  }

  return it->second->container_id;
}

size_t TabContainerManager::GetActiveContainerCount() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return tab_to_container_.size();
}

ContainerLifecycleState TabContainerManager::GetContainerState(
    content::WebContents* web_contents) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = tab_to_container_.find(web_contents);
  if (it == tab_to_container_.end()) {
    return ContainerLifecycleState::kNotCreated;
  }

  return it->second->state;
}

ContainerLifecycleState TabContainerManager::GetContainerStateById(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto tab_it = container_to_tab_.find(container_id);
  if (tab_it == container_to_tab_.end()) {
    return ContainerLifecycleState::kNotCreated;
  }

  auto it = tab_to_container_.find(tab_it->second);
  if (it == tab_to_container_.end()) {
    return ContainerLifecycleState::kNotCreated;
  }

  return it->second->state;
}

std::vector<std::string> TabContainerManager::GetAllContainerIds() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<std::string> result;
  for (const auto& pair : tab_to_container_) {
    result.push_back(pair.second->container_id);
  }
  return result;
}

content::WebContents* TabContainerManager::GetWebContentsForContainer(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_to_tab_.find(container_id);
  if (it == container_to_tab_.end()) {
    return nullptr;
  }

  return const_cast<content::WebContents*>(it->second);
}

TabContainerManager::ValidationResult 
TabContainerManager::ValidateAllContainers() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ValidationResult result;
  result.passed = true;

  ;

  for (const auto& partition_id : destroyed_partition_ids_) {
    if (partition_to_tab_.find(partition_id) != partition_to_tab_.end()) {
      result.orphaned_partitions.push_back(partition_id);
      result.warnings.push_back(
          "Partition " + partition_id + " is in both destroyed and active sets");
      result.passed = false;
    }
  }

  for (const auto& pair : tab_to_container_) {
    const ContainerInfo* info = pair.second.get();

    auto part_it = partition_to_tab_.find(info->partition_id);
    if (part_it == partition_to_tab_.end()) {
      result.inconsistent_containers.push_back(info->container_id);
      result.warnings.push_back(
          "Container " + info->container_id + " not in partition_to_tab map");
      result.passed = false;
    } else if (part_it->second != pair.first) {
      result.inconsistent_containers.push_back(info->container_id);
      result.warnings.push_back(
          "Container " + info->container_id + " has mismatched WebContents");
      result.passed = false;
    }

    auto cont_it = container_to_tab_.find(info->container_id);
    if (cont_it == container_to_tab_.end()) {
      result.inconsistent_containers.push_back(info->container_id);
      result.warnings.push_back(
          "Container " + info->container_id + " not in container_to_tab map");
      result.passed = false;
    }
  }

  ;

  return result;
}

size_t TabContainerManager::CleanupOrphanedPartitions() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  size_t cleaned = 0;

  std::vector<std::string> to_remove;
  for (const auto& partition_id : destroyed_partition_ids_) {
    if (partition_to_tab_.find(partition_id) != partition_to_tab_.end()) {
      to_remove.push_back(partition_id);
    }
  }

  for (const auto& partition_id : to_remove) {
    partition_to_tab_.erase(partition_id);
    cleaned++;
    ;
  }

  if (restore_manager_) {
    restore_manager_->CleanupAllOrphanedPartitions();
  }

  ;

  return cleaned;
}

bool TabContainerManager::IsPartitionIdDestroyed(
    const std::string& partition_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return destroyed_partition_ids_.find(partition_id) != 
         destroyed_partition_ids_.end();
}

size_t TabContainerManager::GetDestroyedPartitionIdCount() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return destroyed_partition_ids_.size();
}

content::StoragePartition* TabContainerManager::GetStoragePartitionForContainer(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto tab_it = container_to_tab_.find(container_id);
  if (tab_it == container_to_tab_.end()) {
    return nullptr;
  }

  auto it = tab_to_container_.find(tab_it->second);
  if (it == tab_to_container_.end()) {
    return nullptr;
  }

  return it->second->storage_partition;
}

content::StoragePartition* TabContainerManager::GetStoragePartitionForWebContents(
    content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!web_contents || !browser_context_) {
    return nullptr;
  }

  auto it = tab_to_container_.find(web_contents);
  if (it == tab_to_container_.end() || !it->second) {

    ;
    return browser_context_->GetDefaultStoragePartition();
  }

  ContainerInfo* container_info = it->second.get();
  if (container_info->partition_id.empty()) {
    ;
    return browser_context_->GetDefaultStoragePartition();
  }

  const std::string& container_id = container_info->container_id;
  const std::string initial_container_partition_id = container_info->partition_id;

  content::StoragePartition* site_instance_partition = nullptr;
  std::string site_partition_name;
  if (content::SiteInstance* site_instance = web_contents->GetSiteInstance()) {
    site_instance_partition =
        browser_context_->GetStoragePartition(site_instance);
    if (site_instance_partition) {
      site_partition_name =
          site_instance_partition->GetConfig().partition_name();
    }
  }

  if (site_instance_partition &&
      site_partition_name.starts_with("tab_partition_")) {
    if (site_partition_name != container_info->partition_id) {
      ;
      container_info->partition_id = site_partition_name;
    }
    if (container_info->storage_partition &&
        container_info->storage_partition != site_instance_partition) {
      ;
    }
    container_info->storage_partition = site_instance_partition;
    RegisterStoragePartition(container_id, site_instance_partition);
    tab_container::ContainerPartitionTracker::GetInstance()->RegisterPartition(
        site_instance_partition, container_info->partition_id, web_contents);
    return site_instance_partition;
  }

  const std::string tanya_recorded_partition =
      tab_util::GetTanyaInitialStoragePartitionIdIfRecorded(web_contents);
  if (!tanya_recorded_partition.empty() &&
      tanya_recorded_partition.starts_with("tab_partition_")) {
    if (tanya_recorded_partition != container_info->partition_id) {
      ;
      container_info->partition_id = tanya_recorded_partition;
    }
    content::StoragePartitionConfig tanya_config =
        content::StoragePartitionConfig::Create(
            browser_context_, "tab_container", tanya_recorded_partition,
            browser_context_->IsOffTheRecord());
    content::StoragePartition* tanya_partition =
        browser_context_->GetStoragePartition(tanya_config);
    if (tanya_partition) {
      if (container_info->storage_partition &&
          container_info->storage_partition != tanya_partition) {
        ;
      }
      container_info->storage_partition = tanya_partition;
      RegisterStoragePartition(container_id, tanya_partition);
      tab_container::ContainerPartitionTracker::GetInstance()->RegisterPartition(
          tanya_partition, container_info->partition_id, web_contents);
      return tanya_partition;
    }
  }

  if (!site_partition_name.empty() &&
      site_partition_name != container_info->partition_id) {
    ;
  }

  if (container_info->storage_partition) {
    return container_info->storage_partition;
  }

  const std::string& partition_id = container_info->partition_id;

  content::StoragePartitionConfig config =
      content::StoragePartitionConfig::Create(
          browser_context_, "tab_container", partition_id,
          browser_context_->IsOffTheRecord());

  content::StoragePartition* storage_partition =
      browser_context_->GetStoragePartition(config);
  if (!storage_partition) {
    return browser_context_->GetDefaultStoragePartition();
  }

  RegisterStoragePartition(container_id, storage_partition);

  tab_container::ContainerPartitionTracker::GetInstance()->RegisterPartition(
      storage_partition, partition_id, web_contents);

  return storage_partition;
}

void TabContainerManager::RegisterStoragePartition(
    const std::string& container_id,
    content::StoragePartition* partition) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto tab_it = container_to_tab_.find(container_id);
  if (tab_it == container_to_tab_.end()) {
    ;
    return;
  }

  auto it = tab_to_container_.find(tab_it->second);
  if (it != tab_to_container_.end()) {
    it->second->storage_partition = partition;
    ;
  }
}

void TabContainerManager::OnRapidTabCreation(content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  if (recent_creations_.size() > kMaxContainersPerMinute / 2) {
    ;
  }
}

void TabContainerManager::OnRapidTabClosure(content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  auto it = tab_to_container_.find(web_contents);
  if (it != tab_to_container_.end()) {

    if (!it->second->marked_for_destruction) {
      MarkContainerForDestruction(web_contents);
    }
  }
}

TabContainerManager::TurnoverStats TabContainerManager::GetTurnoverStats() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  TurnoverStats stats;
  stats.containers_created_last_minute = recent_creations_.size();
  stats.containers_destroyed_last_minute = recent_destructions_.size();

  if (!recent_creations_.empty()) {
    stats.last_creation = recent_creations_.back();
  }
  if (!recent_destructions_.empty()) {
    stats.last_destruction = recent_destructions_.back();
  }

  return stats;
}

tab_container::ContainerNetworkContextManager* 
TabContainerManager::GetNetworkContextManager() {
  return network_context_manager_.get();
}

tab_container::ContainerProxyManager* TabContainerManager::GetProxyManager() {
  return proxy_manager_.get();
}

tab_container::ContainerDnsManager* TabContainerManager::GetDnsManager() {
  return dns_manager_.get();
}

tab_container::ContainerRestoreManager* TabContainerManager::GetRestoreManager() {
  return restore_manager_.get();
}

void TabContainerManager::SetDebugLoggingEnabled(bool enabled) {
  debug_logging_enabled_ = enabled;

  if (network_context_manager_) {
    network_context_manager_->SetDebugLoggingEnabled(enabled);
  }
  if (proxy_manager_) {
    proxy_manager_->SetDebugLoggingEnabled(enabled);
  }

  ;
}

std::string TabContainerManager::GetDiagnosticReport() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::stringstream report;
  report << "=== TabContainerManager Diagnostic Report ===\n";
  report << "Browser Context: " << browser_context_ << "\n\n";

  report << "Container Statistics:\n";
  report << "  Active Containers: " << tab_to_container_.size() << "\n";
  report << "  Destroyed Partition IDs: " << destroyed_partition_ids_.size() << "\n";
  report << "  Container ID Counter: " << container_id_counter_ << "\n\n";

  TurnoverStats turnover = GetTurnoverStats();
  report << "Turnover (last minute):\n";
  report << "  Created: " << turnover.containers_created_last_minute << "\n";
  report << "  Destroyed: " << turnover.containers_destroyed_last_minute << "\n\n";

  report << "Active Containers:\n";
  for (const auto& pair : tab_to_container_) {
    const ContainerInfo* info = pair.second.get();
    report << "  - " << info->container_id << ":\n";
    report << "      Partition ID: " << info->partition_id << "\n";
    report << "      State: " << LifecycleStateToString(info->state) << "\n";
    report << "      Marked for Destruction: " 
           << (info->marked_for_destruction ? "Yes" : "No") << "\n";
    report << "      Has StoragePartition: " 
           << (info->storage_partition ? "Yes" : "No") << "\n";
  }

  report << "\nValidation:\n";
  ValidationResult validation = ValidateAllContainers();
  report << "  Passed: " << (validation.passed ? "Yes" : "No") << "\n";
  report << "  Orphaned Partitions: " << validation.orphaned_partitions.size() << "\n";
  report << "  Inconsistent Containers: " << validation.inconsistent_containers.size() << "\n";

  if (network_context_manager_) {
    report << "\n" << network_context_manager_->GetDiagnosticReport();
  }
  if (proxy_manager_) {
    report << "\n" << proxy_manager_->GetDiagnosticReport();
  }
  if (dns_manager_) {
    report << "\n" << dns_manager_->GetDiagnosticReport();
  }

  return report.str();
}

void TabContainerManager::DumpStateToLog() const {
  ;
}

std::string TabContainerManager::GenerateContainerId() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::stringstream ss;
  ss << "container_" << container_id_counter_++ << "_"
     << base::UnguessableToken::Create().ToString();
  return ss.str();
}

std::string TabContainerManager::GeneratePartitionId() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return "tab_partition_" + base::UnguessableToken::Create().ToString();
}

bool TabContainerManager::ValidatePartitionIdNotReused(
    const std::string& partition_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (destroyed_partition_ids_.find(partition_id) !=
      destroyed_partition_ids_.end()) {
    return false;
  }

  if (partition_to_tab_.find(partition_id) != partition_to_tab_.end()) {
    return false;
  }
  return true;
}

void TabContainerManager::TransitionContainerState(
    ContainerInfo* info,
    ContainerLifecycleState new_state) {
  ContainerLifecycleState old_state = info->state;
  info->state = new_state;
  info->state_changed_at = base::TimeTicks::Now();

  content::WebContents* web_contents = nullptr;
  auto it = container_to_tab_.find(info->container_id);
  if (it != container_to_tab_.end()) {
    web_contents = const_cast<content::WebContents*>(it->second);
  }

  LogContainerStateTransition(web_contents, info->container_id,
                              LifecycleStateToString(old_state),
                              LifecycleStateToString(new_state));

  NotifyContainerStateChanged(info->container_id, old_state, new_state);
}

void TabContainerManager::NotifyContainerCreated(
    content::WebContents* web_contents,
    const std::string& container_id,
    const std::string& partition_id) {
  for (auto& observer : observers_) {
    observer.OnContainerCreated(web_contents, container_id, partition_id);
  }
}

void TabContainerManager::NotifyContainerStateChanged(
    const std::string& container_id,
    ContainerLifecycleState old_state,
    ContainerLifecycleState new_state) {
  for (auto& observer : observers_) {
    observer.OnContainerStateChanged(container_id, old_state, new_state);
  }
}

void TabContainerManager::NotifyContainerWillBeDestroyed(
    const std::string& container_id) {
  for (auto& observer : observers_) {
    observer.OnContainerWillBeDestroyed(container_id);
  }
}

void TabContainerManager::NotifyContainerDestroyed(
    const std::string& container_id,
    const std::string& partition_id) {
  for (auto& observer : observers_) {
    observer.OnContainerDestroyed(container_id, partition_id);
  }
}

void TabContainerManager::NotifyContainerError(
    const std::string& container_id,
    const std::string& error) {
  for (auto& observer : observers_) {
    observer.OnContainerError(container_id, error);
  }
}

void TabContainerManager::InitializeSubManagers() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  network_context_manager_ = 
      std::make_unique<tab_container::ContainerNetworkContextManager>(
          browser_context_);

  proxy_manager_ = 
      std::make_unique<tab_container::ContainerProxyManager>(browser_context_);

  dns_manager_ = 
      std::make_unique<tab_container::ContainerDnsManager>(browser_context_);

  restore_manager_ = 
      std::make_unique<tab_container::ContainerRestoreManager>(browser_context_);

  ;
}

void TabContainerManager::CleanupSubManagerResources(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  if (network_context_manager_) {
    network_context_manager_->DestroyNetworkContext(container_id);
  }

  if (proxy_manager_) {
    proxy_manager_->RemoveProxyConfig(container_id);
  }

  if (dns_manager_) {
    dns_manager_->RemoveDnsConfig(container_id);
  }
}
