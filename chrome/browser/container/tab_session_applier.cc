
#include "chrome/browser/container/tab_session_applier.h"

#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/container/tanya_profile_persist_state.h"
#include "chrome/browser/container/profile_load_apply.h"
#include "chrome/browser/container/profile_encryption_key_provider.h"
#include "chrome/browser/container/tab_container_manager.h"
#include "chrome/browser/container/tab_container_manager_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tab_contents/tab_contents_iterator.h"
#include "components/tabs/public/tab_interface.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"

namespace tab_container {

namespace {

template <typename Callback, typename... Args>
void PostToCurrentSequence(Callback callback, Args&&... args) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(callback), std::forward<Args>(args)...));
}

content::WebContents* FindWebContentsBySessionId(Profile* profile,
                                                 SessionID id) {
  if (!profile || !id.is_valid() || id.id() <= 0) {
    return nullptr;
  }

  content::WebContents* result = nullptr;
  tabs::ForEachTabInterface([&](tabs::TabInterface* tab) {
    content::WebContents* contents = tab->GetContents();
    if (!contents) {
      return true;
    }
    if (Profile::FromBrowserContext(contents->GetBrowserContext()) != profile) {
      return true;
    }
    if (sessions::SessionTabHelper::IdForTab(contents) == id) {
      result = contents;
      return false;
    }
    return true;
  });
  return result;
}

TabSessionApplyResult MakeFailure(TabSessionApplyResult::Kind kind,
                                  std::optional<std::string> detail =
                                      std::nullopt) {
  TabSessionApplyResult result;
  result.kind = kind;
  result.error_detail = std::move(detail);
  return result;
}

}  

WriteOptions::WriteOptions() = default;
WriteOptions::WriteOptions(const WriteOptions&) = default;
WriteOptions::WriteOptions(WriteOptions&&) = default;
WriteOptions& WriteOptions::operator=(const WriteOptions&) = default;
WriteOptions& WriteOptions::operator=(WriteOptions&&) = default;
WriteOptions::~WriteOptions() = default;

TabSessionApplyResult::TabSessionApplyResult() = default;
TabSessionApplyResult::TabSessionApplyResult(
    const TabSessionApplyResult&) = default;
TabSessionApplyResult::TabSessionApplyResult(TabSessionApplyResult&&) =
    default;
TabSessionApplyResult& TabSessionApplyResult::operator=(
    const TabSessionApplyResult&) = default;
TabSessionApplyResult& TabSessionApplyResult::operator=(
    TabSessionApplyResult&&) = default;
TabSessionApplyResult::~TabSessionApplyResult() = default;

TabSessionApplier::ResolvedTabContext::ResolvedTabContext() = default;
TabSessionApplier::ResolvedTabContext::ResolvedTabContext(
    const ResolvedTabContext&) = default;
TabSessionApplier::ResolvedTabContext::ResolvedTabContext(
    ResolvedTabContext&&) = default;
TabSessionApplier::ResolvedTabContext&
TabSessionApplier::ResolvedTabContext::operator=(
    const ResolvedTabContext&) = default;
TabSessionApplier::ResolvedTabContext&
TabSessionApplier::ResolvedTabContext::operator=(ResolvedTabContext&&) =
    default;
TabSessionApplier::ResolvedTabContext::~ResolvedTabContext() = default;

TabSessionApplier::TabSessionApplier(Profile* profile) : profile_(profile) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

TabSessionApplier::~TabSessionApplier() = default;

void TabSessionApplier::ApplyCookiesToTab(
    SessionID tab_id,
    std::vector<net::CanonicalCookie> cookies,
    WriteOptions options,
    ApplyCallback on_done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StartApply(tab_id, std::move(cookies), std::move(options), std::move(on_done));
}

void TabSessionApplier::ReplaceCookiesForDomain(
    SessionID tab_id,
    const std::string& domain,
    std::vector<net::CanonicalCookie> cookies,
    PersistSemantic persist,
    ApplyCallback on_done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StartReplace(tab_id, domain, std::move(cookies), persist, std::move(on_done));
}

void TabSessionApplier::DeleteCookiesForDomain(SessionID tab_id,
                                               const std::string& domain,
                                               ApplyCallback on_done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StartDelete(tab_id, domain, std::move(on_done));
}

void TabSessionApplier::DeleteCookiesForTab(
    SessionID tab_id,
    std::vector<net::CanonicalCookie> cookies,
    ApplyCallback on_done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StartDeleteCookiesForTab(tab_id, std::move(cookies), std::move(on_done));
}

