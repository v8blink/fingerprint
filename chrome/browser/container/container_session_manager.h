
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_SESSION_MANAGER_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_SESSION_MANAGER_H_

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
#include "base/values.h"

namespace content {
class BrowserContext;
class WebContents;
}  

namespace tab_container {

enum class SessionPersistenceMode {

  kNone = 0,

  kMemoryOnly = 1,

  kDisk = 2,

  kEncrypted = 3,
};

enum class SessionState {
  kNotCreated,
  kCreating,
  kActive,
  kSuspended,
  kSaving,
  kRestoring,
  kDestroying,
  kDestroyed,
  kError,
};

enum class SessionDataType {
  kNavigationHistory = 1 << 0,
  kFormData = 1 << 1,
  kScrollPosition = 1 << 2,
  kCookies = 1 << 3,
  kLocalStorage = 1 << 4,
  kSessionStorage = 1 << 5,
  kIndexedDB = 1 << 6,
  kServiceWorkers = 1 << 7,
  kCacheStorage = 1 << 8,
  kAll = 0xFFFF,
};

struct ContainerSessionConfig {
  std::string container_id;
  SessionPersistenceMode persistence_mode = SessionPersistenceMode::kMemoryOnly;

  uint32_t data_types = static_cast<uint32_t>(SessionDataType::kAll);

  bool auto_save = true;
  base::TimeDelta save_interval = base::Minutes(5);
  base::TimeDelta session_timeout = base::Hours(24);
  bool restore_on_startup = true;

  size_t max_memory_bytes = 100 * 1024 * 1024;  
  bool enable_compression = true;

  bool require_encryption = false;
  std::string encryption_key_id;

  ContainerSessionConfig() = default;
};

struct SessionSnapshot {
  std::string session_id;
  std::string container_id;
  base::Time created_at;
  base::Time modified_at;

  struct NavigationState {
    std::string url;
    std::string title;
    std::string referrer;
    int scroll_x = 0;
    int scroll_y = 0;
    std::vector<std::string> back_history;
    std::vector<std::string> forward_history;
  };
  std::vector<NavigationState> navigation_entries;
  int current_navigation_index = 0;

  struct FormEntry {
    std::string origin;
    std::string form_id;
    std::map<std::string, std::string> field_values;
  };
  std::vector<FormEntry> form_entries;

  std::string local_storage_data;
  std::string session_storage_data;
  std::string cookies_data;

  std::map<std::string, std::string> metadata;

  bool IsValid() const;
  size_t GetSizeBytes() const;

  std::string Serialize() const;
  static std::optional<SessionSnapshot> Deserialize(const std::string& data);

  SessionSnapshot() = default;
};

struct ContainerSessionInfo {
  std::string session_id;
  std::string container_id;
  SessionState state = SessionState::kNotCreated;
  ContainerSessionConfig config;

  base::TimeTicks created_at;
  base::TimeTicks last_activity;
  base::TimeTicks last_save;

  std::optional<SessionSnapshot> snapshot;

  size_t save_count = 0;
  size_t restore_count = 0;
  size_t error_count = 0;
  size_t current_memory_usage = 0;

  ContainerSessionInfo() = default;
};

class ContainerSessionObserver : public base::CheckedObserver {
 public:
  ~ContainerSessionObserver() override = default;

  virtual void OnSessionCreated(const std::string& container_id,
                                const std::string& session_id) {}
  virtual void OnSessionStateChanged(const std::string& container_id,
                                     SessionState old_state,
                                     SessionState new_state) {}
  virtual void OnSessionSaved(const std::string& container_id,
                              const std::string& session_id) {}
  virtual void OnSessionRestored(const std::string& container_id,
                                 const std::string& session_id) {}
  virtual void OnSessionDestroyed(const std::string& container_id,
                                  const std::string& session_id) {}
  virtual void OnSessionError(const std::string& container_id,
                              const std::string& error) {}
};

class ContainerSessionManager {
 public:
  explicit ContainerSessionManager(content::BrowserContext* browser_context);
  ~ContainerSessionManager();

  ContainerSessionManager(const ContainerSessionManager&) = delete;
  ContainerSessionManager& operator=(const ContainerSessionManager&) = delete;

  void AddObserver(ContainerSessionObserver* observer);
  void RemoveObserver(ContainerSessionObserver* observer);

  std::string CreateSession(const std::string& container_id,
                           const ContainerSessionConfig& config);

  std::string GetOrCreateSession(const std::string& container_id);

  bool DestroySession(const std::string& container_id);

  std::optional<std::string> GetSessionId(const std::string& container_id) const;

  std::optional<ContainerSessionInfo> GetSessionInfo(
      const std::string& container_id) const;

  SessionState GetSessionState(const std::string& container_id) const;

  bool SaveSession(const std::string& container_id);

