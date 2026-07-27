
#include "chrome/browser/container/cookie_portability_service.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/token.h"
#include "chrome/browser/container/cookie_audit_log.h"
#include "chrome/browser/container/cookie_portability_codec.h"
#include "chrome/browser/container/createProfiles.h"
#include "chrome/browser/container/tanya_profile_persist_state.h"
#include "chrome/browser/container/profile_exporter.h"
#include "chrome/browser/container/profile_encryption_key_provider.h"
#include "chrome/browser/container/tab_container_manager.h"
#include "chrome/browser/container/tab_container_manager_factory.h"
#include "chrome/browser/container/tab_session_applier_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_controller.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "crypto/sha2.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/url_constants.h"

namespace tab_container {

namespace {


content::WebContents* FindWebContentsInProfile(Profile* profile,
                                               SessionID session_id) {
  if (!profile || !session_id.is_valid()) {
    return nullptr;
  }
  for (content::WebContentsImpl* contents_impl :
       content::WebContentsImpl::GetAllWebContents()) {
    content::WebContents* contents = contents_impl;
    if (!contents) {
      continue;
    }
    Profile* contents_profile =
        Profile::FromBrowserContext(contents->GetBrowserContext());
    if (contents_profile != profile) {
      continue;
    }
    if (sessions::SessionTabHelper::IdForTab(contents) == session_id) {
      return contents;
    }
  }
  return nullptr;
}

content::WebContents* FindWebContentsAcrossProfiles(SessionID session_id,
                                                    Profile** owning_profile) {
  if (owning_profile) {
    *owning_profile = nullptr;
  }
  if (!session_id.is_valid()) {
    return nullptr;
  }
  for (content::WebContentsImpl* contents_impl :
       content::WebContentsImpl::GetAllWebContents()) {
    content::WebContents* contents = contents_impl;
    if (!contents) {
      continue;
    }
    if (sessions::SessionTabHelper::IdForTab(contents) == session_id) {
      if (owning_profile) {
        *owning_profile =
            Profile::FromBrowserContext(contents->GetBrowserContext());
      }
      return contents;
    }
  }
  return nullptr;
}

TabSessionApplyResult MakeFailure(TabSessionApplyResult::Kind kind,
                                  std::string detail) {
  TabSessionApplyResult result;
  result.kind = kind;
  result.error_detail = std::move(detail);
  return result;
}

std::string DigestCookieForAudit(const net::CanonicalCookie& cookie) {
  const std::string material =
      base::StringPrintf("%s|%s|%s", cookie.Name().c_str(),
                         cookie.Domain().c_str(), cookie.Value().c_str());
  const std::string digest = crypto::SHA256HashString(material);
  return base::StringPrintf("%s@%s:%s", cookie.Name().c_str(),
                            cookie.Domain().c_str(),
                            base::HexEncode(std::string_view(digest.data(), 8))
                                .c_str());
}

std::vector<std::string> DigestCookiesForAudit(
    const std::vector<net::CanonicalCookie>& cookies) {
  std::vector<std::string> digests;
  digests.reserve(cookies.size());
  for (const net::CanonicalCookie& cookie : cookies) {
    digests.push_back(DigestCookieForAudit(cookie));
  }
  return digests;
}

std::vector<content::WebContents*> CollectTanyaTabsForProfile(Profile* profile) {
  std::vector<content::WebContents*> tabs;
  TabContainerManager* manager = GetForBrowserContext(profile);
  if (!manager) {
    return tabs;
  }
  for (content::WebContentsImpl* contents_impl :
       content::WebContentsImpl::GetAllWebContents()) {
    content::WebContents* contents = contents_impl;
    if (!contents ||
        Profile::FromBrowserContext(contents->GetBrowserContext()) != profile) {
      continue;
    }
    if (manager->GetContainerIdForTab(contents).empty()) {
      continue;
    }

    const GURL& url = contents->GetLastCommittedURL();
    if (!url.SchemeIsHTTPOrHTTPS()) {
      continue;
    }
    tabs.push_back(contents);
  }
  return tabs;
}

std::string ApplyKindToString(TabSessionApplyResult::Kind kind) {
  switch (kind) {
    case TabSessionApplyResult::Kind::kSuccess:
      return "kSuccess";
    case TabSessionApplyResult::Kind::kPartialSuccess:
      return "kPartialSuccess";
    case TabSessionApplyResult::Kind::kFailedInvalidSessionId:
      return "kFailedInvalidSessionId";
    case TabSessionApplyResult::Kind::kFailedTabNotFound:
      return "kFailedTabNotFound";
    case TabSessionApplyResult::Kind::kFailedTabClosed:
      return "kFailedTabClosed";
    case TabSessionApplyResult::Kind::kFailedNoContainerManager:
      return "kFailedNoContainerManager";
    case TabSessionApplyResult::Kind::kFailedNoStoragePartition:
      return "kFailedNoStoragePartition";
    case TabSessionApplyResult::Kind::kFailedDefaultPartition:
      return "kFailedDefaultPartition";
    case TabSessionApplyResult::Kind::kFailedNoCookieManager:
      return "kFailedNoCookieManager";
    case TabSessionApplyResult::Kind::kFailedDeletePhase:
      return "kFailedDeletePhase";
    case TabSessionApplyResult::Kind::kFailedAllApplyRejected:
      return "kFailedAllApplyRejected";
    case TabSessionApplyResult::Kind::kFailedNoPersistAttachment:
      return "kFailedNoPersistAttachment";
    case TabSessionApplyResult::Kind::kFailedPersistFlush:
      return "kFailedPersistFlush";
  }
  return "kFailedAllApplyRejected";
}

std::string HostMatchToDetail(
    profile_exporter::HostFingerprintMatch host_match) {
  switch (host_match) {
    case profile_exporter::HostFingerprintMatch::kSameHost:
      return "same";
    case profile_exporter::HostFingerprintMatch::kDifferentHost:
      return "different";
    case profile_exporter::HostFingerprintMatch::kUnknown:
      return "unknown";
  }
  return "unknown";
}

std::string EnvelopeErrorToString(profile_exporter::EnvelopeError error) {
  switch (error) {
    case profile_exporter::EnvelopeError::kOk:
      return "kOk";
    case profile_exporter::EnvelopeError::kFileIoFailed:
      return "kFileIoFailed";
    case profile_exporter::EnvelopeError::kDecryptFailed:
      return "kDecryptFailed";
    case profile_exporter::EnvelopeError::kJsonParseFailed:
      return "kJsonParseFailed";
    case profile_exporter::EnvelopeError::kSchemaVersionMismatch:
      return "kSchemaVersionMismatch";
    case profile_exporter::EnvelopeError::kFieldMissing:
      return "kFieldMissing";
    case profile_exporter::EnvelopeError::kFieldTypeWrong:
      return "kFieldTypeWrong";
  }
  return "kJsonParseFailed";
}

std::string ProfileIdFor(Profile* profile) {
  return profile ? profile->GetBaseName().AsUTF8Unsafe() : std::string();
}

CookieAuditEntry MakeAuditEntry(SessionID tab_id,
                                std::string profile_id,
                                CookieAuditEntry::Source source,
                                CookieAuditEntry::Op op,
                                TabSessionApplyResult result,
                                std::vector<std::string> digests,
                                std::string detail) {
  CookieAuditEntry entry;
  entry.when = base::Time::Now();
  entry.who = "local";
  entry.source = source;
  entry.op = op;
  entry.target_tab = tab_id;
  entry.target_profile_id = std::move(profile_id);
  entry.result_kind = result.kind;
  entry.cookie_digests = std::move(digests);
  entry.detail = detail.empty()
                     ? result.error_detail.value_or(std::string())
                     : std::move(detail);
  return entry;
}

void MergeApplyResults(TabSessionApplyResult* aggregate,
                       const TabSessionApplyResult& next) {
  aggregate->cookies_attempted += next.cookies_attempted;
  aggregate->cookies_applied += next.cookies_applied;
  aggregate->cookies_deleted_first += next.cookies_deleted_first;
  aggregate->skipped.insert(aggregate->skipped.end(), next.skipped.begin(),
                            next.skipped.end());
  if (aggregate->kind == TabSessionApplyResult::Kind::kSuccess &&
      next.kind != TabSessionApplyResult::Kind::kSuccess) {
    aggregate->kind =
        next.cookies_applied > 0 || aggregate->cookies_applied > 0
            ? TabSessionApplyResult::Kind::kPartialSuccess
            : next.kind;
  } else if (aggregate->kind == TabSessionApplyResult::Kind::kPartialSuccess ||
             next.kind == TabSessionApplyResult::Kind::kPartialSuccess) {
    aggregate->kind = TabSessionApplyResult::Kind::kPartialSuccess;
  } else if (aggregate->kind != TabSessionApplyResult::Kind::kSuccess &&
             next.kind == TabSessionApplyResult::Kind::kSuccess &&
             aggregate->cookies_applied > 0) {
    aggregate->kind = TabSessionApplyResult::Kind::kPartialSuccess;
  } else if (aggregate->kind != TabSessionApplyResult::Kind::kSuccess &&
             next.kind != TabSessionApplyResult::Kind::kSuccess &&
             aggregate->error_detail && next.error_detail) {
    aggregate->error_detail =
        *aggregate->error_detail + "; " + *next.error_detail;
  }
  if (next.error_detail.has_value()) {
    aggregate->error_detail = next.error_detail;
  }
}

CookiePortabilityService::RefreshPreview MakePreviewFailure(
    TabSessionApplyResult::Kind kind,
    std::string detail,
    bool write_back_to_tanya_json) {
  CookiePortabilityService::RefreshPreview preview;
  preview.validation_kind = kind;
  preview.detail = std::move(detail);
  preview.write_back_to_tanya_json = write_back_to_tanya_json;
  return preview;
}

struct ReplaceExecutionState {
  base::flat_map<std::string, std::vector<net::CanonicalCookie>> cookies_by_domain;
  std::vector<std::string> domains;
  size_t index = 0;
  PersistSemantic persist = PersistSemantic::kOneTime;
  TabSessionApplyResult aggregate;
  CookiePortabilityService::ApplyCallback on_done;

