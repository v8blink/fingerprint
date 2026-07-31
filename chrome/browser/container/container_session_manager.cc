
#include "chrome/browser/container/container_session_manager.h"

#include <algorithm>
#include <sstream>

#include "base/files/file_util.h"
#include "base/hash/hash.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "chrome/browser/container/encryption_util.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"

namespace tab_container {

namespace {

std::string SessionStateToString(SessionState state) {
  switch (state) {
    case SessionState::kNotCreated: return "NotCreated";
    case SessionState::kCreating: return "Creating";
    case SessionState::kActive: return "Active";
    case SessionState::kSuspended: return "Suspended";
    case SessionState::kSaving: return "Saving";
    case SessionState::kRestoring: return "Restoring";
    case SessionState::kDestroying: return "Destroying";
    case SessionState::kDestroyed: return "Destroyed";
    case SessionState::kError: return "Error";
  }
  return "Unknown";
}

std::string PersistenceModeToString(SessionPersistenceMode mode) {
  switch (mode) {
    case SessionPersistenceMode::kNone: return "None";
    case SessionPersistenceMode::kMemoryOnly: return "MemoryOnly";
    case SessionPersistenceMode::kDisk: return "Disk";
    case SessionPersistenceMode::kEncrypted: return "Encrypted";
  }
  return "Unknown";
}

}  

bool SessionSnapshot::IsValid() const {
  return !session_id.empty() && !container_id.empty();
}

size_t SessionSnapshot::GetSizeBytes() const {
  size_t size = session_id.size() + container_id.size();

  for (const auto& nav : navigation_entries) {
    size += nav.url.size() + nav.title.size() + nav.referrer.size();
    for (const auto& h : nav.back_history) size += h.size();
    for (const auto& h : nav.forward_history) size += h.size();
  }

  for (const auto& form : form_entries) {
    size += form.origin.size() + form.form_id.size();
    for (const auto& f : form.field_values) {
      size += f.first.size() + f.second.size();
    }
  }

  size += local_storage_data.size() + session_storage_data.size() + 
          cookies_data.size();

  for (const auto& m : metadata) {
    size += m.first.size() + m.second.size();
  }

  return size;
}

std::string SessionSnapshot::Serialize() const {
  base::DictValue dict;
  dict.Set("session_id", session_id);
  dict.Set("container_id", container_id);
  dict.Set("created_at",
           static_cast<double>(created_at.ToInternalValue()));
  dict.Set("modified_at",
           static_cast<double>(modified_at.ToInternalValue()));
  dict.Set("current_navigation_index", current_navigation_index);

  base::ListValue nav_list;
  for (const auto& nav : navigation_entries) {
    base::DictValue nav_dict;
    nav_dict.Set("url", nav.url);
    nav_dict.Set("title", nav.title);
    nav_dict.Set("referrer", nav.referrer);
    nav_dict.Set("scroll_x", nav.scroll_x);
    nav_dict.Set("scroll_y", nav.scroll_y);

    base::ListValue back_list;
    for (const auto& h : nav.back_history) {
      back_list.Append(h);
    }
    nav_dict.Set("back_history", std::move(back_list));

    base::ListValue forward_list;
    for (const auto& h : nav.forward_history) {
      forward_list.Append(h);
    }
    nav_dict.Set("forward_history", std::move(forward_list));

    nav_list.Append(std::move(nav_dict));
  }
  dict.Set("navigation_entries", std::move(nav_list));

  base::ListValue form_list;
  for (const auto& form : form_entries) {
    base::DictValue form_dict;
    form_dict.Set("origin", form.origin);
    form_dict.Set("form_id", form.form_id);

    base::DictValue fields;
    for (const auto& f : form.field_values) {
      fields.Set(f.first, f.second);
    }
    form_dict.Set("field_values", std::move(fields));

    form_list.Append(std::move(form_dict));
  }
  dict.Set("form_entries", std::move(form_list));

  dict.Set("local_storage_data", local_storage_data);
  dict.Set("session_storage_data", session_storage_data);
  dict.Set("cookies_data", cookies_data);

  base::DictValue meta;
  for (const auto& m : metadata) {
    meta.Set(m.first, m.second);
  }
  dict.Set("metadata", std::move(meta));

  std::string output;
  base::JSONWriter::Write(dict, &output);
  return output;
}

std::optional<SessionSnapshot> SessionSnapshot::Deserialize(
    const std::string& data) {
  auto parsed = base::JSONReader::Read(
      data, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }

  SessionSnapshot snapshot;
  const base::DictValue& dict = parsed->GetDict();

  const std::string* session_id = dict.FindString("session_id");
  if (session_id) snapshot.session_id = *session_id;

  const std::string* container_id = dict.FindString("container_id");
  if (container_id) snapshot.container_id = *container_id;

  snapshot.current_navigation_index = 
      dict.FindInt("current_navigation_index").value_or(0);

  return snapshot;
}

ContainerSessionManager::ContainerSessionManager(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  StartAutoSaveTimer();
}

ContainerSessionManager::~ContainerSessionManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  StopAutoSaveTimer();