  using SaveCallback = base::OnceCallback<void(bool success)>;
  void SaveSessionAsync(const std::string& container_id,
                        SaveCallback callback);

  bool RestoreSession(const std::string& container_id);

  using RestoreCallback = base::OnceCallback<void(bool success)>;
  void RestoreSessionAsync(const std::string& container_id,
                           RestoreCallback callback);

  size_t SaveAllSessions();

  std::vector<std::string> GetRestorableSessions() const;

  bool SuspendSession(const std::string& container_id);

  bool ResumeSession(const std::string& container_id);

  void TouchSession(const std::string& container_id);

  bool IsSessionExpired(const std::string& container_id) const;

  bool CaptureNavigationState(const std::string& container_id,
                              content::WebContents* web_contents);

  bool RestoreNavigationState(const std::string& container_id,
                              content::WebContents* web_contents);

  void AddNavigationEntry(const std::string& container_id,
                          const std::string& url,
                          const std::string& title);

  std::vector<SessionSnapshot::NavigationState> GetNavigationHistory(
      const std::string& container_id) const;

  bool SaveFormData(const std::string& container_id,
                    const std::string& origin,
                    const std::string& form_id,
                    const std::map<std::string, std::string>& data);

  std::optional<std::map<std::string, std::string>> GetFormData(
      const std::string& container_id,
      const std::string& origin,
      const std::string& form_id) const;

  void ClearFormData(const std::string& container_id);

  void SaveScrollPosition(const std::string& container_id,
                          const std::string& url,
                          int x, int y);

  std::pair<int, int> GetScrollPosition(const std::string& container_id,
                                        const std::string& url) const;

  bool UpdateSessionConfig(const std::string& container_id,
                           const ContainerSessionConfig& config);

  std::optional<ContainerSessionConfig> GetSessionConfig(
      const std::string& container_id) const;

  void SetDefaultConfig(const ContainerSessionConfig& config);

  size_t GetTotalMemoryUsage() const;

  size_t GetSessionMemoryUsage(const std::string& container_id) const;

  size_t EvictLRUSessions(size_t bytes_to_free);

  void SetMemoryLimit(size_t max_bytes);

  void ClearSessionData(const std::string& container_id);

  void ClearSessionData(const std::string& container_id,
                        uint32_t data_types);

  size_t CleanupExpiredSessions();

  size_t CleanupOrphanedSessionFiles();

  struct SessionStatistics {
    size_t active_sessions = 0;
    size_t suspended_sessions = 0;
    size_t total_memory_usage = 0;
    size_t total_saves = 0;
    size_t total_restores = 0;
    size_t total_errors = 0;
    base::TimeTicks oldest_session;
    base::TimeTicks newest_session;
  };
  SessionStatistics GetStatistics() const;

  std::vector<std::string> GetAllSessionContainerIds() const;

  void SetDebugLoggingEnabled(bool enabled);
  std::string GetDiagnosticReport() const;
  void DumpStateToLog() const;

 private:

  std::string GenerateSessionId();

  void TransitionState(ContainerSessionInfo* info, SessionState new_state);

  bool PersistSession(const std::string& container_id);

  bool LoadSession(const std::string& container_id);

  base::FilePath GetSessionStoragePath(const std::string& session_id) const;

  std::string CompressData(const std::string& data) const;
  std::string DecompressData(const std::string& data) const;

  std::string EncryptData(const std::string& data,
                          const std::string& key_id) const;
  std::string DecryptData(const std::string& data,
                          const std::string& key_id) const;

  void NotifySessionCreated(const std::string& container_id,
                            const std::string& session_id);
  void NotifyStateChanged(const std::string& container_id,
                          SessionState old_state,
                          SessionState new_state);
  void NotifySessionSaved(const std::string& container_id,
                          const std::string& session_id);
  void NotifySessionRestored(const std::string& container_id,
                             const std::string& session_id);
  void NotifySessionDestroyed(const std::string& container_id,
                              const std::string& session_id);
  void NotifyError(const std::string& container_id, const std::string& error);

  void OnAutoSaveTimer();

  void StartAutoSaveTimer();
  void StopAutoSaveTimer();

  raw_ptr<content::BrowserContext> browser_context_;

  std::map<std::string, std::unique_ptr<ContainerSessionInfo>> sessions_;

  std::map<std::string, std::string> container_to_session_;

  std::map<std::string, std::map<std::string, std::pair<int, int>>> scroll_positions_;

  ContainerSessionConfig default_config_;

  size_t memory_limit_ = 500 * 1024 * 1024;  
  bool debug_logging_enabled_ = false;

  uint64_t session_id_counter_ = 0;

  base::ObserverList<ContainerSessionObserver> observers_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ContainerSessionManager> weak_factory_{this};
};

}  

#endif  
