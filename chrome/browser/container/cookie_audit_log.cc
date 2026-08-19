
#include "chrome/browser/container/cookie_audit_log.h"

#include <optional>
#include <utility>

#include "base/check.h"
#include "base/i18n/time_formatting.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_split.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/containers/span.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"

namespace tab_container {

CookieAuditEntry::CookieAuditEntry() = default;
CookieAuditEntry::CookieAuditEntry(const CookieAuditEntry&) = default;
CookieAuditEntry::CookieAuditEntry(CookieAuditEntry&&) = default;
CookieAuditEntry& CookieAuditEntry::operator=(const CookieAuditEntry&) = default;
CookieAuditEntry& CookieAuditEntry::operator=(CookieAuditEntry&&) = default;
CookieAuditEntry::~CookieAuditEntry() = default;

namespace {

constexpr int64_t kMaxAuditFileBytes = 16 * 1024 * 1024;
constexpr char kAuditDirName[] = "TanyaAudit";
constexpr char kAuditFileName[] = "cookie_audit.log";

std::string SourceToString(CookieAuditEntry::Source source) {
  switch (source) {
    case CookieAuditEntry::Source::kManualCrud:
      return "manual";
    case CookieAuditEntry::Source::kRefresh:
      return "refresh";
    case CookieAuditEntry::Source::kMigration:
      return "migration";
  }
}

std::string OpToString(CookieAuditEntry::Op op) {
  switch (op) {
    case CookieAuditEntry::Op::kRead:
      return "read";
    case CookieAuditEntry::Op::kWrite:
      return "write";
    case CookieAuditEntry::Op::kDelete:
      return "delete";
    case CookieAuditEntry::Op::kReplace:
      return "replace";
    case CookieAuditEntry::Op::kImport:
      return "import";
    case CookieAuditEntry::Op::kExport:
      return "export";
    case CookieAuditEntry::Op::kRefresh:
      return "refresh";
    case CookieAuditEntry::Op::kMigrationExport:
      return "migration_export";
    case CookieAuditEntry::Op::kMigrationImport:
      return "migration_import";
  }
}

CookieAuditEntry::Source StringToSource(const std::string& source) {
  if (source == "refresh") {
    return CookieAuditEntry::Source::kRefresh;
  }
  if (source == "migration") {
    return CookieAuditEntry::Source::kMigration;
  }
  return CookieAuditEntry::Source::kManualCrud;
}

CookieAuditEntry::Op StringToOp(const std::string& op) {
  if (op == "write") {
    return CookieAuditEntry::Op::kWrite;
  }
  if (op == "delete") {
    return CookieAuditEntry::Op::kDelete;
  }
  if (op == "replace") {
    return CookieAuditEntry::Op::kReplace;
  }
  if (op == "import") {
    return CookieAuditEntry::Op::kImport;
  }
  if (op == "export") {
    return CookieAuditEntry::Op::kExport;
  }
  if (op == "refresh") {
    return CookieAuditEntry::Op::kRefresh;
  }
  if (op == "migration_export") {
    return CookieAuditEntry::Op::kMigrationExport;
  }
  if (op == "migration_import") {
    return CookieAuditEntry::Op::kMigrationImport;
  }
  return CookieAuditEntry::Op::kRead;
}

std::optional<TabSessionApplyResult::Kind> IntToResultKind(int value) {
  switch (value) {
    case static_cast<int>(TabSessionApplyResult::Kind::kSuccess):
      return TabSessionApplyResult::Kind::kSuccess;
    case static_cast<int>(TabSessionApplyResult::Kind::kPartialSuccess):
      return TabSessionApplyResult::Kind::kPartialSuccess;
    case static_cast<int>(TabSessionApplyResult::Kind::kFailedInvalidSessionId):
      return TabSessionApplyResult::Kind::kFailedInvalidSessionId;
    case static_cast<int>(TabSessionApplyResult::Kind::kFailedTabNotFound):
      return TabSessionApplyResult::Kind::kFailedTabNotFound;
    case static_cast<int>(TabSessionApplyResult::Kind::kFailedTabClosed):
      return TabSessionApplyResult::Kind::kFailedTabClosed;
    case static_cast<int>(TabSessionApplyResult::Kind::kFailedNoContainerManager):
      return TabSessionApplyResult::Kind::kFailedNoContainerManager;
    case static_cast<int>(TabSessionApplyResult::Kind::kFailedNoStoragePartition):
      return TabSessionApplyResult::Kind::kFailedNoStoragePartition;
    case static_cast<int>(TabSessionApplyResult::Kind::kFailedDefaultPartition):
      return TabSessionApplyResult::Kind::kFailedDefaultPartition;
    case static_cast<int>(TabSessionApplyResult::Kind::kFailedNoCookieManager):
      return TabSessionApplyResult::Kind::kFailedNoCookieManager;
    case static_cast<int>(TabSessionApplyResult::Kind::kFailedDeletePhase):
      return TabSessionApplyResult::Kind::kFailedDeletePhase;
    case static_cast<int>(TabSessionApplyResult::Kind::kFailedAllApplyRejected):
      return TabSessionApplyResult::Kind::kFailedAllApplyRejected;
    case static_cast<int>(TabSessionApplyResult::Kind::kFailedNoPersistAttachment):
      return TabSessionApplyResult::Kind::kFailedNoPersistAttachment;
    case static_cast<int>(TabSessionApplyResult::Kind::kFailedPersistFlush):
      return TabSessionApplyResult::Kind::kFailedPersistFlush;
  }
  return std::nullopt;
}

base::FilePath GetAuditDirectory(Profile* profile) {
  CHECK(profile);
  return profile->GetPath().AppendASCII(kAuditDirName);
}

base::FilePath GetWritableLogPathBlocking(const base::FilePath& audit_dir) {
  const base::FilePath primary = audit_dir.AppendASCII(kAuditFileName);
  std::optional<int64_t> size = base::GetFileSize(primary);
  if (size.has_value() && *size >= kMaxAuditFileBytes) {
    return primary.AddExtensionASCII("1");
  }
  return primary;
}

base::DictValue EntryToDict(const CookieAuditEntry& entry) {
  base::DictValue dict;
  dict.Set("when", base::TimeFormatAsIso8601(entry.when));
  dict.Set("who", entry.who);
  dict.Set("source", SourceToString(entry.source));
  dict.Set("op", OpToString(entry.op));
  dict.Set("target_tab", entry.target_tab.id());
  dict.Set("target_profile_id", entry.target_profile_id);
  dict.Set("result_kind", static_cast<int>(entry.result_kind));
  base::ListValue digests;
  for (const std::string& digest : entry.cookie_digests) {
    digests.Append(digest);
  }
  dict.Set("cookie_digests", std::move(digests));
  dict.Set("detail", entry.detail);
  return dict;
}

std::optional<CookieAuditEntry> DictToEntry(const base::DictValue& dict) {
  const std::string* when = dict.FindString("when");
  const std::string* who = dict.FindString("who");
  const std::string* source = dict.FindString("source");
  const std::string* op = dict.FindString("op");
  const std::string* profile_id = dict.FindString("target_profile_id");
  const std::string* detail = dict.FindString("detail");
  std::optional<int> target_tab = dict.FindInt("target_tab");
  std::optional<int> result_kind = dict.FindInt("result_kind");
  const base::ListValue* digests = dict.FindList("cookie_digests");
  if (!when || !who || !source || !op || !profile_id || !detail ||
      !target_tab.has_value() || !result_kind.has_value() || !digests) {
    return std::nullopt;
  }

  base::Time parsed_when;
  if (!base::Time::FromUTCString(when->c_str(), &parsed_when)) {
    return std::nullopt;
  }

  CookieAuditEntry entry;
  entry.when = parsed_when;
  entry.who = *who;
  entry.source = StringToSource(*source);
  entry.op = StringToOp(*op);
  entry.target_tab = SessionID::FromSerializedValue(*target_tab);
  entry.target_profile_id = *profile_id;
  std::optional<TabSessionApplyResult::Kind> safe_kind =
      IntToResultKind(*result_kind);
  if (!safe_kind.has_value()) {
    return std::nullopt;
  }
  entry.result_kind = *safe_kind;
  for (const base::Value& digest : *digests) {
    if (digest.is_string()) {
      entry.cookie_digests.push_back(digest.GetString());
    }
  }
  entry.detail = *detail;
  return entry;
}

bool MatchesFilters(const CookieAuditEntry& entry, const QueryFilters& filters) {
  if (!filters.profile_filter.empty() &&
      entry.target_profile_id != filters.profile_filter) {
    return false;
  }
  if (!filters.source_filter.empty() &&
      SourceToString(entry.source) != filters.source_filter) {
    return false;
  }
  if (!filters.date_from.is_null() && entry.when < filters.date_from) {
    return false;
  }
  if (!filters.date_to.is_null() && entry.when > filters.date_to) {
    return false;
  }
  return true;
}

void AppendLine(const base::FilePath& log_path, const std::string& line) {
  base::CreateDirectory(log_path.DirName());
  std::string with_newline = line + "\n";
  base::AppendToFile(log_path, base::as_byte_span(with_newline));
}

std::vector<CookieAuditEntry> ReadEntriesFromLog(Profile* profile,
                                                 const QueryFilters& filters) {
  std::vector<CookieAuditEntry> result;
  std::vector<base::FilePath> candidates = {
      GetAuditDirectory(profile).AppendASCII(kAuditFileName),
      GetAuditDirectory(profile).AppendASCII(kAuditFileName).AddExtensionASCII("1"),
  };
  for (const base::FilePath& path : candidates) {
    std::string content;
    if (!base::ReadFileToString(path, &content)) {
      continue;
    }
    for (std::string_view line : base::SplitStringPiece(
             content, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      std::optional<base::Value> parsed =
          base::JSONReader::Read(line, base::JSON_PARSE_RFC);
      if (!parsed || !parsed->is_dict()) {
        continue;
      }
      std::optional<CookieAuditEntry> entry = DictToEntry(parsed->GetDict());
      if (entry && MatchesFilters(*entry, filters)) {
        result.push_back(std::move(*entry));
      }
    }
  }
  return result;
}

}  

void CookieAuditLog::RecordOperation(Profile* profile, CookieAuditEntry entry) {
  if (!profile) {
    return;
  }

  base::FilePath audit_dir = GetAuditDirectory(profile);
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(
          [](base::FilePath audit_dir, CookieAuditEntry task_entry) {
            base::FilePath log_path = GetWritableLogPathBlocking(audit_dir);
            std::string json;
            if (base::JSONWriter::Write(EntryToDict(task_entry), &json)) {
              AppendLine(log_path, json);
            }
          },
          std::move(audit_dir), std::move(entry)));
}

void CookieAuditLog::Query(
    Profile* profile,
    const QueryFilters& filters,
    base::OnceCallback<void(std::vector<CookieAuditEntry>)> on_done) {
  if (!profile) {
    std::move(on_done).Run({});
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&ReadEntriesFromLog, profile, filters), std::move(on_done));
}

base::FilePath CookieAuditLog::GetLogPathForTesting(Profile* profile) {
  return GetAuditDirectory(profile).AppendASCII(kAuditFileName);
}

}  