  SaveAllSessions();

  ;
}

void ContainerSessionManager::AddObserver(ContainerSessionObserver* observer) {
  observers_.AddObserver(observer);
}

void ContainerSessionManager::RemoveObserver(
    ContainerSessionObserver* observer) {
  observers_.RemoveObserver(observer);
}

std::string ContainerSessionManager::CreateSession(
    const std::string& container_id,
    const ContainerSessionConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  auto existing_it = container_to_session_.find(container_id);
  if (existing_it != container_to_session_.end()) {
    ;
    return existing_it->second;
  }

  std::string session_id = GenerateSessionId();

  auto info = std::make_unique<ContainerSessionInfo>();
  info->session_id = session_id;
  info->container_id = container_id;
  info->config = config;
  info->config.container_id = container_id;
  info->created_at = base::TimeTicks::Now();
  info->last_activity = base::TimeTicks::Now();

  TransitionState(info.get(), SessionState::kCreating);

  sessions_[session_id] = std::move(info);
  container_to_session_[container_id] = session_id;

  TransitionState(sessions_[session_id].get(), SessionState::kActive);

  NotifySessionCreated(container_id, session_id);

  ;

  return session_id;
}

std::string ContainerSessionManager::GetOrCreateSession(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_to_session_.find(container_id);
  if (it != container_to_session_.end()) {
    TouchSession(container_id);
    return it->second;
  }

  return CreateSession(container_id, default_config_);
}

bool ContainerSessionManager::DestroySession(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    ;
    return false;
  }

  std::string session_id = session_it->second;

  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    container_to_session_.erase(session_it);
    return false;
  }

  TransitionState(it->second.get(), SessionState::kDestroying);

  if (it->second->config.persistence_mode != SessionPersistenceMode::kNone) {
    PersistSession(container_id);
  }

  TransitionState(it->second.get(), SessionState::kDestroyed);

  NotifySessionDestroyed(container_id, session_id);

  scroll_positions_.erase(container_id);
  container_to_session_.erase(session_it);
  sessions_.erase(it);

  ;

  return true;
}

std::optional<std::string> ContainerSessionManager::GetSessionId(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_to_session_.find(container_id);
  if (it == container_to_session_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<ContainerSessionInfo> ContainerSessionManager::GetSessionInfo(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return std::nullopt;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end()) {
    return std::nullopt;
  }

  return *it->second;
}

SessionState ContainerSessionManager::GetSessionState(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return SessionState::kNotCreated;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end()) {
    return SessionState::kNotCreated;
  }

  return it->second->state;
}