  raw_ptr<Profile> profile = nullptr;
};

void RunReplacePlanStep(base::WeakPtr<CookiePortabilityService> self,
                        SessionID tab_id,
                        std::shared_ptr<ReplaceExecutionState> state) {
  if (!self) {
    std::move(state->on_done)
        .Run(MakeFailure(TabSessionApplyResult::Kind::kFailedTabClosed,
                         "CookiePortabilityService was destroyed"));
    return;
  }
  if (state->index >= state->domains.size()) {
    std::move(state->on_done).Run(std::move(state->aggregate));
    return;
  }

  TabSessionApplier* applier = TabSessionApplierFactory::GetForProfile(state->profile);
  if (!applier) {
    TabSessionApplyResult result =
        MakeFailure(TabSessionApplyResult::Kind::kFailedNoCookieManager,
                    "TabSessionApplier is unavailable for refresh");
    MergeApplyResults(&state->aggregate, result);
    std::move(state->on_done).Run(std::move(state->aggregate));
    return;
  }

  const std::string domain = state->domains[state->index++];
  auto it = state->cookies_by_domain.find(domain);
  std::vector<net::CanonicalCookie> cookies =
      it != state->cookies_by_domain.end() ? std::move(it->second)
                                           : std::vector<net::CanonicalCookie>();
  if (it != state->cookies_by_domain.end()) {
    state->cookies_by_domain.erase(it);
  }

  const PersistSemantic persist_for_call = state->persist;
  applier->ReplaceCookiesForDomain(
      tab_id, domain, std::move(cookies), persist_for_call,
      base::BindOnce(
          [](base::WeakPtr<CookiePortabilityService> self, SessionID tab_id,
             std::shared_ptr<ReplaceExecutionState> state,
             TabSessionApplyResult result) {
            MergeApplyResults(&state->aggregate, result);
            RunReplacePlanStep(std::move(self), tab_id, std::move(state));
          },
          std::move(self), tab_id, std::move(state)));
}

}  

CookiePortabilityService::RefreshSource::RefreshSource() = default;
CookiePortabilityService::RefreshSource::RefreshSource(const RefreshSource&) =
    default;
CookiePortabilityService::RefreshSource::RefreshSource(RefreshSource&&) =
    default;
CookiePortabilityService::RefreshSource&
CookiePortabilityService::RefreshSource::operator=(const RefreshSource&) =
    default;
CookiePortabilityService::RefreshSource&
CookiePortabilityService::RefreshSource::operator=(RefreshSource&&) = default;
CookiePortabilityService::RefreshSource::~RefreshSource() = default;

CookiePortabilityService::RefreshPreview::RefreshPreview() = default;
CookiePortabilityService::RefreshPreview::RefreshPreview(
    const RefreshPreview&) = default;
CookiePortabilityService::RefreshPreview::RefreshPreview(RefreshPreview&&) =
    default;
CookiePortabilityService::RefreshPreview&
CookiePortabilityService::RefreshPreview::operator=(const RefreshPreview&) =
    default;
CookiePortabilityService::RefreshPreview&
CookiePortabilityService::RefreshPreview::operator=(RefreshPreview&&) = default;
CookiePortabilityService::RefreshPreview::~RefreshPreview() = default;

CookiePortabilityService::RefreshBundle::RefreshBundle() = default;
CookiePortabilityService::RefreshBundle::RefreshBundle(const RefreshBundle&) =
    default;
CookiePortabilityService::RefreshBundle::RefreshBundle(RefreshBundle&&) =
    default;
CookiePortabilityService::RefreshBundle&
CookiePortabilityService::RefreshBundle::operator=(const RefreshBundle&) =
    default;
CookiePortabilityService::RefreshBundle&
CookiePortabilityService::RefreshBundle::operator=(RefreshBundle&&) = default;
CookiePortabilityService::RefreshBundle::~RefreshBundle() = default;

CookiePortabilityService::RefreshReplacePlan::RefreshReplacePlan() = default;
CookiePortabilityService::RefreshReplacePlan::RefreshReplacePlan(
    const RefreshReplacePlan&) = default;
CookiePortabilityService::RefreshReplacePlan::RefreshReplacePlan(
    RefreshReplacePlan&&) = default;
CookiePortabilityService::RefreshReplacePlan&
CookiePortabilityService::RefreshReplacePlan::operator=(
    const RefreshReplacePlan&) = default;
CookiePortabilityService::RefreshReplacePlan&
CookiePortabilityService::RefreshReplacePlan::operator=(RefreshReplacePlan&&) =
    default;
CookiePortabilityService::RefreshReplacePlan::~RefreshReplacePlan() = default;

CookiePortabilityService::ExportRequest::ExportRequest() = default;
CookiePortabilityService::ExportRequest::ExportRequest(const ExportRequest&) =
    default;
CookiePortabilityService::ExportRequest::ExportRequest(ExportRequest&&) =
    default;
CookiePortabilityService::ExportRequest&
CookiePortabilityService::ExportRequest::operator=(const ExportRequest&) =
    default;
CookiePortabilityService::ExportRequest&
CookiePortabilityService::ExportRequest::operator=(ExportRequest&&) = default;
CookiePortabilityService::ExportRequest::~ExportRequest() = default;

CookiePortabilityService::ImportPreview::ImportPreview() = default;
CookiePortabilityService::ImportPreview::ImportPreview(const ImportPreview&) =
    default;
CookiePortabilityService::ImportPreview::ImportPreview(ImportPreview&&) =
    default;
CookiePortabilityService::ImportPreview&
CookiePortabilityService::ImportPreview::operator=(const ImportPreview&) =
    default;
CookiePortabilityService::ImportPreview&
CookiePortabilityService::ImportPreview::operator=(ImportPreview&&) = default;
CookiePortabilityService::ImportPreview::~ImportPreview() = default;

CookiePortabilityService::ImportRequest::ImportRequest() = default;
CookiePortabilityService::ImportRequest::ImportRequest(const ImportRequest&) =
    default;
CookiePortabilityService::ImportRequest::ImportRequest(ImportRequest&&) =
    default;
CookiePortabilityService::ImportRequest&
CookiePortabilityService::ImportRequest::operator=(const ImportRequest&) =
    default;
CookiePortabilityService::ImportRequest&
CookiePortabilityService::ImportRequest::operator=(ImportRequest&&) = default;
CookiePortabilityService::ImportRequest::~ImportRequest() = default;

CookiePortabilityService::ImportPlanEntry::ImportPlanEntry() = default;
CookiePortabilityService::ImportPlanEntry::ImportPlanEntry(
    const ImportPlanEntry&) = default;
CookiePortabilityService::ImportPlanEntry::ImportPlanEntry(ImportPlanEntry&&) =
    default;
CookiePortabilityService::ImportPlanEntry&
CookiePortabilityService::ImportPlanEntry::operator=(const ImportPlanEntry&) =
    default;
CookiePortabilityService::ImportPlanEntry&
CookiePortabilityService::ImportPlanEntry::operator=(ImportPlanEntry&&) =
    default;
CookiePortabilityService::ImportPlanEntry::~ImportPlanEntry() = default;

CookiePortabilityService::RefreshGuard::RefreshGuard(
    CookiePortabilityService* service,
    SessionID tab_id)
    : service_(service), tab_id_(tab_id) {}

CookiePortabilityService::RefreshGuard::~RefreshGuard() {
  if (!service_) {
    return;
  }
  service_->refreshing_tabs_.erase(tab_id_.id());
  service_->MaybeRunQueuedTabWrite(tab_id_);
}

std::unique_ptr<CookiePortabilityService::RefreshGuard>
CookiePortabilityService::RefreshGuard::Acquire(
    CookiePortabilityService* service,
    SessionID tab_id) {
  if (!service || !tab_id.is_valid()) {
    return nullptr;
  }
  const auto [it, inserted] = service->refreshing_tabs_.insert(tab_id.id());
  if (!inserted) {
    return nullptr;
  }
  return std::make_unique<RefreshGuard>(service, tab_id);
}

CookiePortabilityService::CookiePortabilityService(Profile* profile)
    : profile_(profile) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

CookiePortabilityService::~CookiePortabilityService() = default;

void CookiePortabilityService::SetSingleCookie(SessionID tab_id,
                                               net::CanonicalCookie cookie,
                                               WriteOptions options,
                                               ApplyCallback on_done) {
  DCHECK(profile_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (EnqueueOrRunTabWrite(
          tab_id, base::BindOnce(&CookiePortabilityService::SetSingleCookieImpl,
                                 weak_factory_.GetWeakPtr(), tab_id,
                                 std::move(cookie), std::move(options),
                                 std::move(on_done)))) {
    return;
  }
}

void CookiePortabilityService::ApplyCookiesFromExternalSource(
    SessionID tab_id,
    std::vector<net::CanonicalCookie> cookies,
    WriteOptions options,
    ApplyCallback on_done) {
  DCHECK(profile_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (EnqueueOrRunTabWrite(
          tab_id,
          base::BindOnce(
              &CookiePortabilityService::ApplyCookiesFromExternalSourceImpl,
              weak_factory_.GetWeakPtr(), tab_id, std::move(cookies),
              std::move(options), std::move(on_done)))) {
    return;
  }
}

void CookiePortabilityService::ReplaceDomainCookies(
    SessionID tab_id,
    const std::string& domain,
    std::vector<net::CanonicalCookie> cookies,
    PersistSemantic persist,
    ApplyCallback on_done) {
  DCHECK(profile_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (EnqueueOrRunTabWrite(
          tab_id,
          base::BindOnce(&CookiePortabilityService::ReplaceDomainCookiesImpl,
                         weak_factory_.GetWeakPtr(), tab_id, domain,
                         std::move(cookies), persist, std::move(on_done)))) {
    return;
  }
}

void CookiePortabilityService::DeleteDomainCookies(SessionID tab_id,
                                                   const std::string& domain,
                                                   ApplyCallback on_done) {
  DCHECK(profile_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (EnqueueOrRunTabWrite(
          tab_id, base::BindOnce(&CookiePortabilityService::DeleteDomainCookiesImpl,
                                 weak_factory_.GetWeakPtr(), tab_id, domain,
                                 std::move(on_done)))) {
    return;
  }
}

void CookiePortabilityService::DeleteCookies(
    SessionID tab_id,
    std::vector<net::CanonicalCookie> cookies,
    ApplyCallback on_done) {
  DCHECK(profile_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (EnqueueOrRunTabWrite(
          tab_id,
          base::BindOnce(&CookiePortabilityService::DeleteCookiesImpl,
                         weak_factory_.GetWeakPtr(), tab_id,
                         std::move(cookies), std::move(on_done)))) {
    return;
  }
}

void CookiePortabilityService::ReadTabCookies(SessionID tab_id,
                                              ReadCallback on_done) {
  DCHECK(profile_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  TabContainerManager* manager = GetExistingForBrowserContext(profile_);
  if (!manager) {
    TabSessionApplyResult result =
        MakeFailure(TabSessionApplyResult::Kind::kFailedNoContainerManager,
                    "TabContainerManager is not attached to this profile");
    CookieAuditLog::RecordOperation(
        profile_,
        MakeAuditEntry(tab_id, ProfileIdFor(profile_), CookieAuditEntry::Source::kManualCrud,
                       CookieAuditEntry::Op::kRead, result, {},
                       "ReadTabCookies missing manager"));
    std::move(on_done).Run({});
    return;
  }
  TabSessionApplier* applier = TabSessionApplierFactory::GetForProfile(profile_);
  if (!applier) {
    TabSessionApplyResult result =
        MakeFailure(TabSessionApplyResult::Kind::kFailedNoCookieManager,
                    "TabSessionApplier is unavailable for this profile");
    CookieAuditLog::RecordOperation(
        profile_,
        MakeAuditEntry(tab_id, ProfileIdFor(profile_), CookieAuditEntry::Source::kManualCrud,
                       CookieAuditEntry::Op::kRead, result, {},
                       "ReadTabCookies missing applier"));
    std::move(on_done).Run({});
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateTargetTab(tab_id)) {
    CookieAuditLog::RecordOperation(
        profile_,
        MakeAuditEntry(tab_id, ProfileIdFor(profile_), CookieAuditEntry::Source::kManualCrud,
                       CookieAuditEntry::Op::kRead, *guard, {},
                       "ReadTabCookies preflight rejected"));
    std::move(on_done).Run({});
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateOwningProfile(tab_id)) {
    CookieAuditLog::RecordOperation(
        profile_,
        MakeAuditEntry(tab_id, ProfileIdFor(profile_), CookieAuditEntry::Source::kManualCrud,
                       CookieAuditEntry::Op::kRead, *guard, {},
                       "ReadTabCookies profile guard rejected"));
    std::move(on_done).Run({});
    return;
  }

  applier->ReadCookiesForTab(
      tab_id, base::BindOnce(
                  [](base::WeakPtr<CookiePortabilityService> self,
                     SessionID tab_id, std::string profile_id,
                     ReadCallback on_done, net::CookieList cookies) {
                    if (self && self->profile_) {
                      TabSessionApplyResult result;
                      result.kind = TabSessionApplyResult::Kind::kSuccess;
                      result.cookies_attempted = cookies.size();
                      result.cookies_applied = cookies.size();
                      std::vector<std::string> digests;
                      digests.reserve(cookies.size());
                      for (const net::CanonicalCookie& cookie : cookies) {
                        digests.push_back(DigestCookieForAudit(cookie));
                      }
                      CookieAuditLog::RecordOperation(
                          self->profile_,
                          MakeAuditEntry(tab_id, std::move(profile_id),
                                         CookieAuditEntry::Source::kManualCrud,
                                         CookieAuditEntry::Op::kRead, result,
                                         std::move(digests),
                                         "ReadTabCookies completed"));
                    }
                    std::move(on_done).Run(std::move(cookies));
                  },
                  weak_factory_.GetWeakPtr(), tab_id, ProfileIdFor(profile_),
                  std::move(on_done)));
}

void CookiePortabilityService::PreviewRefresh(SessionID tab_id,
                                              const RefreshSource& source,
                                              PreviewCallback on_done) {
  DCHECK(profile_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!profile_) {
    std::move(on_done).Run(MakePreviewFailure(
        TabSessionApplyResult::Kind::kFailedNoContainerManager,
        "Profile is unavailable", source.write_back_to_tanya_json));
    return;
  }
  if (!GetExistingForBrowserContext(profile_)) {
    std::move(on_done).Run(MakePreviewFailure(
        TabSessionApplyResult::Kind::kFailedNoContainerManager,
        "TabContainerManager is not attached to this profile",
        source.write_back_to_tanya_json));
    return;
  }
  if (!TabSessionApplierFactory::GetForProfile(profile_)) {
    std::move(on_done).Run(MakePreviewFailure(
        TabSessionApplyResult::Kind::kFailedNoCookieManager,
        "TabSessionApplier is unavailable for this profile",
        source.write_back_to_tanya_json));
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateTargetTab(tab_id)) {
    std::move(on_done).Run(MakePreviewFailure(guard->kind,
                                              guard->error_detail.value_or(""),
                                              source.write_back_to_tanya_json));
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateOwningProfile(tab_id)) {
    std::move(on_done).Run(MakePreviewFailure(guard->kind,
                                              guard->error_detail.value_or(""),
                                              source.write_back_to_tanya_json));
    return;
  }

  base::expected<RefreshBundle, TabSessionApplyResult> bundle =
      ResolveRefreshSource(tab_id, source);
  if (!bundle.has_value()) {
    std::move(on_done).Run(MakePreviewFailure(
        bundle.error().kind, bundle.error().error_detail.value_or(std::string()),
        source.write_back_to_tanya_json));
    return;
  }

  ReadTabCookies(
      tab_id,
      base::BindOnce(
          [](base::WeakPtr<CookiePortabilityService> self,
             RefreshSource source,
             RefreshBundle bundle,
             PreviewCallback on_done,
             net::CookieList cookies) {
            if (!self) {
              std::move(on_done).Run(MakePreviewFailure(
                  TabSessionApplyResult::Kind::kFailedTabClosed,
                  "CookiePortabilityService was destroyed",
                  source.write_back_to_tanya_json));
              return;
            }
            RefreshReplacePlan plan = self->BuildReplacePlan(bundle, cookies);
            RefreshPreview preview;
            preview.validation_kind =
                plan.short_circuit_empty
                    ? TabSessionApplyResult::Kind::kFailedAllApplyRejected
                    : TabSessionApplyResult::Kind::kSuccess;
            preview.affected_domains = plan.affected_domains;
            preview.cookies_deleted_first = plan.cookies_deleted_first;
            preview.cookies_to_write = plan.cookies_to_write;
            preview.short_circuit_empty = plan.short_circuit_empty;
            preview.write_back_to_tanya_json = source.write_back_to_tanya_json;
            preview.detail = bundle.detail_source;
            std::move(on_done).Run(std::move(preview));
          },
          weak_factory_.GetWeakPtr(), source, std::move(bundle.value()),
          std::move(on_done)));
}

void CookiePortabilityService::RefreshFromPersistedSource(
    SessionID tab_id,
    const RefreshSource& source,
    ProgressCallback on_progress,
    ApplyCallback on_done) {
  DCHECK(profile_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!profile_) {
    std::move(on_done).Run(MakeFailure(
        TabSessionApplyResult::Kind::kFailedNoContainerManager,
        "Profile is unavailable"));
    return;
  }
  if (!GetExistingForBrowserContext(profile_)) {
    std::move(on_done).Run(MakeFailure(
        TabSessionApplyResult::Kind::kFailedNoContainerManager,
        "TabContainerManager is not attached to this profile"));
    return;
  }
  if (!TabSessionApplierFactory::GetForProfile(profile_)) {
    std::move(on_done).Run(MakeFailure(
        TabSessionApplyResult::Kind::kFailedNoCookieManager,
        "TabSessionApplier is unavailable for this profile"));
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateTargetTab(tab_id)) {
    std::move(on_done).Run(std::move(*guard));
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateOwningProfile(tab_id)) {
    std::move(on_done).Run(std::move(*guard));
    return;
  }

  std::unique_ptr<RefreshGuard> refresh_guard = RefreshGuard::Acquire(this, tab_id);
  if (!refresh_guard) {
    std::move(on_done).Run(MakeFailure(
        TabSessionApplyResult::Kind::kFailedAllApplyRejected,
        "Refresh already in progress for this tab"));
    return;
  }

  const base::TimeTicks refresh_start = base::TimeTicks::Now();
  base::expected<RefreshBundle, TabSessionApplyResult> bundle =
      ResolveRefreshSource(tab_id, source);
  if (!bundle.has_value()) {
    TabSessionApplyResult result = bundle.error();
    base::UmaHistogramEnumeration("PTI.CookieRefresh.ResultKind", result.kind);
    base::UmaHistogramTimes("PTI.CookieRefresh.Duration",
                            base::TimeTicks::Now() - refresh_start);
    base::UmaHistogramBoolean("PTI.CookieRefresh.EmptyShortCircuit", false);
    CookieAuditLog::RecordOperation(
        profile_,
        MakeAuditEntry(
            tab_id, ProfileIdFor(profile_), CookieAuditEntry::Source::kRefresh,
            CookieAuditEntry::Op::kRefresh, result, {},
            source.kind == RefreshSourceKind::kPersistedTanyaJson
                ? "source=persisted write_back=" +
                      std::string(source.write_back_to_tanya_json ? "true"
                                                                : "false") +
                      " affected_domains=0"
                : "source=file write_back=" +
                      std::string(source.write_back_to_tanya_json ? "true"
                                                                : "false") +
                      " affected_domains=0"));
    std::move(on_done).Run(std::move(result));
    return;
  }

  if (!on_progress.is_null()) {
    on_progress.Run(50);
  }

  ReadTabCookies(
      tab_id,
      base::BindOnce(
          [](base::WeakPtr<CookiePortabilityService> self,
             SessionID tab_id,
             RefreshSource source,
             ProgressCallback on_progress,
             base::TimeTicks refresh_start,
             std::unique_ptr<RefreshGuard> refresh_guard,
             RefreshBundle bundle,
             ApplyCallback on_done,
             net::CookieList current_cookies) {
            if (!self) {
              std::move(on_done).Run(MakeFailure(
                  TabSessionApplyResult::Kind::kFailedTabClosed,
                  "CookiePortabilityService was destroyed"));
              return;
            }
            RefreshReplacePlan plan =
                self->BuildReplacePlan(bundle, current_cookies);
            if (!on_progress.is_null()) {
              on_progress.Run(75);
            }

            if (plan.short_circuit_empty) {
              TabSessionApplyResult result =
                  MakeFailure(TabSessionApplyResult::Kind::kFailedAllApplyRejected,
                              "Refresh resolved to an empty cookie set");
              base::UmaHistogramEnumeration("PTI.CookieRefresh.ResultKind",
                                            result.kind);
              base::UmaHistogramTimes("PTI.CookieRefresh.Duration",
                                      base::TimeTicks::Now() - refresh_start);
              base::UmaHistogramBoolean("PTI.CookieRefresh.EmptyShortCircuit",
                                        true);
              CookieAuditLog::RecordOperation(
                  self->profile_,
                  MakeAuditEntry(
                      tab_id, ProfileIdFor(self->profile_),
                      CookieAuditEntry::Source::kRefresh,
                      CookieAuditEntry::Op::kRefresh, result, {},
                      bundle.detail_source + " write_back=" +
                          std::string(source.write_back_to_tanya_json ? "true"
                                                                    : "false") +
                          " affected_domains=0"));
              std::move(on_done).Run(std::move(result));
              return;
            }

            const PersistSemantic persist =
                source.write_back_to_tanya_json ? PersistSemantic::kPersistent
                                               : PersistSemantic::kOneTime;

            const size_t plan_affected_domains_count =
                plan.affected_domains.size();
            self->ExecuteReplacePlan(
                tab_id, plan, persist,
                base::BindOnce(
                    [](base::WeakPtr<CookiePortabilityService> self,
                       SessionID tab_id,
                       RefreshSource source,
                       base::TimeTicks refresh_start,
                       std::unique_ptr<RefreshGuard> refresh_guard,
                       RefreshBundle bundle,
                       size_t plan_affected_domains_count,
                       ApplyCallback on_done,
                       TabSessionApplyResult result) {
                      if (!self) {
                        std::move(on_done).Run(std::move(result));
                        return;
                      }
                      if ((result.kind == TabSessionApplyResult::Kind::kSuccess ||
                           result.kind ==
                               TabSessionApplyResult::Kind::kPartialSuccess) &&
                          source.write_back_to_tanya_json) {
                        content::WebContents* contents =
                            FindWebContentsInProfile(self->profile_, tab_id);
                        TanyaProfilePersistState* state =
                            contents
                                ? TanyaProfilePersistState::FromWebContents(contents)
                                : nullptr;
                        if (!state || state->attached_encrypted_path().empty()) {
                          result = MakeFailure(
                              TabSessionApplyResult::Kind::kFailedNoPersistAttachment,
                              "No persist attachment is available for write-back");
                        } else {
                          const std::string encryption_key =
                              GetOrCreateProfileEncryptionKey(self->profile_);
                          std::string error;
                          const std::string cookies_json =
                              SerializeCookieListToJson(bundle.cookies);
                          if (!UpdateEncryptedTanyaProfileAtPath(
                                  state->attached_encrypted_path(), encryption_key,
                                  cookies_json, state->profile_file_proxy_json(),
                                  &error)) {
                            result = MakeFailure(
                                TabSessionApplyResult::Kind::kFailedPersistFlush,
                                error.empty() ? "Failed to flush tanya profile"
                                              : error);
                          }
                        }
                      }

                      base::UmaHistogramEnumeration("PTI.CookieRefresh.ResultKind",
                                                    result.kind);
                      base::UmaHistogramTimes(
                          "PTI.CookieRefresh.Duration",
                          base::TimeTicks::Now() - refresh_start);
                      base::UmaHistogramBoolean("PTI.CookieRefresh.EmptyShortCircuit",
                                                false);
                      CookieAuditLog::RecordOperation(
                          self->profile_,
                          MakeAuditEntry(
                              tab_id, ProfileIdFor(self->profile_),
                              CookieAuditEntry::Source::kRefresh,
                              CookieAuditEntry::Op::kRefresh, result,
                              DigestCookiesForAudit(bundle.cookies),
                              bundle.detail_source + " write_back=" +
                                  std::string(source.write_back_to_tanya_json
                                                  ? "true"
                                                  : "false") +
                                  " affected_domains=" +
                                  base::NumberToString(plan_affected_domains_count)));
                      std::move(on_done).Run(std::move(result));
                    },
                    self, tab_id, source, refresh_start,
                    std::move(refresh_guard), std::move(bundle),
                    plan_affected_domains_count, std::move(on_done)));
          },
          weak_factory_.GetWeakPtr(), tab_id, source, on_progress,
          refresh_start, std::move(refresh_guard),
          std::move(bundle.value()), std::move(on_done)));
}

void CookiePortabilityService::ExportProfileToEncryptedFile(
    ExportRequest req,
    MigrationProgressCallback on_progress,
    ApplyCallback on_done) {
  DCHECK(profile_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!profile_) {
    std::move(on_done).Run(MakeFailure(
        TabSessionApplyResult::Kind::kFailedNoContainerManager,
        "CookiePortabilityService is not attached to a profile"));
    return;
  }
  if (req.output_path.empty()) {

    std::move(on_done).Run(MakeFailure(
        TabSessionApplyResult::Kind::kFailedAllApplyRejected,
        "Migration export path must not be empty"));
    return;
  }

  if (!req.output_path.IsAbsolute()) {
    ;
    std::move(on_done).Run(MakeFailure(
        TabSessionApplyResult::Kind::kFailedAllApplyRejected,
        "Migration export path must be absolute, got: " +
            req.output_path.AsUTF8Unsafe()));
    return;
  }
  {
    base::ScopedAllowBlockingForTesting allow_blocking_for_parent_check;

    if (base::DirectoryExists(req.output_path)) {
      ;
      std::move(on_done).Run(MakeFailure(
          TabSessionApplyResult::Kind::kFailedAllApplyRejected,
          "Migration export path points to an existing directory, not a "
          "file; append a filename like 'export.tanya': " +
              req.output_path.AsUTF8Unsafe()));
      return;
    }
    if (!base::DirectoryExists(req.output_path.DirName())) {
      ;
      std::move(on_done).Run(MakeFailure(
          TabSessionApplyResult::Kind::kFailedAllApplyRejected,
          "Migration export parent directory does not exist: " +
              req.output_path.DirName().AsUTF8Unsafe()));
      return;
    }
  }
  ;
  if (!GetForBrowserContext(profile_)) {
    std::move(on_done).Run(MakeFailure(
        TabSessionApplyResult::Kind::kFailedNoContainerManager,
        "TabContainerManager is unavailable for export"));
    return;
  }
  if (!TabSessionApplierFactory::GetForProfile(profile_)) {
    std::move(on_done).Run(MakeFailure(
        TabSessionApplyResult::Kind::kFailedNoCookieManager,
        "TabSessionApplier is unavailable for export"));
    return;
  }

  const base::TimeTicks export_start = base::TimeTicks::Now();
  const std::string operation_id = base::UnguessableToken::Create().ToString();
  if (on_progress) {
    on_progress.Run(operation_id, 25, "Scanning tanya tabs.");
  }

  std::vector<content::WebContents*> tabs = CollectTanyaTabsForProfile(profile_);
  if (tabs.empty()) {
    TabSessionApplyResult result = MakeFailure(
        TabSessionApplyResult::Kind::kFailedAllApplyRejected,
        "No tanya tabs are available for export");
    base::UmaHistogramEnumeration("PTI.CookieMigration.ExportResultKind",
                                  result.kind);
    base::UmaHistogramTimes("PTI.CookieMigration.ExportDuration",
                            base::TimeTicks::Now() - export_start);
    std::move(on_done).Run(std::move(result));
    return;
  }

  std::vector<SessionID> tab_ids;
  tab_ids.reserve(tabs.size());
  for (content::WebContents* contents : tabs) {
    const SessionID id = sessions::SessionTabHelper::IdForTab(contents);
    if (id.is_valid()) {
      tab_ids.push_back(id);
    }
  }
  if (tab_ids.empty()) {
    TabSessionApplyResult result = MakeFailure(
        TabSessionApplyResult::Kind::kFailedAllApplyRejected,
        "No tanya tabs with valid SessionIDs available for export");
    base::UmaHistogramEnumeration("PTI.CookieMigration.ExportResultKind",
                                  result.kind);
    base::UmaHistogramTimes("PTI.CookieMigration.ExportDuration",
                            base::TimeTicks::Now() - export_start);
    std::move(on_done).Run(std::move(result));
    return;
  }

  struct ExportState {
    profile_exporter::ProfileEnvelope envelope;

    std::vector<SessionID> tabs;
    size_t index = 0;

    ApplyCallback on_done;
  };
  auto state = std::make_shared<ExportState>();
  state->tabs = std::move(tab_ids);
  state->on_done = std::move(on_done);
  state->envelope.meta.schema_version = profile_exporter::kEnvelopeSchemaVersion;
  state->envelope.meta.created_at = base::Time::Now();
  state->envelope.meta.host_fingerprint =
      profile_exporter::ComputeLocalHostFingerprint();

  auto step = std::make_shared<base::RepeatingCallback<void()>>();
  *step = base::BindRepeating(
      [](base::WeakPtr<CookiePortabilityService> self, ExportRequest req,
         base::TimeTicks export_start, std::string operation_id,
         MigrationProgressCallback on_progress,
         std::shared_ptr<ExportState> state,
         std::shared_ptr<base::RepeatingCallback<void()>> step) {
        if (!self) {

          step->Reset();
          std::move(state->on_done)
              .Run(MakeFailure(
                  TabSessionApplyResult::Kind::kFailedTabClosed,
                  "CookiePortabilityService was destroyed during export"));
          return;
        }
        if (state->index >= state->tabs.size()) {
          if (on_progress) {
            on_progress.Run(operation_id, 50, "Serializing migration package.");
          }
          auto encrypted = profile_exporter::EncryptEnvelope(state->envelope);
          if (!encrypted.has_value()) {
            TabSessionApplyResult result = MakeFailure(
                TabSessionApplyResult::Kind::kFailedAllApplyRejected,
                "Migration export serialization failed");
            base::UmaHistogramEnumeration(
                "PTI.CookieMigration.ExportResultKind", result.kind);
            base::UmaHistogramTimes("PTI.CookieMigration.ExportDuration",
                                    base::TimeTicks::Now() - export_start);
            CookieAuditLog::RecordOperation(
                self->profile_,
                MakeAuditEntry(
                    SessionID::InvalidValue(), ProfileIdFor(self->profile_),
                    CookieAuditEntry::Source::kMigration,
                    CookieAuditEntry::Op::kMigrationExport, result, {},
                    "direction=export tab_count=" +
                        base::NumberToString(state->envelope.tabs.size()) +
                        " cookie_total=" +
                        base::NumberToString(
                            profile_exporter::SummarizeEnvelope(state->envelope)
                                .cookie_total) +
                        " schema_version=" +
                        base::NumberToString(profile_exporter::kEnvelopeSchemaVersion) +
                        " error=" + EnvelopeErrorToString(encrypted.error())));
            step->Reset();  
            std::move(state->on_done).Run(std::move(result));
            return;
          }
          if (on_progress) {
            on_progress.Run(operation_id, 90, "Writing migration package.");
          }

          base::ScopedAllowBlockingForTesting allow_blocking_for_export_write;
          ;
          const bool write_ok = base::WriteFile(req.output_path, *encrypted);
          if (!write_ok) {
            ;
          } else {
            const std::optional<int64_t> written_size =
                base::GetFileSize(req.output_path);
            ;
          }
          if (!write_ok) {
            TabSessionApplyResult result = MakeFailure(
                TabSessionApplyResult::Kind::kFailedAllApplyRejected,
                "Migration export file write failed (path: " +
                    req.output_path.AsUTF8Unsafe() + ")");
            base::UmaHistogramEnumeration(
                "PTI.CookieMigration.ExportResultKind", result.kind);
            base::UmaHistogramTimes("PTI.CookieMigration.ExportDuration",
                                    base::TimeTicks::Now() - export_start);
            CookieAuditLog::RecordOperation(
                self->profile_,
                MakeAuditEntry(
                    SessionID::InvalidValue(), ProfileIdFor(self->profile_),
                    CookieAuditEntry::Source::kMigration,
                    CookieAuditEntry::Op::kMigrationExport, result, {},
                    "direction=export tab_count=" +
                        base::NumberToString(state->envelope.tabs.size()) +
                        " cookie_total=" +
                        base::NumberToString(
                            profile_exporter::SummarizeEnvelope(state->envelope)
                                .cookie_total) +
                        " schema_version=" +
                        base::NumberToString(profile_exporter::kEnvelopeSchemaVersion) +
                        " error=kFileIoFailed"));
            step->Reset();  
            std::move(state->on_done).Run(std::move(result));
            return;
          }

          TabSessionApplyResult result;
          result.kind = TabSessionApplyResult::Kind::kSuccess;
          const auto summary = profile_exporter::SummarizeEnvelope(state->envelope);
          base::UmaHistogramEnumeration(
              "PTI.CookieMigration.ExportResultKind", result.kind);
          base::UmaHistogramTimes("PTI.CookieMigration.ExportDuration",
                                  base::TimeTicks::Now() - export_start);
          CookieAuditLog::RecordOperation(
              self->profile_,
              MakeAuditEntry(
                  SessionID::InvalidValue(), ProfileIdFor(self->profile_),
                  CookieAuditEntry::Source::kMigration,
                  CookieAuditEntry::Op::kMigrationExport, result, {},
                  "direction=export tab_count=" +
                      base::NumberToString(summary.tab_count) +
                      " cookie_total=" +
                      base::NumberToString(summary.cookie_total) +
                      " schema_version=" +
                      base::NumberToString(profile_exporter::kEnvelopeSchemaVersion)));
          if (on_progress) {
            on_progress.Run(operation_id, 100, "Migration export ready.");
          }
          step->Reset();  
          std::move(state->on_done).Run(std::move(result));
          return;
        }

        const SessionID tab_id = state->tabs[state->index++];

        content::WebContents* contents =
            FindWebContentsInProfile(self->profile_, tab_id);
        if (!contents) {

          base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
              FROM_HERE, *step);
          return;
        }
        auto record =
            std::make_shared<profile_exporter::ProfileEnvelope::TabRecord>();
        record->original_tab_label = base::StringPrintf("SID %d", tab_id.id());

        record->primary_url = contents->GetLastCommittedURL().spec();

        self->ReadTabCookies(
            tab_id,
            base::BindOnce(
                [](base::WeakPtr<CookiePortabilityService> self,
                   ExportRequest req, std::string operation_id,
                   MigrationProgressCallback on_progress,
                   std::shared_ptr<ExportState> state,
                   std::shared_ptr<profile_exporter::ProfileEnvelope::TabRecord>
                       record,
                   std::shared_ptr<base::RepeatingCallback<void()>> step,
                   net::CookieList cookies) {
                  if (!self) {
                    step->Reset();  
                    std::move(state->on_done)
                        .Run(MakeFailure(
                            TabSessionApplyResult::Kind::kFailedTabClosed,
                            "CookiePortabilityService was destroyed during "
                            "export"));
                    return;
                  }
                  record->cookies.assign(cookies.begin(), cookies.end());
                  state->envelope.tabs.push_back(std::move(*record));
                  if (on_progress) {
                    on_progress.Run(operation_id, 25,
                                    "Reading tab cookie state.");
                  }
                  (*step).Run();
                },
                self, req, operation_id, on_progress, state, record, step));
      },
      weak_factory_.GetWeakPtr(), req, export_start, operation_id, on_progress,
      state, step);

  (*step).Run();
}

void CookiePortabilityService::PreviewImportProfile(
    base::FilePath input_path,
    ImportPreviewCallback on_done) {
  DCHECK(profile_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ImportPreview preview;
  auto envelope = LoadAndDecodeEnvelope(input_path);
  if (!envelope.has_value()) {
    preview.envelope_error = static_cast<int>(envelope.error());
    std::move(on_done).Run(std::move(preview));
    return;
  }

  preview.envelope_schema_version = envelope->meta.schema_version;
  if (auto schema = profile_exporter::ValidateSchemaVersion(envelope->meta);
      !schema.has_value()) {
    preview.envelope_error = static_cast<int>(schema.error());
    std::move(on_done).Run(std::move(preview));
    return;
  }

  preview.envelope_error = static_cast<int>(profile_exporter::EnvelopeError::kOk);
  preview.host_match = static_cast<int>(profile_exporter::CompareHostFingerprint(
      envelope->meta, profile_exporter::ComputeLocalHostFingerprint()));
  preview.created_at = envelope->meta.created_at;
  const auto summary = profile_exporter::SummarizeEnvelope(*envelope);
  preview.tab_count = summary.tab_count;
  preview.cookie_total = summary.cookie_total;
  preview.affected_domains = summary.affected_domains;
  std::move(on_done).Run(std::move(preview));
}

void CookiePortabilityService::ImportProfileFromEncryptedFile(
    ImportRequest req,
    MigrationProgressCallback on_progress,
    ApplyCallback on_done) {
  DCHECK(profile_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!profile_) {
    std::move(on_done).Run(MakeFailure(
        TabSessionApplyResult::Kind::kFailedNoContainerManager,
        "CookiePortabilityService is not attached to a profile"));
    return;
  }
  if (!GetForBrowserContext(profile_)) {
    std::move(on_done).Run(MakeFailure(
        TabSessionApplyResult::Kind::kFailedNoContainerManager,
        "TabContainerManager is unavailable for import"));
    return;
  }
  if (!TabSessionApplierFactory::GetForProfile(profile_)) {
    std::move(on_done).Run(MakeFailure(
        TabSessionApplyResult::Kind::kFailedNoCookieManager,
        "TabSessionApplier is unavailable for import"));
    return;
  }

  const base::TimeTicks import_start = base::TimeTicks::Now();
  const std::string operation_id = base::UnguessableToken::Create().ToString();
  if (on_progress) {
    on_progress.Run(operation_id, 15, "Reading migration package.");
  }
  auto envelope = LoadAndDecodeEnvelope(req.input_path);
  if (!envelope.has_value()) {
    TabSessionApplyResult result = MakeFailure(
        TabSessionApplyResult::Kind::kFailedAllApplyRejected,
        "Failed to decode migration package");
    base::UmaHistogramEnumeration("PTI.CookieMigration.ImportResultKind",
                                  result.kind);
    base::UmaHistogramTimes("PTI.CookieMigration.ImportDuration",
                            base::TimeTicks::Now() - import_start);
    CookieAuditLog::RecordOperation(
        profile_,
        MakeAuditEntry(SessionID::InvalidValue(), ProfileIdFor(profile_),
                       CookieAuditEntry::Source::kMigration,
                       CookieAuditEntry::Op::kMigrationImport, result, {},
                       "direction=import tab_count=0 cookie_total=0 "
                       "schema_version=0 host_match=unknown error=" +
                           EnvelopeErrorToString(envelope.error())));
    std::move(on_done).Run(std::move(result));
    return;
  }
  if (auto schema = profile_exporter::ValidateSchemaVersion(envelope->meta);
      !schema.has_value()) {
    TabSessionApplyResult result = MakeFailure(
        TabSessionApplyResult::Kind::kFailedAllApplyRejected,
        "schema_mismatch=expected_" +
            base::NumberToString(profile_exporter::kEnvelopeSchemaVersion) +
            "_got_" + base::NumberToString(envelope->meta.schema_version));
    base::UmaHistogramEnumeration("PTI.CookieMigration.ImportResultKind",
                                  result.kind);
    base::UmaHistogramTimes("PTI.CookieMigration.ImportDuration",
                            base::TimeTicks::Now() - import_start);
    CookieAuditLog::RecordOperation(
        profile_,
        MakeAuditEntry(SessionID::InvalidValue(), ProfileIdFor(profile_),
                       CookieAuditEntry::Source::kMigration,
                       CookieAuditEntry::Op::kMigrationImport, result, {},
                       "direction=import tab_count=0 cookie_total=0 schema_version=" +
                           base::NumberToString(envelope->meta.schema_version) +
                           " host_match=unknown error=" +
                           EnvelopeErrorToString(schema.error())));
    std::move(on_done).Run(std::move(result));
    return;
  }

  const auto host_match = profile_exporter::CompareHostFingerprint(
      envelope->meta, profile_exporter::ComputeLocalHostFingerprint());
  if (on_progress) {
    on_progress.Run(operation_id, 30, "Planning target tabs.");
  }
  std::vector<ImportPlanEntry> plan = PlanImportPerTab(*envelope);
  if (plan.empty()) {
    TabSessionApplyResult result = MakeFailure(
        TabSessionApplyResult::Kind::kFailedAllApplyRejected,
        "Migration import plan is empty");
    const auto summary = profile_exporter::SummarizeEnvelope(*envelope);
    base::UmaHistogramEnumeration("PTI.CookieMigration.ImportResultKind",
                                  result.kind);
    base::UmaHistogramTimes("PTI.CookieMigration.ImportDuration",
                            base::TimeTicks::Now() - import_start);
    CookieAuditLog::RecordOperation(
        profile_,
        MakeAuditEntry(SessionID::InvalidValue(), ProfileIdFor(profile_),
                       CookieAuditEntry::Source::kMigration,
                       CookieAuditEntry::Op::kMigrationImport, result, {},
                       "direction=import tab_count=" +
                           base::NumberToString(summary.tab_count) +
                           " cookie_total=" +
                           base::NumberToString(summary.cookie_total) +
                           " schema_version=" +
                           base::NumberToString(envelope->meta.schema_version) +
                           " host_match=" + HostMatchToDetail(host_match) +
                           " error=kFailedAllApplyRejected"));
    std::move(on_done).Run(std::move(result));
    return;
  }

  WriteOptions options;
  options.scope = WriteScope::kPartialOverride;
  options.persist = req.persist;
  if (on_progress) {
    on_progress.Run(operation_id, 50, "Applying cookies to target tabs.");
  }
  ExecuteImportPlan(
      std::move(plan), options, req.input_path, on_progress, operation_id,
      base::BindOnce(
          [](base::WeakPtr<CookiePortabilityService> self,
             profile_exporter::DecodedSummary summary,
             profile_exporter::HostFingerprintMatch host_match,
             int schema_version, base::TimeTicks import_start,
             ApplyCallback on_done,
             TabSessionApplyResult result) {
            if (!self) {
              std::move(on_done).Run(std::move(result));
              return;
            }
            base::UmaHistogramEnumeration(
                "PTI.CookieMigration.ImportResultKind", result.kind);
            base::UmaHistogramEnumeration(
                "PTI.CookieMigration.HostFingerprintMatch", host_match);
            base::UmaHistogramTimes("PTI.CookieMigration.ImportDuration",
                                    base::TimeTicks::Now() - import_start);
            CookieAuditLog::RecordOperation(
                self->profile_,
                MakeAuditEntry(SessionID::InvalidValue(),
                               ProfileIdFor(self->profile_),
                               CookieAuditEntry::Source::kMigration,
                               CookieAuditEntry::Op::kMigrationImport, result,
                               {},
                               "direction=import tab_count=" +
                                   base::NumberToString(summary.tab_count) +
                                   " cookie_total=" +
                                   base::NumberToString(summary.cookie_total) +
                                   " schema_version=" +
                                   base::NumberToString(schema_version) +
                                   " host_match=" +
                                   HostMatchToDetail(host_match) +
                                   " error=" + ApplyKindToString(result.kind)));
            std::move(on_done).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(),
          profile_exporter::SummarizeEnvelope(*envelope), host_match,
          envelope->meta.schema_version, import_start, std::move(on_done)));
}

void CookiePortabilityService::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
}

void CookiePortabilityService::SetSingleCookieImpl(SessionID tab_id,
                                                   net::CanonicalCookie cookie,
                                                   WriteOptions options,
                                                   ApplyCallback on_done) {
  TabContainerManager* manager = GetExistingForBrowserContext(profile_);
  if (!manager) {
    TabSessionApplyResult result = MakeFailure(
        TabSessionApplyResult::Kind::kFailedNoContainerManager,
        "TabContainerManager is not attached to this profile");
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kWrite, result,
                                 {DigestCookieForAudit(cookie)},
                                 "SetSingleCookie preflight"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(result));
    return;
  }
  TabSessionApplier* applier = TabSessionApplierFactory::GetForProfile(profile_);
  if (!applier) {
    TabSessionApplyResult result =
        MakeFailure(TabSessionApplyResult::Kind::kFailedNoCookieManager,
                    "TabSessionApplier is unavailable for this profile");
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kWrite, result,
                                 {DigestCookieForAudit(cookie)},
                                 "SetSingleCookie no applier"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(result));
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateTargetTab(tab_id)) {
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kWrite, *guard,
                                 {DigestCookieForAudit(cookie)},
                                 "SetSingleCookie invalid target"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(*guard));
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateOwningProfile(tab_id)) {
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kWrite, *guard,
                                 {DigestCookieForAudit(cookie)},
                                 "SetSingleCookie owning profile mismatch"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(*guard));
    return;
  }

  std::vector<net::CanonicalCookie> cookies;
  cookies.push_back(std::move(cookie));
  std::vector<std::string> digests = DigestCookiesForAudit(cookies);
  applier->ApplyCookiesToTab(
      tab_id, std::move(cookies), std::move(options),
      base::BindOnce(
          [](base::WeakPtr<CookiePortabilityService> self, SessionID tab_id,
             std::string profile_id, std::vector<std::string> digests,
             ApplyCallback on_done, TabSessionApplyResult result) {
            if (self && self->profile_) {
              CookieAuditLog::RecordOperation(
                  self->profile_,
                  MakeAuditEntry(tab_id, std::move(profile_id),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kWrite, result,
                                 std::move(digests), "SetSingleCookie"));
              self->FinishTabWrite(tab_id);
            }
            std::move(on_done).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), tab_id, ProfileIdFor(profile_),
          std::move(digests), std::move(on_done)));
}

void CookiePortabilityService::ApplyCookiesFromExternalSourceImpl(
    SessionID tab_id,
    std::vector<net::CanonicalCookie> cookies,
    WriteOptions options,
    ApplyCallback on_done) {
  TabContainerManager* manager = GetExistingForBrowserContext(profile_);
  if (!manager) {
    TabSessionApplyResult result = MakeFailure(
        TabSessionApplyResult::Kind::kFailedNoContainerManager,
        "TabContainerManager is not attached to this profile");
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kImport, result,
                                 DigestCookiesForAudit(cookies),
                                 "ApplyCookiesFromExternalSource preflight"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(result));
    return;
  }
  TabSessionApplier* applier = TabSessionApplierFactory::GetForProfile(profile_);
  if (!applier) {
    TabSessionApplyResult result =
        MakeFailure(TabSessionApplyResult::Kind::kFailedNoCookieManager,
                    "TabSessionApplier is unavailable for this profile");
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kImport, result,
                                 DigestCookiesForAudit(cookies),
                                 "ApplyCookiesFromExternalSource no applier"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(result));
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateTargetTab(tab_id)) {
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kImport, *guard,
                                 DigestCookiesForAudit(cookies),
                                 "ApplyCookiesFromExternalSource invalid target"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(*guard));
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateOwningProfile(tab_id)) {
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(
                      tab_id, ProfileIdFor(profile_),
                      CookieAuditEntry::Source::kManualCrud,
                      CookieAuditEntry::Op::kImport, *guard,
                      DigestCookiesForAudit(cookies),
                      "ApplyCookiesFromExternalSource owning profile mismatch"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(*guard));
    return;
  }

