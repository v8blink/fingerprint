
#ifndef CHROME_BROWSER_CONTAINER_TAB_SESSION_APPLIER_H_
#define CHROME_BROWSER_CONTAINER_TAB_SESSION_APPLIER_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "chrome/browser/container/cookie_apply.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/sessions/core/session_id.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_options.h"
#include "url/gurl.h"

class Profile;
class TabContainerManager;

namespace content {
class StoragePartition;
class WebContents;
}  

namespace network::mojom {
class CookieManager;
}  

namespace tab_container {

enum class WriteScope {
  kPartialOverride,
};

enum class PersistSemantic {
  kOneTime,
  kPersistent,
};

struct WriteOptions {
  WriteOptions();
  WriteOptions(const WriteOptions&);
  WriteOptions(WriteOptions&&);
  WriteOptions& operator=(const WriteOptions&);
  WriteOptions& operator=(WriteOptions&&);
  ~WriteOptions();

  WriteScope scope = WriteScope::kPartialOverride;
  PersistSemantic persist = PersistSemantic::kOneTime;
  net::CookieOptions cookie_options = net::CookieOptions::MakeAllInclusive();
  std::optional<GURL> override_source_url;
};

struct TabSessionApplyResult {
  enum class Kind {
    kSuccess,
    kPartialSuccess,
    kFailedInvalidSessionId,
    kFailedTabNotFound,
    kFailedTabClosed,
    kFailedNoContainerManager,
    kFailedNoStoragePartition,
    kFailedDefaultPartition,
    kFailedNoCookieManager,
    kFailedDeletePhase,
    kFailedAllApplyRejected,
    kFailedNoPersistAttachment,
    kFailedPersistFlush,
    kMaxValue = kFailedPersistFlush,
  } kind = Kind::kSuccess;

  TabSessionApplyResult();
  TabSessionApplyResult(const TabSessionApplyResult&);
  TabSessionApplyResult(TabSessionApplyResult&&);
  TabSessionApplyResult& operator=(const TabSessionApplyResult&);
  TabSessionApplyResult& operator=(TabSessionApplyResult&&);
  ~TabSessionApplyResult();

  size_t cookies_attempted = 0;
  size_t cookies_applied = 0;
  size_t cookies_deleted_first = 0;
  std::vector<std::pair<std::string, std::string>> skipped;
  std::optional<std::string> error_detail;
};

class TabSessionApplier : public KeyedService {
 public:
  using ApplyCallback = base::OnceCallback<void(TabSessionApplyResult)>;
  using ReadCallback = base::OnceCallback<void(net::CookieList)>;

  explicit TabSessionApplier(Profile* profile);
  ~TabSessionApplier() override;

  TabSessionApplier(const TabSessionApplier&) = delete;
  TabSessionApplier& operator=(const TabSessionApplier&) = delete;

  void ApplyCookiesToTab(SessionID tab_id,
                         std::vector<net::CanonicalCookie> cookies,
                         WriteOptions options,
                         ApplyCallback on_done);

  void ReplaceCookiesForDomain(SessionID tab_id,
                               const std::string& domain,
                               std::vector<net::CanonicalCookie> cookies,
                               PersistSemantic persist,
                               ApplyCallback on_done);

  void DeleteCookiesForDomain(SessionID tab_id,
                              const std::string& domain,
                              ApplyCallback on_done);

  void DeleteCookiesForTab(SessionID tab_id,
                           std::vector<net::CanonicalCookie> cookies,
                           ApplyCallback on_done);

  void ReadCookiesForTab(SessionID tab_id, ReadCallback on_done);

  void Shutdown() override;

  enum class PersistFlushResult {
    kScheduled,
    kNoAttachment,
    kFailedSchedule,
  };

  struct ResolvedTabContext {
    ResolvedTabContext();
    ResolvedTabContext(const ResolvedTabContext&);
    ResolvedTabContext(ResolvedTabContext&&);
    ResolvedTabContext& operator=(const ResolvedTabContext&);
    ResolvedTabContext& operator=(ResolvedTabContext&&);
    ~ResolvedTabContext();

    base::WeakPtr<content::WebContents> web_contents;
    raw_ptr<content::StoragePartition> partition = nullptr;
    raw_ptr<network::mojom::CookieManager> cookie_manager = nullptr;
  };

  std::optional<ResolvedTabContext> ResolveTabContext(
      SessionID tab_id,
      TabSessionApplyResult* failure_out);

  PersistFlushResult MaybeSchedulePersistFlush(ResolvedTabContext context);

  void OnApplyCompleted(ResolvedTabContext context,
                        PersistSemantic persist,
                        size_t deleted_first,
                        ApplyCallback on_done,
                        cookie_apply::CookieApplyOutcome outcome);
  void OnDeleteCompleted(ResolvedTabContext context,
                         std::string domain,
                         ApplyCallback on_done,
                         cookie_apply::DeleteCookiesOutcome outcome);
  void OnDeleteCookiesForTabCompleted(
      ResolvedTabContext context,
      size_t attempted,
      ApplyCallback on_done,
      cookie_apply::DeleteCookiesOutcome outcome);

 private:
  void StartApply(SessionID tab_id,
                  std::vector<net::CanonicalCookie> cookies,
                  WriteOptions options,
                  ApplyCallback on_done);
  void StartReplace(SessionID tab_id,
                    std::string domain,
                    std::vector<net::CanonicalCookie> cookies,
                    PersistSemantic persist,
                    ApplyCallback on_done);
  void StartDelete(SessionID tab_id,
                   std::string domain,
                   ApplyCallback on_done);
  void StartDeleteCookiesForTab(SessionID tab_id,
                                std::vector<net::CanonicalCookie> cookies,
                                ApplyCallback on_done);
  void StartRead(SessionID tab_id, ReadCallback on_done);

  bool HasPersistAttachment(content::WebContents* contents) const;

  void OnReplaceDeleteCompleted(ResolvedTabContext context,
                                std::string domain,
                                std::vector<net::CanonicalCookie> cookies,
                                PersistSemantic persist,
                                ApplyCallback on_done,
                                cookie_apply::DeleteCookiesOutcome outcome);
  void OnReadFlushCompleted(ResolvedTabContext context, ReadCallback on_done);
  void OnReadCookiesCompleted(ResolvedTabContext context,
                              ReadCallback on_done,
                              net::CookieList cookies);

  raw_ptr<Profile> profile_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<TabSessionApplier> weak_factory_{this};
};

}  

#endif  
