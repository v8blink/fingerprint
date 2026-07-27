
#ifndef CHROME_BROWSER_CONTAINER_COOKIE_APPLY_H_
#define CHROME_BROWSER_CONTAINER_COOKIE_APPLY_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_options.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "url/gurl.h"

namespace tab_container::cookie_apply {

struct DeleteCookiesOutcome {
  bool success = false;
  uint32_t num_deleted = 0;
};

void GetAllCookiesAsync(
    network::mojom::CookieManager* cookie_manager,
    base::OnceCallback<void(net::CookieList)> on_done);

void DeleteCookiesAsync(
    network::mojom::CookieManager* cookie_manager,
    network::mojom::CookieDeletionFilterPtr filter,
    base::OnceCallback<void(DeleteCookiesOutcome)> on_done);

void DeleteCanonicalCookiesAsync(
    network::mojom::CookieManager* cookie_manager,
    std::vector<net::CanonicalCookie> cookies,
    base::OnceCallback<void(DeleteCookiesOutcome)> on_done);

void FlushCookieStoreAsync(network::mojom::CookieManager* cookie_manager,
                           base::OnceClosure on_done);

struct ApplyOptions {
  ApplyOptions();
  ApplyOptions(const ApplyOptions&);
  ApplyOptions(ApplyOptions&&);
  ApplyOptions& operator=(const ApplyOptions&);
  ApplyOptions& operator=(ApplyOptions&&);
  ~ApplyOptions();

  net::CookieOptions cookie_options = net::CookieOptions::MakeAllInclusive();
  std::optional<GURL> override_source_url;
};

struct CookieApplyOutcome {
  CookieApplyOutcome();
  CookieApplyOutcome(const CookieApplyOutcome&);
  CookieApplyOutcome(CookieApplyOutcome&&);
  CookieApplyOutcome& operator=(const CookieApplyOutcome&);
  CookieApplyOutcome& operator=(CookieApplyOutcome&&);
  ~CookieApplyOutcome();

  size_t attempted = 0;
  size_t applied = 0;
  std::vector<std::pair<std::string, std::string>> skipped;
};

void ApplyCanonicalCookiesAsync(
    network::mojom::CookieManager* cookie_manager,
    std::vector<net::CanonicalCookie> cookies,
    ApplyOptions options,
    base::OnceCallback<void(CookieApplyOutcome)> on_done);

GURL SourceUrlForCookie(const net::CanonicalCookie& cookie);

network::mojom::CookieDeletionFilterPtr MakeDomainFilter(
    const std::string& domain);

}  

#endif  