  std::vector<std::string> digests = DigestCookiesForAudit(cookies);
  applier->ApplyCookiesToTab(
      tab_id, std::move(cookies), std::move(options),
      base::BindOnce(
          [](base::WeakPtr<CookiePortabilityService> self, SessionID tab_id,
             std::string profile_id, std::vector<std::string> digests,
             ApplyCallback on_done, TabSessionApplyResult result) {
            if (self && self->profile_) {
              CookieAuditLog::RecordOperation(
                  self->profile_,
                  MakeAuditEntry(tab_id, std::move(profile_id),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kImport, result,
                                 std::move(digests),
                                 "ApplyCookiesFromExternalSource"));
              self->FinishTabWrite(tab_id);
            }
            std::move(on_done).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), tab_id, ProfileIdFor(profile_),
          std::move(digests), std::move(on_done)));
}

void CookiePortabilityService::ReplaceDomainCookiesImpl(
    SessionID tab_id,
    const std::string& domain,
    std::vector<net::CanonicalCookie> cookies,
    PersistSemantic persist,
    ApplyCallback on_done) {
  TabContainerManager* manager = GetExistingForBrowserContext(profile_);
  if (!manager) {
    TabSessionApplyResult result = MakeFailure(
        TabSessionApplyResult::Kind::kFailedNoContainerManager,
        "TabContainerManager is not attached to this profile");
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kReplace, result,
                                 DigestCookiesForAudit(cookies),
                                 "ReplaceDomainCookies preflight"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(result));
    return;
  }
  TabSessionApplier* applier = TabSessionApplierFactory::GetForProfile(profile_);
  if (!applier) {
    TabSessionApplyResult result =
        MakeFailure(TabSessionApplyResult::Kind::kFailedNoCookieManager,
                    "TabSessionApplier is unavailable for this profile");
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kReplace, result,
                                 DigestCookiesForAudit(cookies),
                                 "ReplaceDomainCookies no applier"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(result));
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateTargetTab(tab_id)) {
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kReplace, *guard,
                                 DigestCookiesForAudit(cookies),
                                 "ReplaceDomainCookies invalid target"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(*guard));
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateOwningProfile(tab_id)) {
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kReplace, *guard,
                                 DigestCookiesForAudit(cookies),
                                 "ReplaceDomainCookies owning profile mismatch"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(*guard));
    return;
  }

  std::vector<std::string> digests = DigestCookiesForAudit(cookies);
  applier->ReplaceCookiesForDomain(
      tab_id, domain, std::move(cookies), persist,
      base::BindOnce(
          [](base::WeakPtr<CookiePortabilityService> self, SessionID tab_id,
             std::string profile_id, std::string domain,
             std::vector<std::string> digests, ApplyCallback on_done,
             TabSessionApplyResult result) {
            if (self && self->profile_) {
              CookieAuditLog::RecordOperation(
                  self->profile_,
                  MakeAuditEntry(tab_id, std::move(profile_id),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kReplace, result,
                                 std::move(digests),
                                 "ReplaceDomainCookies:" + domain));
              self->FinishTabWrite(tab_id);
            }
            std::move(on_done).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), tab_id, ProfileIdFor(profile_), domain,
          std::move(digests), std::move(on_done)));
}