void TabSessionApplier::ReadCookiesForTab(SessionID tab_id,
                                          ReadCallback on_done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StartRead(tab_id, std::move(on_done));
}

void TabSessionApplier::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
}

void TabSessionApplier::StartApply(SessionID tab_id,
                                   std::vector<net::CanonicalCookie> cookies,
                                   WriteOptions options,
                                   ApplyCallback on_done) {
  if (!tab_id.is_valid() || tab_id.id() <= 0) {
    PostToCurrentSequence(std::move(on_done),
                          MakeFailure(
                              TabSessionApplyResult::Kind::kFailedInvalidSessionId,
                              "SessionID is invalid"));
    return;
  }

  TabSessionApplyResult failure;
  std::optional<ResolvedTabContext> context =
      ResolveTabContext(tab_id, &failure);
  if (!context) {
    PostToCurrentSequence(std::move(on_done), std::move(failure));
    return;
  }
  if (!context->web_contents) {
    PostToCurrentSequence(std::move(on_done),
                          MakeFailure(TabSessionApplyResult::Kind::kFailedTabClosed,
                                      "WebContents was closed before apply"));
    return;
  }
  if (options.persist == PersistSemantic::kPersistent &&
      !HasPersistAttachment(context->web_contents.get())) {
    PostToCurrentSequence(std::move(on_done),
                          MakeFailure(
                              TabSessionApplyResult::Kind::kFailedNoPersistAttachment,
                              "Persistent apply requires TanyaProfilePersistState"));
    return;
  }

  cookie_apply::ApplyOptions apply_options;
  apply_options.cookie_options = options.cookie_options;
  apply_options.override_source_url = options.override_source_url;
  cookie_apply::ApplyCanonicalCookiesAsync(
      context->cookie_manager, std::move(cookies), std::move(apply_options),
      base::BindOnce(&TabSessionApplier::OnApplyCompleted,
                     weak_factory_.GetWeakPtr(), *context, options.persist,
                     0u, std::move(on_done)));
}

void TabSessionApplier::StartReplace(SessionID tab_id,
                                     std::string domain,
                                     std::vector<net::CanonicalCookie> cookies,
                                     PersistSemantic persist,
                                     ApplyCallback on_done) {
  if (!tab_id.is_valid() || tab_id.id() <= 0) {
    PostToCurrentSequence(std::move(on_done),
                          MakeFailure(
                              TabSessionApplyResult::Kind::kFailedInvalidSessionId,
                              "SessionID is invalid"));
    return;
  }

  TabSessionApplyResult failure;
  std::optional<ResolvedTabContext> context =
      ResolveTabContext(tab_id, &failure);
  if (!context) {
    PostToCurrentSequence(std::move(on_done), std::move(failure));
    return;
  }
  if (domain.empty()) {
    PostToCurrentSequence(
        std::move(on_done),
        MakeFailure(TabSessionApplyResult::Kind::kFailedDeletePhase,
                    "ReplaceCookiesForDomain requires a non-empty domain"));
    return;
  }
  if (!context->web_contents) {
    PostToCurrentSequence(std::move(on_done),
                          MakeFailure(TabSessionApplyResult::Kind::kFailedTabClosed,
                                      "WebContents was closed before replace"));
    return;
  }
  if (persist == PersistSemantic::kPersistent &&
      !HasPersistAttachment(context->web_contents.get())) {
    PostToCurrentSequence(std::move(on_done),
                          MakeFailure(
                              TabSessionApplyResult::Kind::kFailedNoPersistAttachment,
                              "Persistent replace requires TanyaProfilePersistState"));
    return;
  }

  auto filter = cookie_apply::MakeDomainFilter(domain);
  cookie_apply::DeleteCookiesAsync(
      context->cookie_manager, std::move(filter),
      base::BindOnce(&TabSessionApplier::OnReplaceDeleteCompleted,
                     weak_factory_.GetWeakPtr(), *context, std::move(domain),
                     std::move(cookies), persist, std::move(on_done)));
}