bool ContainerSessionManager::SaveSession(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return false;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end()) {
    return false;
  }

  ContainerSessionInfo* info = it->second.get();
  SessionState prev_state = info->state;

  TransitionState(info, SessionState::kSaving);

  SessionSnapshot snapshot;
  snapshot.session_id = info->session_id;
  snapshot.container_id = container_id;
  snapshot.created_at = base::Time::Now();
  snapshot.modified_at = base::Time::Now();

  if (info->snapshot.has_value()) {
    snapshot.navigation_entries = info->snapshot->navigation_entries;
    snapshot.current_navigation_index = info->snapshot->current_navigation_index;
    snapshot.form_entries = info->snapshot->form_entries;
  }

  info->snapshot = snapshot;
  info->last_save = base::TimeTicks::Now();
  info->save_count++;

  bool success = true;
  if (info->config.persistence_mode == SessionPersistenceMode::kDisk ||
      info->config.persistence_mode == SessionPersistenceMode::kEncrypted) {
    success = PersistSession(container_id);
  }

  TransitionState(info, success ? prev_state : SessionState::kError);

  if (success) {
    NotifySessionSaved(container_id, info->session_id);
  } else {
    NotifyError(container_id, "Failed to save session");
    info->error_count++;
  }

  return success;
}

void ContainerSessionManager::SaveSessionAsync(
    const std::string& container_id,
    SaveCallback callback) {

  bool success = SaveSession(container_id);
  std::move(callback).Run(success);
}

bool ContainerSessionManager::RestoreSession(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {

    return LoadSession(container_id);
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end()) {
    return false;
  }

  ContainerSessionInfo* info = it->second.get();
  TransitionState(info, SessionState::kRestoring);

  bool success = true;

  if (!info->snapshot.has_value()) {
    success = LoadSession(container_id);
  }

  if (success) {
    info->restore_count++;
    TransitionState(info, SessionState::kActive);
    NotifySessionRestored(container_id, info->session_id);
  } else {
    TransitionState(info, SessionState::kError);
    NotifyError(container_id, "Failed to restore session");
    info->error_count++;
  }

  return success;
}

void ContainerSessionManager::RestoreSessionAsync(
    const std::string& container_id,
    RestoreCallback callback) {
  bool success = RestoreSession(container_id);
  std::move(callback).Run(success);
}

size_t ContainerSessionManager::SaveAllSessions() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  size_t saved = 0;
  for (const auto& pair : container_to_session_) {
    if (SaveSession(pair.first)) {
      saved++;
    }
  }

  ;

  return saved;
}

std::vector<std::string> ContainerSessionManager::GetRestorableSessions() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<std::string> result;

  for (const auto& pair : sessions_) {
    if (pair.second->snapshot.has_value() &&
        pair.second->snapshot->IsValid()) {
      result.push_back(pair.second->container_id);
    }
  }

  return result;
}

bool ContainerSessionManager::SuspendSession(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return false;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end()) {
    return false;
  }

  SaveSession(container_id);

  TransitionState(it->second.get(), SessionState::kSuspended);

  it->second->current_memory_usage = 0;

  return true;
}

bool ContainerSessionManager::ResumeSession(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return false;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end() || it->second->state != SessionState::kSuspended) {
    return false;
  }

  TransitionState(it->second.get(), SessionState::kActive);
  TouchSession(container_id);

  return true;
}

void ContainerSessionManager::TouchSession(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return;
  }

  auto it = sessions_.find(session_it->second);
  if (it != sessions_.end()) {
    it->second->last_activity = base::TimeTicks::Now();
  }
}

bool ContainerSessionManager::IsSessionExpired(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return true;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end()) {
    return true;
  }

  base::TimeDelta elapsed = base::TimeTicks::Now() - it->second->last_activity;
  return elapsed > it->second->config.session_timeout;
}