void CookiePortabilityService::DeleteDomainCookiesImpl(SessionID tab_id,
                                                       const std::string& domain,
                                                       ApplyCallback on_done) {
  TabContainerManager* manager = GetExistingForBrowserContext(profile_);
  if (!manager) {
    TabSessionApplyResult result = MakeFailure(
        TabSessionApplyResult::Kind::kFailedNoContainerManager,
        "TabContainerManager is not attached to this profile");
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kDelete, result,
                                 {"domain:" + domain},
                                 "DeleteDomainCookies preflight"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(result));
    return;
  }
  TabSessionApplier* applier = TabSessionApplierFactory::GetForProfile(profile_);
  if (!applier) {
    TabSessionApplyResult result =
        MakeFailure(TabSessionApplyResult::Kind::kFailedNoCookieManager,
                    "TabSessionApplier is unavailable for this profile");
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kDelete, result,
                                 {"domain:" + domain},
                                 "DeleteDomainCookies no applier"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(result));
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateTargetTab(tab_id)) {
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kDelete, *guard,
                                 {"domain:" + domain},
                                 "DeleteDomainCookies invalid target"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(*guard));
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateOwningProfile(tab_id)) {
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kDelete, *guard,
                                 {"domain:" + domain},
                                 "DeleteDomainCookies owning profile mismatch"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(*guard));
    return;
  }

  applier->DeleteCookiesForDomain(
      tab_id, domain,
      base::BindOnce(
          [](base::WeakPtr<CookiePortabilityService> self, SessionID tab_id,
             std::string profile_id, std::string domain, ApplyCallback on_done,
             TabSessionApplyResult result) {
            if (self && self->profile_) {
              std::vector<std::string> digests;
              digests.push_back("domain:" + domain);
              CookieAuditLog::RecordOperation(
                  self->profile_,
                  MakeAuditEntry(tab_id, std::move(profile_id),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kDelete, result,
                                 std::move(digests),
                                 "DeleteDomainCookies:" + domain));
              self->FinishTabWrite(tab_id);
            }
            std::move(on_done).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), tab_id, ProfileIdFor(profile_), domain,
          std::move(on_done)));
}

