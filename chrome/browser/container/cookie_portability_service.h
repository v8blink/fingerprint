
#ifndef CHROME_BROWSER_CONTAINER_COOKIE_PORTABILITY_SERVICE_H_
#define CHROME_BROWSER_CONTAINER_COOKIE_PORTABILITY_SERVICE_H_

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "chrome/browser/container/profile_exporter.h"
#include "chrome/browser/container/tab_session_applier.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/sessions/core/session_id.h"
#include "net/cookies/canonical_cookie.h"
#include "url/gurl.h"

class Profile;

namespace tab_container {

class CookiePortabilityService : public KeyedService {
 public:
  using ApplyCallback = base::OnceCallback<void(TabSessionApplyResult)>;
  using ReadCallback = base::OnceCallback<void(net::CookieList)>;
  using ProgressCallback = base::RepeatingCallback<void(int32_t)>;
  using MigrationProgressCallback =
      base::RepeatingCallback<void(const std::string&, int32_t, const std::string&)>;

  enum class RefreshSourceKind {
    kPersistedTanyaJson,
    kExternalJsonFile,
  };

  struct RefreshSource {
    RefreshSource();
    RefreshSource(const RefreshSource&);
    RefreshSource(RefreshSource&&);
    RefreshSource& operator=(const RefreshSource&);
    RefreshSource& operator=(RefreshSource&&);
    ~RefreshSource();

    RefreshSourceKind kind = RefreshSourceKind::kPersistedTanyaJson;
    base::FilePath file_path;
    bool write_back_to_tanya_json = false;
  };

  struct RefreshPreview {
    RefreshPreview();
    RefreshPreview(const RefreshPreview&);
    RefreshPreview(RefreshPreview&&);
    RefreshPreview& operator=(const RefreshPreview&);
    RefreshPreview& operator=(RefreshPreview&&);
    ~RefreshPreview();

    TabSessionApplyResult::Kind validation_kind =
        TabSessionApplyResult::Kind::kSuccess;
    std::vector<std::string> affected_domains;
    size_t cookies_deleted_first = 0;
    size_t cookies_to_write = 0;
    bool short_circuit_empty = false;
    bool write_back_to_tanya_json = false;
    std::string detail;
  };

  using PreviewCallback = base::OnceCallback<void(RefreshPreview)>;

  enum class MigrationSourceKind {
    kEncryptedTanyaJsonFile,
  };

  struct ExportRequest {
    ExportRequest();
    ExportRequest(const ExportRequest&);
    ExportRequest(ExportRequest&&);
    ExportRequest& operator=(const ExportRequest&);
    ExportRequest& operator=(ExportRequest&&);
    ~ExportRequest();

    base::FilePath output_path;
  };

  struct ImportPreview {
    ImportPreview();
    ImportPreview(const ImportPreview&);
    ImportPreview(ImportPreview&&);
    ImportPreview& operator=(const ImportPreview&);
    ImportPreview& operator=(ImportPreview&&);
    ~ImportPreview();

    int envelope_error = 0;
    int host_match = 0;
    int envelope_schema_version = 0;
    uint32_t tab_count = 0;
    uint32_t cookie_total = 0;
    std::vector<std::string> affected_domains;
    base::Time created_at;
  };

  struct ImportRequest {
    ImportRequest();
    ImportRequest(const ImportRequest&);
    ImportRequest(ImportRequest&&);
    ImportRequest& operator=(const ImportRequest&);
    ImportRequest& operator=(ImportRequest&&);
    ~ImportRequest();

    base::FilePath input_path;
    PersistSemantic persist = PersistSemantic::kPersistent;
  };

  using ImportPreviewCallback = base::OnceCallback<void(ImportPreview)>;

  explicit CookiePortabilityService(Profile* profile);
  ~CookiePortabilityService() override;

  CookiePortabilityService(const CookiePortabilityService&) = delete;
  CookiePortabilityService& operator=(const CookiePortabilityService&) = delete;

  void SetSingleCookie(SessionID tab_id,
                       net::CanonicalCookie cookie,
                       WriteOptions options,
                       ApplyCallback on_done);

  void ApplyCookiesFromExternalSource(SessionID tab_id,
                                      std::vector<net::CanonicalCookie> cookies,
                                      WriteOptions options,
                                      ApplyCallback on_done);

  void ReplaceDomainCookies(SessionID tab_id,
                            const std::string& domain,
                            std::vector<net::CanonicalCookie> cookies,
                            PersistSemantic persist,
                            ApplyCallback on_done);

  void DeleteDomainCookies(SessionID tab_id,
                           const std::string& domain,
                           ApplyCallback on_done);

  void DeleteCookies(SessionID tab_id,
                     std::vector<net::CanonicalCookie> cookies,
                     ApplyCallback on_done);

  void ReadTabCookies(SessionID tab_id, ReadCallback on_done);

  void PreviewRefresh(SessionID tab_id,
                      const RefreshSource& source,
                      PreviewCallback on_done);