bool ContainerSessionManager::CaptureNavigationState(
    const std::string& container_id,
    content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!web_contents) {
    return false;
  }

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return false;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end()) {
    return false;
  }

  if (!it->second->snapshot.has_value()) {
    it->second->snapshot = SessionSnapshot();
    it->second->snapshot->session_id = it->second->session_id;
    it->second->snapshot->container_id = container_id;
  }

  SessionSnapshot::NavigationState nav;
  nav.url = web_contents->GetLastCommittedURL().spec();
  nav.title = base::UTF16ToUTF8(web_contents->GetTitle());

  auto scroll_it = scroll_positions_.find(container_id);
  if (scroll_it != scroll_positions_.end()) {
    auto url_it = scroll_it->second.find(nav.url);
    if (url_it != scroll_it->second.end()) {
      nav.scroll_x = url_it->second.first;
      nav.scroll_y = url_it->second.second;
    }
  }

  it->second->snapshot->navigation_entries.push_back(nav);
  it->second->snapshot->current_navigation_index = 
      it->second->snapshot->navigation_entries.size() - 1;
  it->second->snapshot->modified_at = base::Time::Now();

  ;

  return true;
}

bool ContainerSessionManager::RestoreNavigationState(
    const std::string& container_id,
    content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!web_contents) {
    return false;
  }

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return false;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end() || !it->second->snapshot.has_value()) {
    return false;
  }

  const SessionSnapshot& snapshot = *it->second->snapshot;

  if (snapshot.navigation_entries.empty()) {
    return false;
  }

  int index = std::min(snapshot.current_navigation_index,
                       static_cast<int>(snapshot.navigation_entries.size()) - 1);
  const auto& nav = snapshot.navigation_entries[index];

  scroll_positions_[container_id][nav.url] = {nav.scroll_x, nav.scroll_y};

  ;

  return true;
}

void ContainerSessionManager::AddNavigationEntry(
    const std::string& container_id,
    const std::string& url,
    const std::string& title) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end()) {
    return;
  }

  if (!it->second->snapshot.has_value()) {
    it->second->snapshot = SessionSnapshot();
    it->second->snapshot->session_id = it->second->session_id;
    it->second->snapshot->container_id = container_id;
  }

  SessionSnapshot::NavigationState nav;
  nav.url = url;
  nav.title = title;

  it->second->snapshot->navigation_entries.push_back(nav);
  it->second->snapshot->current_navigation_index =
      it->second->snapshot->navigation_entries.size() - 1;

  TouchSession(container_id);
}

std::vector<SessionSnapshot::NavigationState>
ContainerSessionManager::GetNavigationHistory(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return {};
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end() || !it->second->snapshot.has_value()) {
    return {};
  }

  return it->second->snapshot->navigation_entries;
}

bool ContainerSessionManager::SaveFormData(
    const std::string& container_id,
    const std::string& origin,
    const std::string& form_id,
    const std::map<std::string, std::string>& data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return false;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end()) {
    return false;
  }

  if (!it->second->snapshot.has_value()) {
    it->second->snapshot = SessionSnapshot();
    it->second->snapshot->session_id = it->second->session_id;
    it->second->snapshot->container_id = container_id;
  }

  auto& form_entries = it->second->snapshot->form_entries;
  auto form_it = std::find_if(form_entries.begin(), form_entries.end(),
      [&](const SessionSnapshot::FormEntry& e) {
        return e.origin == origin && e.form_id == form_id;
      });

  if (form_it != form_entries.end()) {
    form_it->field_values = data;
  } else {
    SessionSnapshot::FormEntry entry;
    entry.origin = origin;
    entry.form_id = form_id;
    entry.field_values = data;
    form_entries.push_back(entry);
  }

  TouchSession(container_id);

  return true;
}

std::optional<std::map<std::string, std::string>>
ContainerSessionManager::GetFormData(
    const std::string& container_id,
    const std::string& origin,
    const std::string& form_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return std::nullopt;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end() || !it->second->snapshot.has_value()) {
    return std::nullopt;
  }

  const auto& form_entries = it->second->snapshot->form_entries;
  auto form_it = std::find_if(form_entries.begin(), form_entries.end(),
      [&](const SessionSnapshot::FormEntry& e) {
        return e.origin == origin && e.form_id == form_id;
      });

  if (form_it == form_entries.end()) {
    return std::nullopt;
  }

  return form_it->field_values;
}

