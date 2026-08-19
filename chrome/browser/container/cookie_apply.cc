
#include "chrome/browser/container/cookie_apply.h"

#include <limits>
#include <string>
#include <utility>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/strings/stringprintf.h"
#include "base/task/single_thread_task_runner.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"

namespace tab_container::cookie_apply {

namespace {

constexpr uint32_t kDeleteCookiesFailureSentinel =
    std::numeric_limits<uint32_t>::max();

template <typename Callback, typename... Args>
void PostToCurrentSequence(Callback callback, Args&&... args) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(callback), std::forward<Args>(args)...));
}

}  

ApplyOptions::ApplyOptions() = default;
ApplyOptions::ApplyOptions(const ApplyOptions&) = default;
ApplyOptions::ApplyOptions(ApplyOptions&&) = default;
ApplyOptions& ApplyOptions::operator=(const ApplyOptions&) = default;
ApplyOptions& ApplyOptions::operator=(ApplyOptions&&) = default;
ApplyOptions::~ApplyOptions() = default;

CookieApplyOutcome::CookieApplyOutcome() = default;
CookieApplyOutcome::CookieApplyOutcome(const CookieApplyOutcome&) = default;
CookieApplyOutcome::CookieApplyOutcome(CookieApplyOutcome&&) = default;
CookieApplyOutcome& CookieApplyOutcome::operator=(const CookieApplyOutcome&) =
    default;
CookieApplyOutcome& CookieApplyOutcome::operator=(CookieApplyOutcome&&) =
    default;
CookieApplyOutcome::~CookieApplyOutcome() = default;

void GetAllCookiesAsync(network::mojom::CookieManager* cookie_manager,
                        base::OnceCallback<void(net::CookieList)> on_done) {
  if (!cookie_manager) {
    PostToCurrentSequence(std::move(on_done), net::CookieList());
    return;
  }
  cookie_manager->GetAllCookies(
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::OnceCallback<void(net::CookieList)> on_done,
                 const net::CookieList& cookies) {
                std::move(on_done).Run(cookies);
              },
              std::move(on_done)),
          net::CookieList()));
}

void DeleteCookiesAsync(network::mojom::CookieManager* cookie_manager,
                        network::mojom::CookieDeletionFilterPtr filter,
                        base::OnceCallback<void(DeleteCookiesOutcome)> on_done) {
  if (!cookie_manager || !filter || !filter->including_domains ||
      filter->including_domains->empty()) {
    PostToCurrentSequence(std::move(on_done), DeleteCookiesOutcome());
    return;
  }
  cookie_manager->DeleteCookies(
      std::move(filter),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::OnceCallback<void(DeleteCookiesOutcome)> on_done,
                 uint32_t num_deleted) {
                DeleteCookiesOutcome outcome;
                outcome.success = num_deleted != kDeleteCookiesFailureSentinel;
                if (outcome.success) {
                  outcome.num_deleted = num_deleted;
                }
                std::move(on_done).Run(std::move(outcome));
              },
              std::move(on_done)),
          kDeleteCookiesFailureSentinel));
}

void DeleteCanonicalCookiesAsync(
    network::mojom::CookieManager* cookie_manager,
    std::vector<net::CanonicalCookie> cookies,
    base::OnceCallback<void(DeleteCookiesOutcome)> on_done) {
  if (!cookie_manager) {
    PostToCurrentSequence(std::move(on_done), DeleteCookiesOutcome());
    return;
  }
  if (cookies.empty()) {
    DeleteCookiesOutcome outcome;
    outcome.success = true;
    outcome.num_deleted = 0;
    PostToCurrentSequence(std::move(on_done), std::move(outcome));
    return;
  }

  auto outcome = base::MakeRefCounted<base::RefCountedData<DeleteCookiesOutcome>>();
  outcome->data.success = true;
  outcome->data.num_deleted = 0;

  base::OnceCallback<void()> final_cb = base::BindOnce(
      [](scoped_refptr<base::RefCountedData<DeleteCookiesOutcome>> outcome,
         base::OnceCallback<void(DeleteCookiesOutcome)> on_done) {
        std::move(on_done).Run(std::move(outcome->data));
      },
      outcome, std::move(on_done));
  base::RepeatingClosure barrier =
      base::BarrierClosure(cookies.size(), std::move(final_cb));

  for (const net::CanonicalCookie& cookie : cookies) {
    cookie_manager->DeleteCanonicalCookie(
        cookie,
        base::BindOnce(
            [](scoped_refptr<base::RefCountedData<DeleteCookiesOutcome>> outcome,
               base::RepeatingClosure barrier,
               bool success) {
              if (success) {
                ++outcome->data.num_deleted;
              } else {
                outcome->data.success = false;
              }
              barrier.Run();
            },
            outcome, barrier));
  }
}