void CookiePortabilityService::DeleteCookiesImpl(
    SessionID tab_id,
    std::vector<net::CanonicalCookie> cookies,
    ApplyCallback on_done) {
  TabContainerManager* manager = GetExistingForBrowserContext(profile_);
  if (!manager) {
    TabSessionApplyResult result = MakeFailure(
        TabSessionApplyResult::Kind::kFailedNoContainerManager,
        "TabContainerManager is not attached to this profile");
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kDelete, result,
                                 DigestCookiesForAudit(cookies),
                                 "DeleteCookies preflight"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(result));
    return;
  }
  TabSessionApplier* applier = TabSessionApplierFactory::GetForProfile(profile_);
  if (!applier) {
    TabSessionApplyResult result =
        MakeFailure(TabSessionApplyResult::Kind::kFailedNoCookieManager,
                    "TabSessionApplier is unavailable for this profile");
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kDelete, result,
                                 DigestCookiesForAudit(cookies),
                                 "DeleteCookies no applier"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(result));
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateTargetTab(tab_id)) {
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kDelete, *guard,
                                 DigestCookiesForAudit(cookies),
                                 "DeleteCookies invalid target"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(*guard));
    return;
  }
  if (std::optional<TabSessionApplyResult> guard = ValidateOwningProfile(tab_id)) {
    CookieAuditLog::RecordOperation(
        profile_, MakeAuditEntry(tab_id, ProfileIdFor(profile_),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kDelete, *guard,
                                 DigestCookiesForAudit(cookies),
                                 "DeleteCookies owning profile mismatch"));
    FinishTabWrite(tab_id);
    std::move(on_done).Run(std::move(*guard));
    return;
  }

  std::vector<std::string> digests = DigestCookiesForAudit(cookies);
  applier->DeleteCookiesForTab(
      tab_id, std::move(cookies),
      base::BindOnce(
          [](base::WeakPtr<CookiePortabilityService> self, SessionID tab_id,
             std::string profile_id, std::vector<std::string> digests,
             ApplyCallback on_done, TabSessionApplyResult result) {
            if (self && self->profile_) {
              CookieAuditLog::RecordOperation(
                  self->profile_,
                  MakeAuditEntry(tab_id, std::move(profile_id),
                                 CookieAuditEntry::Source::kManualCrud,
                                 CookieAuditEntry::Op::kDelete, result,
                                 std::move(digests), "DeleteCookies"));
              self->FinishTabWrite(tab_id);
            }
            std::move(on_done).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), tab_id, ProfileIdFor(profile_),
          std::move(digests), std::move(on_done)));
}