void ContainerSessionManager::ClearFormData(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return;
  }

  auto it = sessions_.find(session_it->second);
  if (it != sessions_.end() && it->second->snapshot.has_value()) {
    it->second->snapshot->form_entries.clear();
  }
}

void ContainerSessionManager::SaveScrollPosition(
    const std::string& container_id,
    const std::string& url,
    int x, int y) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  scroll_positions_[container_id][url] = {x, y};

  auto session_it = container_to_session_.find(container_id);
  if (session_it != container_to_session_.end()) {
    auto it = sessions_.find(session_it->second);
    if (it != sessions_.end() && it->second->snapshot.has_value()) {
      for (auto& nav : it->second->snapshot->navigation_entries) {
        if (nav.url == url) {
          nav.scroll_x = x;
          nav.scroll_y = y;
          break;
        }
      }
    }
  }
}

std::pair<int, int> ContainerSessionManager::GetScrollPosition(
    const std::string& container_id,
    const std::string& url) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto container_it = scroll_positions_.find(container_id);
  if (container_it == scroll_positions_.end()) {
    return {0, 0};
  }

  auto url_it = container_it->second.find(url);
  if (url_it == container_it->second.end()) {
    return {0, 0};
  }

  return url_it->second;
}

bool ContainerSessionManager::UpdateSessionConfig(
    const std::string& container_id,
    const ContainerSessionConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return false;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end()) {
    return false;
  }

  it->second->config = config;
  it->second->config.container_id = container_id;

  return true;
}

std::optional<ContainerSessionConfig>
ContainerSessionManager::GetSessionConfig(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return std::nullopt;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end()) {
    return std::nullopt;
  }

  return it->second->config;
}

void ContainerSessionManager::SetDefaultConfig(
    const ContainerSessionConfig& config) {
  default_config_ = config;
}

size_t ContainerSessionManager::GetTotalMemoryUsage() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  size_t total = 0;
  for (const auto& pair : sessions_) {
    if (pair.second->snapshot.has_value()) {
      total += pair.second->snapshot->GetSizeBytes();
    }
  }
  return total;
}

size_t ContainerSessionManager::GetSessionMemoryUsage(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return 0;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end() || !it->second->snapshot.has_value()) {
    return 0;
  }

  return it->second->snapshot->GetSizeBytes();
}

size_t ContainerSessionManager::EvictLRUSessions(size_t bytes_to_free) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (bytes_to_free == 0) {
    return 0;
  }

  std::vector<std::pair<base::TimeTicks, std::string>> sessions_by_activity;
  for (const auto& pair : sessions_) {
    if (pair.second->state != SessionState::kActive) {
      continue;
    }
    sessions_by_activity.emplace_back(pair.second->last_activity,
                                       pair.second->container_id);
  }

  std::sort(sessions_by_activity.begin(), sessions_by_activity.end());

  size_t freed = 0;
  for (const auto& entry : sessions_by_activity) {
    if (freed >= bytes_to_free) {
      break;
    }

    size_t session_size = GetSessionMemoryUsage(entry.second);
    if (SuspendSession(entry.second)) {
      freed += session_size;
    }
  }

  return freed;
}

void ContainerSessionManager::SetMemoryLimit(size_t max_bytes) {
  memory_limit_ = max_bytes;
}

void ContainerSessionManager::ClearSessionData(
    const std::string& container_id) {
  ClearSessionData(container_id, static_cast<uint32_t>(SessionDataType::kAll));
}

