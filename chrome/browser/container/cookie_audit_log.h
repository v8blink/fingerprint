
#ifndef CHROME_BROWSER_CONTAINER_COOKIE_AUDIT_LOG_H_
#define CHROME_BROWSER_CONTAINER_COOKIE_AUDIT_LOG_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/time/time.h"
#include "chrome/browser/container/tab_session_applier.h"
#include "components/sessions/core/session_id.h"

class Profile;

namespace tab_container {

struct CookieAuditEntry {
  enum class Source {
    kManualCrud,
    kRefresh,
    kMigration,
  };

  enum class Op {
    kRead,
    kWrite,
    kDelete,
    kReplace,
    kImport,
    kExport,
    kRefresh,
    kMigrationExport,
    kMigrationImport,
  };

  CookieAuditEntry();
  CookieAuditEntry(const CookieAuditEntry&);
  CookieAuditEntry(CookieAuditEntry&&);
  CookieAuditEntry& operator=(const CookieAuditEntry&);
  CookieAuditEntry& operator=(CookieAuditEntry&&);
  ~CookieAuditEntry();

  base::Time when;
  std::string who;
  Source source = Source::kManualCrud;
  Op op = Op::kRead;
  SessionID target_tab = SessionID::InvalidValue();
  std::string target_profile_id;
  TabSessionApplyResult::Kind result_kind =
      TabSessionApplyResult::Kind::kSuccess;
  std::vector<std::string> cookie_digests;
  std::string detail;
};

struct QueryFilters {
  std::string profile_filter;
  std::string source_filter;

  base::Time date_from;
  base::Time date_to;
};

class CookieAuditLog {
 public:
  static void RecordOperation(Profile* profile, CookieAuditEntry entry);
  static void Query(
      Profile* profile,
      const QueryFilters& filters,
      base::OnceCallback<void(std::vector<CookieAuditEntry>)> on_done);

  static base::FilePath GetLogPathForTesting(Profile* profile);
};

}  

#endif  