base::expected<CookiePortabilityService::RefreshBundle, TabSessionApplyResult>
CookiePortabilityService::ResolveRefreshSource(SessionID tab_id,
                                               const RefreshSource& source) const {

  base::ScopedAllowBlockingForTesting allow_blocking_for_refresh_source_read;
  RefreshBundle bundle;
  if (source.kind == RefreshSourceKind::kPersistedTanyaJson) {
    content::WebContents* contents = FindWebContentsInProfile(profile_, tab_id);
    TanyaProfilePersistState* state =
        contents ? TanyaProfilePersistState::FromWebContents(contents) : nullptr;
    if (!state || state->attached_encrypted_path().empty()) {
      return base::unexpected(MakeFailure(
          TabSessionApplyResult::Kind::kFailedNoPersistAttachment,
          "No attached persisted tanya JSON exists for this tab"));
    }

    const std::string encryption_key = GetOrCreateProfileEncryptionKey(profile_);
    PersistedProfile persisted;
    std::string error;
    if (!ReadPersistedProfileFromEncryptedFile(state->attached_encrypted_path(),
                                               encryption_key, &persisted,
                                               &error)) {
      return base::unexpected(MakeFailure(
          TabSessionApplyResult::Kind::kFailedAllApplyRejected,
          error.empty() ? "Failed to read persisted tanya JSON" : error));
    }

    std::optional<base::Value> parsed =
        base::JSONReader::Read(persisted.cookies_json, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_list()) {
      return base::unexpected(MakeFailure(
          TabSessionApplyResult::Kind::kFailedAllApplyRejected,
          "Persisted tanya JSON cookies field is not a list"));
    }
    cookie_portability_codec::DecodeResult decoded =
        cookie_portability_codec::DecodeCookieList(parsed->GetList());
    if (decoded.error != cookie_portability_codec::DecodeError::kOk &&
        decoded.cookies.empty()) {
      return base::unexpected(MakeFailure(
          TabSessionApplyResult::Kind::kFailedAllApplyRejected,
          "Persisted tanya JSON failed cookie decode"));
    }
    bundle.cookies = std::move(decoded.cookies);
    bundle.detail_source = "source=persisted";
    bundle.source_label = state->attached_encrypted_path().AsUTF8Unsafe();
    return bundle;
  }

  if (source.file_path.empty() || !base::PathExists(source.file_path)) {
    return base::unexpected(MakeFailure(
        TabSessionApplyResult::Kind::kFailedAllApplyRejected,
        "Refresh source file does not exist"));
  }
  std::string file_contents;
  if (!base::ReadFileToString(source.file_path, &file_contents)) {
    return base::unexpected(MakeFailure(
        TabSessionApplyResult::Kind::kFailedAllApplyRejected,
        "Failed to read refresh source file"));
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(file_contents, base::JSON_PARSE_RFC);
  if (!parsed) {
    return base::unexpected(MakeFailure(
        TabSessionApplyResult::Kind::kFailedAllApplyRejected,
        "Refresh source file is not valid JSON"));
  }

  cookie_portability_codec::DecodeResult decoded;
  if (parsed->is_list()) {
    decoded = cookie_portability_codec::DecodeCookieList(parsed->GetList());
  } else if (parsed->is_dict()) {
    const base::ListValue* cookies = parsed->GetDict().FindList("cookies");
    if (!cookies) {
      return base::unexpected(MakeFailure(
          TabSessionApplyResult::Kind::kFailedAllApplyRejected,
          "Refresh source file does not contain a cookies list"));
    }
    decoded = cookie_portability_codec::DecodeCookieList(*cookies);
  } else {
    return base::unexpected(MakeFailure(
        TabSessionApplyResult::Kind::kFailedAllApplyRejected,
        "Refresh source JSON must be a list or dict"));
  }
  if (decoded.error != cookie_portability_codec::DecodeError::kOk &&
      decoded.cookies.empty()) {
    return base::unexpected(MakeFailure(
        TabSessionApplyResult::Kind::kFailedAllApplyRejected,
        "Refresh source file rejected all cookies"));
  }

  bundle.cookies = std::move(decoded.cookies);
  bundle.detail_source = "source=file";
  bundle.source_label = source.file_path.AsUTF8Unsafe();
  return bundle;
}

CookiePortabilityService::RefreshReplacePlan
CookiePortabilityService::BuildReplacePlan(
    const RefreshBundle& bundle,
    const net::CookieList& current_cookies) const {
  RefreshReplacePlan plan;
  for (const net::CanonicalCookie& cookie : bundle.cookies) {
    if (cookie.Domain().empty()) {
      continue;
    }
    if (!plan.cookies_by_domain.contains(cookie.Domain())) {
      plan.affected_domains.push_back(cookie.Domain());
    }
    plan.cookies_by_domain[cookie.Domain()].push_back(cookie);
    ++plan.cookies_to_write;
  }

  for (const net::CanonicalCookie& cookie : current_cookies) {
    if (plan.cookies_by_domain.contains(cookie.Domain())) {
      ++plan.cookies_deleted_first;
    }
  }

  plan.short_circuit_empty =
      bundle.cookies.empty() || plan.affected_domains.empty();
  return plan;
}

void CookiePortabilityService::ExecuteReplacePlan(SessionID tab_id,
                                                  const RefreshReplacePlan& plan,
                                                  PersistSemantic persist,
                                                  ApplyCallback on_done) {

  std::shared_ptr<ReplaceExecutionState> state =
      std::make_shared<ReplaceExecutionState>();
  state->cookies_by_domain = plan.cookies_by_domain;
  state->domains = plan.affected_domains;
  state->persist = persist;
  state->aggregate.kind = TabSessionApplyResult::Kind::kSuccess;
  state->aggregate.cookies_deleted_first = 0;
  state->aggregate.cookies_attempted = 0;
  state->aggregate.cookies_applied = 0;
  state->on_done = std::move(on_done);
  state->profile = profile_;
  RunReplacePlanStep(weak_factory_.GetWeakPtr(), tab_id, std::move(state));
}

base::expected<profile_exporter::ProfileEnvelope,
               profile_exporter::EnvelopeError>
CookiePortabilityService::LoadAndDecodeEnvelope(const base::FilePath& path) {

  base::ScopedAllowBlockingForTesting allow_blocking_for_envelope_read;
  if (path.empty() || !base::PathExists(path)) {
    return base::unexpected(profile_exporter::EnvelopeError::kFileIoFailed);
  }
  std::optional<std::vector<uint8_t>> bytes = base::ReadFileToBytes(path);
  if (!bytes.has_value()) {
    return base::unexpected(profile_exporter::EnvelopeError::kFileIoFailed);
  }
  return profile_exporter::DecryptEnvelope(*bytes);
}

std::vector<CookiePortabilityService::ImportPlanEntry>
CookiePortabilityService::PlanImportPerTab(
    const profile_exporter::ProfileEnvelope& env) {

  std::vector<ImportPlanEntry> plan;
  plan.reserve(env.tabs.size());
  for (const auto& tab : env.tabs) {
    ImportPlanEntry entry;
    entry.tab_id = SessionID::InvalidValue();
    entry.original_tab_label = tab.original_tab_label;
    entry.primary_url = GURL(tab.primary_url);
    entry.cookies = tab.cookies;
    plan.push_back(std::move(entry));
  }
  return plan;
}

void CookiePortabilityService::ExecuteImportPlan(
    std::vector<ImportPlanEntry> plan,
    WriteOptions options,
    const base::FilePath& attachment_path,
    MigrationProgressCallback on_progress,
    const std::string& operation_id,
    ApplyCallback on_done) {
  struct ImportExecutionState {
    std::vector<ImportPlanEntry> plan;
    size_t index = 0;
    WriteOptions options;
    TabSessionApplyResult aggregate;
    size_t success_count = 0;
    size_t fail_count = 0;
    size_t non_all_apply_fail_count = 0;
    bool saw_delete_phase = false;
    bool saw_no_persist = false;
    std::vector<std::string> detail_rows;

    ApplyCallback on_done;
  };

  auto state = std::make_shared<ImportExecutionState>();
  state->plan = std::move(plan);
  state->options = options;
  state->aggregate.kind = TabSessionApplyResult::Kind::kSuccess;
  state->on_done = std::move(on_done);

  auto step = std::make_shared<base::RepeatingCallback<void()>>();
  *step = base::BindRepeating(
      [](base::WeakPtr<CookiePortabilityService> self,
         const base::FilePath& attachment_path,
         MigrationProgressCallback on_progress, std::string operation_id,
         std::shared_ptr<ImportExecutionState> state,
         std::shared_ptr<base::RepeatingCallback<void()>> step) {
        if (!self) {

          step->Reset();
          std::move(state->on_done)
              .Run(MakeFailure(
                  TabSessionApplyResult::Kind::kFailedTabClosed,
                  "CookiePortabilityService was destroyed during import"));
          return;
        }
        if (state->index >= state->plan.size()) {
          if (state->success_count == state->plan.size()) {
            state->aggregate.kind = TabSessionApplyResult::Kind::kSuccess;
          } else if (state->saw_delete_phase &&
                     state->non_all_apply_fail_count > 0) {
            state->aggregate.kind =
                TabSessionApplyResult::Kind::kFailedDeletePhase;
          } else if (state->saw_no_persist &&
                     state->non_all_apply_fail_count > 0) {
            state->aggregate.kind =
                TabSessionApplyResult::Kind::kFailedNoPersistAttachment;
          } else if (state->success_count > 0 ||
                     state->non_all_apply_fail_count > 0 ||
                     state->fail_count > 0) {
            state->aggregate.kind =
                (state->fail_count == state->plan.size() &&
                 state->non_all_apply_fail_count == 0)
                    ? TabSessionApplyResult::Kind::kFailedAllApplyRejected
                    : TabSessionApplyResult::Kind::kPartialSuccess;
          }
          if (!state->detail_rows.empty()) {
            state->aggregate.error_detail =
                base::JoinString(state->detail_rows, "; ");
          }
          if (on_progress) {
            on_progress.Run(operation_id, 90, "Finalizing migration import.");
            on_progress.Run(operation_id, 100, "Migration import completed.");
          }
          step->Reset();  
          std::move(state->on_done).Run(std::move(state->aggregate));
          return;
        }

        ImportPlanEntry entry = std::move(state->plan[state->index]);
        const size_t current_index = state->index++;

        NavigateParams open_params(self->profile_, GURL(url::kAboutBlankURL),
                                   ui::PAGE_TRANSITION_TYPED);
        open_params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
        Navigate(&open_params);
        content::WebContents* new_contents =
            open_params.navigated_or_inserted_contents.get();
        if (!new_contents) {
          ++state->fail_count;
          ++state->non_all_apply_fail_count;
          state->detail_rows.push_back(entry.original_tab_label +
                                        "=kFailedTabNotFound");
          (*step).Run();
          return;
        }
        entry.tab_id = sessions::SessionTabHelper::IdForTab(new_contents);

        if (state->options.persist == PersistSemantic::kPersistent) {

          TanyaProfilePersistState::RegisterAttachedEncryptedProfile(
              new_contents, attachment_path, "",
              entry.original_tab_label);
        }

        const GURL navigate_target = entry.primary_url;
        const SessionID new_tab_id = entry.tab_id;
        self->ApplyCookiesFromExternalSource(
            entry.tab_id, std::move(entry.cookies), state->options,
            base::BindOnce(
                [](base::WeakPtr<CookiePortabilityService> self,
                   MigrationProgressCallback on_progress,
                   std::string operation_id,
                   std::shared_ptr<ImportExecutionState> state,
                   std::shared_ptr<base::RepeatingCallback<void()>> step,
                   std::string original_tab_label, size_t current_index,
                   GURL navigate_target, SessionID new_tab_id,
                   TabSessionApplyResult result) {
                  if (!self) {
                    step->Reset();  
                    std::move(state->on_done).Run(std::move(result));
                    return;
                  }
                  state->aggregate.cookies_attempted += result.cookies_attempted;
                  state->aggregate.cookies_applied += result.cookies_applied;
                  state->aggregate.cookies_deleted_first +=
                      result.cookies_deleted_first;
                  state->aggregate.skipped.insert(
                      state->aggregate.skipped.end(), result.skipped.begin(),
                      result.skipped.end());
                  if (result.kind == TabSessionApplyResult::Kind::kSuccess) {
                    ++state->success_count;
                  } else {
                    ++state->fail_count;
                    if (result.kind !=
                        TabSessionApplyResult::Kind::kFailedAllApplyRejected) {
                      ++state->non_all_apply_fail_count;
                    }
                    state->saw_delete_phase |=
                        result.kind ==
                        TabSessionApplyResult::Kind::kFailedDeletePhase;
                    state->saw_no_persist |=
                        result.kind ==
                        TabSessionApplyResult::Kind::kFailedNoPersistAttachment;
                    state->detail_rows.push_back(
                        original_tab_label + "=" + ApplyKindToString(result.kind));
                  }

                  if (navigate_target.is_valid() &&
                      !navigate_target.is_empty()) {
                    if (auto* contents = FindWebContentsInProfile(
                            self->profile_, new_tab_id)) {
                      content::NavigationController::LoadURLParams load(
                          navigate_target);
                      load.transition_type = ui::PAGE_TRANSITION_TYPED;
                      contents->GetController().LoadURLWithParams(load);
                    }
                  }
                  if (on_progress) {
                    const int32_t percent = 50 +
                        static_cast<int32_t>((35.0 * (current_index + 1)) /
                                             std::max<size_t>(1, state->plan.size()));
                    on_progress.Run(operation_id, percent,
                                    "Applying cookies to target tabs.");
                  }
                  (*step).Run();
                },
                self, on_progress, operation_id, state, step,
                entry.original_tab_label, current_index, navigate_target,
                new_tab_id));
      },
      weak_factory_.GetWeakPtr(), attachment_path, on_progress, operation_id,
      state, step);

  (*step).Run();
}

bool CookiePortabilityService::EnqueueOrRunTabWrite(SessionID tab_id,
                                                    base::OnceClosure task) {
  const int32_t serialized = tab_id.id();
  if (refreshing_tabs_.contains(serialized) ||
      active_tab_writes_.contains(serialized)) {
    queued_tab_writes_[serialized].push_back(std::move(task));
    return true;
  }

  active_tab_writes_.insert(serialized);
  std::move(task).Run();
  return false;
}

void CookiePortabilityService::FinishTabWrite(SessionID tab_id) {
  active_tab_writes_.erase(tab_id.id());
  MaybeRunQueuedTabWrite(tab_id);
}

void CookiePortabilityService::MaybeRunQueuedTabWrite(SessionID tab_id) {
  const int32_t serialized = tab_id.id();
  if (refreshing_tabs_.contains(serialized) ||
      active_tab_writes_.contains(serialized)) {
    return;
  }
  auto it = queued_tab_writes_.find(serialized);
  if (it == queued_tab_writes_.end() || it->second.empty()) {
    return;
  }

  base::OnceClosure task = std::move(it->second.front());
  it->second.pop_front();
  if (it->second.empty()) {
    queued_tab_writes_.erase(it);
  }
  active_tab_writes_.insert(serialized);
  std::move(task).Run();
}

std::optional<TabSessionApplyResult>
CookiePortabilityService::ValidateTargetTab(SessionID tab_id) const {
  if (!tab_id.is_valid() || tab_id.id() <= 0) {
    return MakeFailure(TabSessionApplyResult::Kind::kFailedInvalidSessionId,
                       "SessionID is invalid");
  }
  return std::nullopt;
}

std::optional<TabSessionApplyResult>
CookiePortabilityService::ValidateOwningProfile(SessionID tab_id) const {
  Profile* owning_profile = nullptr;
  content::WebContents* contents =
      FindWebContentsAcrossProfiles(tab_id, &owning_profile);
  if (!contents || !owning_profile) {
    return MakeFailure(TabSessionApplyResult::Kind::kFailedTabNotFound,
                       "No tab found for the given SessionID");
  }
  if (owning_profile != profile_) {
    return MakeFailure(TabSessionApplyResult::Kind::kFailedTabNotFound,
                       "Tab belongs to a different profile");
  }
  if (!FindWebContentsInProfile(profile_.get(), tab_id)) {
    return MakeFailure(TabSessionApplyResult::Kind::kFailedTabNotFound,
                       "Tab is not reachable from the current profile");
  }
  return std::nullopt;
}

}  