void ContainerSessionManager::ClearSessionData(
    const std::string& container_id,
    uint32_t data_types) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end() || !it->second->snapshot.has_value()) {
    return;
  }

  SessionSnapshot& snapshot = *it->second->snapshot;

  if (data_types & static_cast<uint32_t>(SessionDataType::kNavigationHistory)) {
    snapshot.navigation_entries.clear();
    snapshot.current_navigation_index = 0;
  }
  if (data_types & static_cast<uint32_t>(SessionDataType::kFormData)) {
    snapshot.form_entries.clear();
  }
  if (data_types & static_cast<uint32_t>(SessionDataType::kScrollPosition)) {
    scroll_positions_.erase(container_id);
    for (auto& nav : snapshot.navigation_entries) {
      nav.scroll_x = 0;
      nav.scroll_y = 0;
    }
  }
  if (data_types & static_cast<uint32_t>(SessionDataType::kCookies)) {
    snapshot.cookies_data.clear();
  }
  if (data_types & static_cast<uint32_t>(SessionDataType::kLocalStorage)) {
    snapshot.local_storage_data.clear();
  }
  if (data_types & static_cast<uint32_t>(SessionDataType::kSessionStorage)) {
    snapshot.session_storage_data.clear();
  }

  snapshot.modified_at = base::Time::Now();
}

size_t ContainerSessionManager::CleanupExpiredSessions() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<std::string> expired;
  for (const auto& pair : container_to_session_) {
    if (IsSessionExpired(pair.first)) {
      expired.push_back(pair.first);
    }
  }

  for (const auto& container_id : expired) {
    DestroySession(container_id);
  }

  ;

  return expired.size();
}

size_t ContainerSessionManager::CleanupOrphanedSessionFiles() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return 0;  
}

ContainerSessionManager::SessionStatistics
ContainerSessionManager::GetStatistics() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  SessionStatistics stats;

  for (const auto& pair : sessions_) {
    if (pair.second->state == SessionState::kActive) {
      stats.active_sessions++;
    } else if (pair.second->state == SessionState::kSuspended) {
      stats.suspended_sessions++;
    }

    stats.total_saves += pair.second->save_count;
    stats.total_restores += pair.second->restore_count;
    stats.total_errors += pair.second->error_count;

    if (pair.second->snapshot.has_value()) {
      stats.total_memory_usage += pair.second->snapshot->GetSizeBytes();
    }

    if (stats.oldest_session.is_null() || 
        pair.second->created_at < stats.oldest_session) {
      stats.oldest_session = pair.second->created_at;
    }
    if (stats.newest_session.is_null() ||
        pair.second->created_at > stats.newest_session) {
      stats.newest_session = pair.second->created_at;
    }
  }

  return stats;
}

std::vector<std::string>
ContainerSessionManager::GetAllSessionContainerIds() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<std::string> result;
  for (const auto& pair : container_to_session_) {
    result.push_back(pair.first);
  }
  return result;
}

void ContainerSessionManager::SetDebugLoggingEnabled(bool enabled) {
  debug_logging_enabled_ = enabled;
}

std::string ContainerSessionManager::GetDiagnosticReport() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::stringstream report;
  report << "=== ContainerSessionManager Diagnostic Report ===\n\n";

  SessionStatistics stats = GetStatistics();
  report << "Statistics:\n";
  report << "  Active Sessions: " << stats.active_sessions << "\n";
  report << "  Suspended Sessions: " << stats.suspended_sessions << "\n";
  report << "  Total Memory Usage: " << stats.total_memory_usage << " bytes\n";
  report << "  Total Saves: " << stats.total_saves << "\n";
  report << "  Total Restores: " << stats.total_restores << "\n";
  report << "  Total Errors: " << stats.total_errors << "\n";
  report << "  Memory Limit: " << memory_limit_ << " bytes\n\n";

  report << "Sessions:\n";
  for (const auto& pair : sessions_) {
    const auto* info = pair.second.get();
    report << "  - " << info->session_id << " (" << info->container_id << "):\n";
    report << "      State: " << SessionStateToString(info->state) << "\n";
    report << "      Persistence: " 
           << PersistenceModeToString(info->config.persistence_mode) << "\n";
    report << "      Saves: " << info->save_count << "\n";
    report << "      Restores: " << info->restore_count << "\n";
    if (info->snapshot.has_value()) {
      report << "      Navigation Entries: " 
             << info->snapshot->navigation_entries.size() << "\n";
      report << "      Form Entries: " 
             << info->snapshot->form_entries.size() << "\n";
      report << "      Snapshot Size: " 
             << info->snapshot->GetSizeBytes() << " bytes\n";
    }
  }

  return report.str();
}