void TabSessionApplier::StartDelete(SessionID tab_id,
                                    std::string domain,
                                    ApplyCallback on_done) {
  if (!tab_id.is_valid() || tab_id.id() <= 0) {
    PostToCurrentSequence(std::move(on_done),
                          MakeFailure(
                              TabSessionApplyResult::Kind::kFailedInvalidSessionId,
                              "SessionID is invalid"));
    return;
  }

  TabSessionApplyResult failure;
  std::optional<ResolvedTabContext> context =
      ResolveTabContext(tab_id, &failure);
  if (!context) {
    PostToCurrentSequence(std::move(on_done), std::move(failure));
    return;
  }
  if (domain.empty()) {
    PostToCurrentSequence(
        std::move(on_done),
        MakeFailure(TabSessionApplyResult::Kind::kFailedDeletePhase,
                    "DeleteCookiesForDomain requires a non-empty domain"));
    return;
  }

  auto filter = cookie_apply::MakeDomainFilter(domain);
  cookie_apply::DeleteCookiesAsync(
      context->cookie_manager, std::move(filter),
      base::BindOnce(&TabSessionApplier::OnDeleteCompleted,
                     weak_factory_.GetWeakPtr(), *context, std::move(domain),
                     std::move(on_done)));
}

void TabSessionApplier::StartDeleteCookiesForTab(
    SessionID tab_id,
    std::vector<net::CanonicalCookie> cookies,
    ApplyCallback on_done) {
  if (!tab_id.is_valid() || tab_id.id() <= 0) {
    PostToCurrentSequence(std::move(on_done),
                          MakeFailure(
                              TabSessionApplyResult::Kind::kFailedInvalidSessionId,
                              "SessionID is invalid"));
    return;
  }

  TabSessionApplyResult failure;
  std::optional<ResolvedTabContext> context =
      ResolveTabContext(tab_id, &failure);
  if (!context) {
    PostToCurrentSequence(std::move(on_done), std::move(failure));
    return;
  }
  if (!context->web_contents) {
    PostToCurrentSequence(std::move(on_done),
                          MakeFailure(TabSessionApplyResult::Kind::kFailedTabClosed,
                                      "WebContents was closed before delete"));
    return;
  }

  const size_t attempted = cookies.size();
  cookie_apply::DeleteCanonicalCookiesAsync(
      context->cookie_manager, std::move(cookies),
      base::BindOnce(&TabSessionApplier::OnDeleteCookiesForTabCompleted,
                     weak_factory_.GetWeakPtr(), *context, attempted,
                     std::move(on_done)));
}

void TabSessionApplier::StartRead(SessionID tab_id, ReadCallback on_done) {
  if (!tab_id.is_valid() || tab_id.id() <= 0) {
    PostToCurrentSequence(std::move(on_done), net::CookieList());
    return;
  }

  TabSessionApplyResult failure;
  std::optional<ResolvedTabContext> context =
      ResolveTabContext(tab_id, &failure);
  if (!context) {
    PostToCurrentSequence(std::move(on_done), net::CookieList());
    return;
  }
  if (!context->web_contents) {
    PostToCurrentSequence(std::move(on_done), net::CookieList());
    return;
  }

  cookie_apply::FlushCookieStoreAsync(
      context->cookie_manager,
      base::BindOnce(&TabSessionApplier::OnReadFlushCompleted,
                     weak_factory_.GetWeakPtr(), *context, std::move(on_done)));
}

std::optional<TabSessionApplier::ResolvedTabContext>
TabSessionApplier::ResolveTabContext(SessionID tab_id,
                                     TabSessionApplyResult* failure_out) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(failure_out);

  if (!tab_id.is_valid() || tab_id.id() <= 0) {
    *failure_out =
        MakeFailure(TabSessionApplyResult::Kind::kFailedInvalidSessionId,
                    "SessionID is invalid");
    return std::nullopt;
  }

  content::WebContents* web_contents =
      FindWebContentsBySessionId(profile_.get(), tab_id);
  if (!web_contents) {
    *failure_out = MakeFailure(TabSessionApplyResult::Kind::kFailedTabNotFound,
                               "No tab found for the given SessionID");
    return std::nullopt;
  }
  if (web_contents->IsBeingDestroyed()) {
    *failure_out = MakeFailure(TabSessionApplyResult::Kind::kFailedTabClosed,
                               "WebContents is being destroyed");
    return std::nullopt;
  }

  TabContainerManager* manager =
      tab_container::GetExistingForBrowserContext(profile_.get());
  if (!manager) {
    *failure_out =
        MakeFailure(TabSessionApplyResult::Kind::kFailedNoContainerManager,
                    "TabContainerManager is not attached to this profile");
    return std::nullopt;
  }
  if (manager->GetContainerIdForTab(web_contents).empty()) {
    *failure_out =
        MakeFailure(TabSessionApplyResult::Kind::kFailedNoStoragePartition,
                    "Tab has no PTI container binding");
    return std::nullopt;
  }

  content::StoragePartition* partition =
      manager->GetStoragePartitionForWebContents(web_contents);
  if (!partition) {
    *failure_out =
        MakeFailure(TabSessionApplyResult::Kind::kFailedNoStoragePartition,
                    "Failed to resolve StoragePartition for tab");
    return std::nullopt;
  }
  if (partition == profile_->GetDefaultStoragePartition()) {
    *failure_out =
        MakeFailure(TabSessionApplyResult::Kind::kFailedDefaultPartition,
                    "Resolved default StoragePartition; refusing shared jar");
    return std::nullopt;
  }

  network::mojom::CookieManager* cookie_manager =
      partition->GetCookieManagerForBrowserProcess();
  if (!cookie_manager) {
    *failure_out =
        MakeFailure(TabSessionApplyResult::Kind::kFailedNoCookieManager,
                    "StoragePartition has no CookieManager");
    return std::nullopt;
  }

  ResolvedTabContext context;
  context.web_contents = web_contents->GetWeakPtr();
  context.partition = partition;
  context.cookie_manager = cookie_manager;
  return context;
}