  void RefreshFromPersistedSource(SessionID tab_id,
                                  const RefreshSource& source,
                                  ProgressCallback on_progress,
                                  ApplyCallback on_done);
  void ExportProfileToEncryptedFile(ExportRequest req,
                                    MigrationProgressCallback on_progress,
                                    ApplyCallback on_done);
  void PreviewImportProfile(base::FilePath input_path,
                            ImportPreviewCallback on_done);
  void ImportProfileFromEncryptedFile(ImportRequest req,
                                      MigrationProgressCallback on_progress,
                                      ApplyCallback on_done);

  void Shutdown() override;

 private:
  struct RefreshBundle {
    RefreshBundle();
    RefreshBundle(const RefreshBundle&);
    RefreshBundle(RefreshBundle&&);
    RefreshBundle& operator=(const RefreshBundle&);
    RefreshBundle& operator=(RefreshBundle&&);
    ~RefreshBundle();

    std::vector<net::CanonicalCookie> cookies;
    std::string detail_source;
    std::string source_label;
  };

  struct RefreshReplacePlan {
    RefreshReplacePlan();
    RefreshReplacePlan(const RefreshReplacePlan&);
    RefreshReplacePlan(RefreshReplacePlan&&);
    RefreshReplacePlan& operator=(const RefreshReplacePlan&);
    RefreshReplacePlan& operator=(RefreshReplacePlan&&);
    ~RefreshReplacePlan();

    std::vector<std::string> affected_domains;
    base::flat_map<std::string, std::vector<net::CanonicalCookie>> cookies_by_domain;
    size_t cookies_deleted_first = 0;
    size_t cookies_to_write = 0;
    bool short_circuit_empty = false;
  };

  struct ImportPlanEntry {
    ImportPlanEntry();
    ImportPlanEntry(const ImportPlanEntry&);
    ImportPlanEntry(ImportPlanEntry&&);
    ImportPlanEntry& operator=(const ImportPlanEntry&);
    ImportPlanEntry& operator=(ImportPlanEntry&&);
    ~ImportPlanEntry();

    SessionID tab_id = SessionID::InvalidValue();
    std::string original_tab_label;

    GURL primary_url;
    std::vector<net::CanonicalCookie> cookies;
  };

  class RefreshGuard {
   public:
    RefreshGuard(CookiePortabilityService* service, SessionID tab_id);
    RefreshGuard(const RefreshGuard&) = delete;
    RefreshGuard& operator=(const RefreshGuard&) = delete;
    ~RefreshGuard();

    static std::unique_ptr<RefreshGuard> Acquire(
        CookiePortabilityService* service,
        SessionID tab_id);

   private:
    raw_ptr<CookiePortabilityService> service_ = nullptr;
    SessionID tab_id_ = SessionID::InvalidValue();
  };

  void SetSingleCookieImpl(SessionID tab_id,
                           net::CanonicalCookie cookie,
                           WriteOptions options,
                           ApplyCallback on_done);
  void ApplyCookiesFromExternalSourceImpl(
      SessionID tab_id,
      std::vector<net::CanonicalCookie> cookies,
      WriteOptions options,
      ApplyCallback on_done);
  void ReplaceDomainCookiesImpl(SessionID tab_id,
                                const std::string& domain,
                                std::vector<net::CanonicalCookie> cookies,
                                PersistSemantic persist,
                                ApplyCallback on_done);
  void DeleteDomainCookiesImpl(SessionID tab_id,
                               const std::string& domain,
                               ApplyCallback on_done);
  void DeleteCookiesImpl(SessionID tab_id,
                         std::vector<net::CanonicalCookie> cookies,
                         ApplyCallback on_done);

  base::expected<RefreshBundle, TabSessionApplyResult> ResolveRefreshSource(
      SessionID tab_id,
      const RefreshSource& source) const;
  RefreshReplacePlan BuildReplacePlan(const RefreshBundle& bundle,
                                      const net::CookieList& current_cookies) const;
  void ExecuteReplacePlan(SessionID tab_id,
                          const RefreshReplacePlan& plan,
                          PersistSemantic persist,
                          ApplyCallback on_done);
  base::expected<profile_exporter::ProfileEnvelope,
                 profile_exporter::EnvelopeError>
  LoadAndDecodeEnvelope(const base::FilePath& path);

  std::vector<ImportPlanEntry> PlanImportPerTab(
      const profile_exporter::ProfileEnvelope& env);
  void ExecuteImportPlan(std::vector<ImportPlanEntry> plan,
                         WriteOptions options,
                         const base::FilePath& attachment_path,
                         MigrationProgressCallback on_progress,
                         const std::string& operation_id,
                         ApplyCallback on_done);

  bool EnqueueOrRunTabWrite(SessionID tab_id, base::OnceClosure task);
  void FinishTabWrite(SessionID tab_id);
  void MaybeRunQueuedTabWrite(SessionID tab_id);

  std::optional<TabSessionApplyResult> ValidateTargetTab(SessionID tab_id) const;
  std::optional<TabSessionApplyResult> ValidateOwningProfile(
      SessionID tab_id) const;

  raw_ptr<Profile> profile_;
  base::flat_set<int32_t> refreshing_tabs_;
  base::flat_set<int32_t> active_tab_writes_;
  base::flat_map<int32_t, std::deque<base::OnceClosure>> queued_tab_writes_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<CookiePortabilityService> weak_factory_{this};
};

}  

#endif  