void ContainerSessionManager::DumpStateToLog() const {
  ;
}

std::string ContainerSessionManager::GenerateSessionId() {
  return "session_" + base::NumberToString(++session_id_counter_) + "_" +
         base::NumberToString(base::RandUint64() % 1000000);
}

void ContainerSessionManager::TransitionState(
    ContainerSessionInfo* info,
    SessionState new_state) {
  SessionState old_state = info->state;
  info->state = new_state;

  if (debug_logging_enabled_) {
    ;
  }

  NotifyStateChanged(info->container_id, old_state, new_state);
}

bool ContainerSessionManager::PersistSession(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto session_it = container_to_session_.find(container_id);
  if (session_it == container_to_session_.end()) {
    return false;
  }

  auto it = sessions_.find(session_it->second);
  if (it == sessions_.end() || !it->second->snapshot.has_value()) {
    return false;
  }

  std::string data = it->second->snapshot->Serialize();

  if (it->second->config.enable_compression) {
    data = CompressData(data);
  }

  if (it->second->config.persistence_mode == SessionPersistenceMode::kEncrypted) {
    data = EncryptData(data, it->second->config.encryption_key_id);
  }

  base::FilePath path = GetSessionStoragePath(it->second->session_id);
  ;

  return true;
}

bool ContainerSessionManager::LoadSession(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return false;
}

base::FilePath ContainerSessionManager::GetSessionStoragePath(
    const std::string& session_id) const {

  return base::FilePath();
}

std::string ContainerSessionManager::CompressData(const std::string& data) const {

  return data;
}

std::string ContainerSessionManager::DecompressData(const std::string& data) const {
  return data;
}

std::string ContainerSessionManager::EncryptData(
    const std::string& data,
    const std::string& key_id) const {
  return EncryptProfilePayload(data, key_id);
}

std::string ContainerSessionManager::DecryptData(
    const std::string& data,
    const std::string& key_id) const {
  return DecryptProfilePayload(data, key_id);
}

void ContainerSessionManager::NotifySessionCreated(
    const std::string& container_id,
    const std::string& session_id) {
  for (auto& observer : observers_) {
    observer.OnSessionCreated(container_id, session_id);
  }
}

void ContainerSessionManager::NotifyStateChanged(
    const std::string& container_id,
    SessionState old_state,
    SessionState new_state) {
  for (auto& observer : observers_) {
    observer.OnSessionStateChanged(container_id, old_state, new_state);
  }
}

void ContainerSessionManager::NotifySessionSaved(
    const std::string& container_id,
    const std::string& session_id) {
  for (auto& observer : observers_) {
    observer.OnSessionSaved(container_id, session_id);
  }
}

void ContainerSessionManager::NotifySessionRestored(
    const std::string& container_id,
    const std::string& session_id) {
  for (auto& observer : observers_) {
    observer.OnSessionRestored(container_id, session_id);
  }
}

void ContainerSessionManager::NotifySessionDestroyed(
    const std::string& container_id,
    const std::string& session_id) {
  for (auto& observer : observers_) {
    observer.OnSessionDestroyed(container_id, session_id);
  }
}

void ContainerSessionManager::NotifyError(
    const std::string& container_id,
    const std::string& error) {
  for (auto& observer : observers_) {
    observer.OnSessionError(container_id, error);
  }
}

void ContainerSessionManager::OnAutoSaveTimer() {
  if (debug_logging_enabled_) {
    ;
  }

  for (const auto& pair : sessions_) {
    if (pair.second->state == SessionState::kActive &&
        pair.second->config.auto_save) {
      SaveSession(pair.second->container_id);
    }
  }
}

void ContainerSessionManager::StartAutoSaveTimer() {

}

void ContainerSessionManager::StopAutoSaveTimer() {

}

}  