TabSessionApplier::PersistFlushResult
TabSessionApplier::MaybeSchedulePersistFlush(ResolvedTabContext context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  content::WebContents* contents = context.web_contents.get();
  if (!contents) {
    return PersistFlushResult::kFailedSchedule;
  }

  TanyaProfilePersistState* state =
      TanyaProfilePersistState::FromWebContents(contents);
  if (!state || state->attached_encrypted_path().empty()) {
    return PersistFlushResult::kNoAttachment;
  }

  TabContainerManager* manager =
      tab_container::GetExistingForBrowserContext(contents->GetBrowserContext());
  if (!manager) {
    return PersistFlushResult::kFailedSchedule;
  }

  if (tab_container::GetOrCreateProfileEncryptionKey(profile_).empty()) {
    return PersistFlushResult::kFailedSchedule;
  }

  content::StoragePartition* partition =
      manager->GetStoragePartitionForWebContents(contents);
  if (!partition ||
      partition == contents->GetBrowserContext()->GetDefaultStoragePartition()) {
    return PersistFlushResult::kFailedSchedule;
  }

  if (partition != context.partition) {
    return PersistFlushResult::kFailedSchedule;
  }

  network::mojom::CookieManager* cookie_manager =
      partition->GetCookieManagerForBrowserProcess();
  if (!cookie_manager || cookie_manager != context.cookie_manager) {
    return PersistFlushResult::kFailedSchedule;
  }

  FlushTanyaPersistedProfileToDiskIfAttachedAsync(contents);
  return PersistFlushResult::kScheduled;
}

bool TabSessionApplier::HasPersistAttachment(content::WebContents* contents) const {
  if (!contents) {
    return false;
  }
  TanyaProfilePersistState* state =
      TanyaProfilePersistState::FromWebContents(contents);
  return state && !state->attached_encrypted_path().empty();
}

void TabSessionApplier::OnApplyCompleted(
    ResolvedTabContext context,
    PersistSemantic persist,
    size_t deleted_first,
    ApplyCallback on_done,
    cookie_apply::CookieApplyOutcome outcome) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!context.web_contents) {
    std::move(on_done).Run(MakeFailure(
        TabSessionApplyResult::Kind::kFailedTabClosed,
        "WebContents closed before cookie apply callback completed"));
    return;
  }

  TabSessionApplyResult result;
  result.cookies_attempted = outcome.attempted;
  result.cookies_applied = outcome.applied;
  result.cookies_deleted_first = deleted_first;
  result.skipped = std::move(outcome.skipped);

  if (persist == PersistSemantic::kPersistent) {
    switch (MaybeSchedulePersistFlush(context)) {
      case PersistFlushResult::kScheduled:
        break;
      case PersistFlushResult::kNoAttachment:
        result.kind = TabSessionApplyResult::Kind::kFailedNoPersistAttachment;
        result.error_detail =
            "Persistent apply requires TanyaProfilePersistState";
        std::move(on_done).Run(std::move(result));
        return;
      case PersistFlushResult::kFailedSchedule:
        result.kind = TabSessionApplyResult::Kind::kFailedPersistFlush;
        result.error_detail =
            "Failed to schedule Tanya profile persistence flush";
        std::move(on_done).Run(std::move(result));
        return;
    }
  }

  if (outcome.attempted > 0 && outcome.applied == 0) {
    result.kind = TabSessionApplyResult::Kind::kFailedAllApplyRejected;
    result.error_detail =
        "All candidate cookies were rejected by inclusion checks";
  } else if (!result.skipped.empty()) {
    result.kind = TabSessionApplyResult::Kind::kPartialSuccess;
  } else {
    result.kind = TabSessionApplyResult::Kind::kSuccess;
  }

  std::move(on_done).Run(std::move(result));
}