void FlushCookieStoreAsync(network::mojom::CookieManager* cookie_manager,
                           base::OnceClosure on_done) {
  if (!cookie_manager) {
    PostToCurrentSequence(std::move(on_done));
    return;
  }
  cookie_manager->FlushCookieStore(
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(std::move(on_done)));
}

void ApplyCanonicalCookiesAsync(
    network::mojom::CookieManager* cookie_manager,
    std::vector<net::CanonicalCookie> cookies,
    ApplyOptions options,
    base::OnceCallback<void(CookieApplyOutcome)> on_done) {
  if (!cookie_manager) {
    PostToCurrentSequence(std::move(on_done), CookieApplyOutcome());
    return;
  }

  if (cookies.empty()) {
    PostToCurrentSequence(std::move(on_done), CookieApplyOutcome());
    return;
  }

  auto outcome = base::MakeRefCounted<base::RefCountedData<CookieApplyOutcome>>();
  outcome->data.attempted = cookies.size();

  base::OnceCallback<void()> final_cb = base::BindOnce(
      [](scoped_refptr<base::RefCountedData<CookieApplyOutcome>> outcome,
         base::OnceCallback<void(CookieApplyOutcome)> on_done) {
        std::move(on_done).Run(std::move(outcome->data));
      },
      outcome, std::move(on_done));
  base::RepeatingClosure barrier =
      base::BarrierClosure(cookies.size(), std::move(final_cb));

  for (const net::CanonicalCookie& cookie : cookies) {
    const GURL source_url = options.override_source_url.value_or(
        SourceUrlForCookie(cookie));
    const std::string name = cookie.Name();
    cookie_manager->SetCanonicalCookie(
        cookie, source_url, options.cookie_options,
        base::BindOnce(
            [](scoped_refptr<base::RefCountedData<CookieApplyOutcome>> outcome,
               std::string cookie_name,
               base::RepeatingClosure barrier,
               net::CookieAccessResult result) {
              const bool include = result.status.IsInclude();
              if (include) {
                ++outcome->data.applied;
              } else {
                outcome->data.skipped.emplace_back(
                    std::move(cookie_name), result.status.GetDebugString());
              }
              barrier.Run();
            },
            outcome, name, barrier));
  }
}

GURL SourceUrlForCookie(const net::CanonicalCookie& cookie) {
  std::string host = cookie.Domain();
  if (!host.empty() && host.front() == '.') {
    host.erase(0, 1);
  }
  const char* scheme = cookie.IsSecure() ? "https" : "http";
  std::string path = cookie.Path();
  if (path.empty()) {
    path = "/";
  }
  const GURL url(base::StringPrintf("%s://%s%s", scheme, host.c_str(),
                                    path.c_str()));
  if (url.is_valid()) {
    return url;
  }
  return GURL(base::StringPrintf("https://%s/", host.c_str()));
}

network::mojom::CookieDeletionFilterPtr MakeDomainFilter(
    const std::string& domain) {

  std::string normalized = domain;
  if (!normalized.empty() && normalized.front() == '.') {
    normalized.erase(0, 1);
  }
  auto filter = network::mojom::CookieDeletionFilter::New();
  filter->including_domains = std::vector<std::string>{std::move(normalized)};
  return filter;
}

}  