void TabSessionApplier::OnDeleteCompleted(ResolvedTabContext context,
                                          std::string domain,
                                          ApplyCallback on_done,
                                          cookie_apply::DeleteCookiesOutcome
                                              outcome) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!context.web_contents) {
    std::move(on_done).Run(
        MakeFailure(TabSessionApplyResult::Kind::kFailedTabClosed,
                    "WebContents closed before delete completed"));
    return;
  }
  if (!outcome.success) {
    std::move(on_done).Run(
        MakeFailure(TabSessionApplyResult::Kind::kFailedDeletePhase,
                    "DeleteCookiesAsync failed for domain: " + domain));
    return;
  }

  TabSessionApplyResult result;
  result.kind = TabSessionApplyResult::Kind::kSuccess;
  result.cookies_deleted_first = outcome.num_deleted;
  std::move(on_done).Run(std::move(result));
}

void TabSessionApplier::OnDeleteCookiesForTabCompleted(
    ResolvedTabContext context,
    size_t attempted,
    ApplyCallback on_done,
    cookie_apply::DeleteCookiesOutcome outcome) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!context.web_contents) {
    std::move(on_done).Run(
        MakeFailure(TabSessionApplyResult::Kind::kFailedTabClosed,
                    "WebContents closed before per-cookie delete completed"));
    return;
  }
  TabSessionApplyResult result;
  result.cookies_attempted = attempted;
  result.cookies_deleted_first = outcome.num_deleted;
  if (!outcome.success) {
    result.kind = TabSessionApplyResult::Kind::kFailedDeletePhase;
    result.error_detail =
        "One or more cookies failed to delete via DeleteCanonicalCookie";
  } else if (attempted > 0 && outcome.num_deleted == 0) {

    result.kind = TabSessionApplyResult::Kind::kPartialSuccess;
  } else if (attempted > 0 && outcome.num_deleted < attempted) {
    result.kind = TabSessionApplyResult::Kind::kPartialSuccess;
  } else {
    result.kind = TabSessionApplyResult::Kind::kSuccess;
  }
  std::move(on_done).Run(std::move(result));
}

void TabSessionApplier::OnReplaceDeleteCompleted(
    ResolvedTabContext context,
    std::string domain,
    std::vector<net::CanonicalCookie> cookies,
    PersistSemantic persist,
    ApplyCallback on_done,
    cookie_apply::DeleteCookiesOutcome outcome) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!context.web_contents) {
    std::move(on_done).Run(
        MakeFailure(TabSessionApplyResult::Kind::kFailedTabClosed,
                    "WebContents closed before replace delete phase completed"));
    return;
  }
  if (!outcome.success) {
    std::move(on_done).Run(
        MakeFailure(TabSessionApplyResult::Kind::kFailedDeletePhase,
                    "DeleteCookiesAsync failed for domain: " + domain));
    return;
  }

  cookie_apply::ApplyCanonicalCookiesAsync(
      context.cookie_manager, std::move(cookies), cookie_apply::ApplyOptions(),
      base::BindOnce(&TabSessionApplier::OnApplyCompleted,
                     weak_factory_.GetWeakPtr(), context, persist,
                     outcome.num_deleted,
                     std::move(on_done)));
}

void TabSessionApplier::OnReadFlushCompleted(ResolvedTabContext context,
                                             ReadCallback on_done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!context.web_contents) {
    std::move(on_done).Run(net::CookieList());
    return;
  }

  cookie_apply::GetAllCookiesAsync(
      context.cookie_manager,
      base::BindOnce(&TabSessionApplier::OnReadCookiesCompleted,
                     weak_factory_.GetWeakPtr(), context, std::move(on_done)));
}

void TabSessionApplier::OnReadCookiesCompleted(ResolvedTabContext context,
                                               ReadCallback on_done,
                                               net::CookieList cookies) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!context.web_contents) {
    std::move(on_done).Run(net::CookieList());
    return;
  }
  std::move(on_done).Run(std::move(cookies));
}

}  
