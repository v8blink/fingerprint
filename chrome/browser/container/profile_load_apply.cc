
#include "chrome/browser/container/profile_load_apply.h"

#include <memory>
#include <string>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/container/createProfiles.h"
#include "chrome/browser/container/tanya_profile_persist_state.h"
#include "chrome/browser/container/profile_encryption_key_provider.h"
#include "chrome/browser/container/tab_container_manager.h"
#include "chrome/browser/container/tab_container_manager_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"
#include "tanya/browser/tanya_proxy_service_factory.h"
#include "tanya/components/proxy/browser/tanya_proxy_service.h"
#include "tanya/net/tanya_proxy.h"
#include "net/cookies/canonical_cookie.h"
#include "services/network/public/mojom/clear_data_filter.mojom.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"

namespace tab_container {

std::unique_ptr<net::TanyaProxy> TanyaProxyFromTanyaProfileProxyJson(
    const std::string& proxy_json) {
  if (proxy_json.empty()) {
    return nullptr;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(proxy_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return nullptr;
  }
  const base::DictValue& d = parsed->GetDict();

  std::string pac_config;
  std::string username;
  std::string password;
  std::string proxy_name;

  if (const std::string* p = d.FindString("pac_config")) {
    pac_config = *p;
  }
  if (const std::string* u = d.FindString("username")) {
    username = *u;
  }
  if (const std::string* pw = d.FindString("password")) {
    password = *pw;
  }
  if (const std::string* pn = d.FindString("proxy_name")) {
    proxy_name = *pn;
  }

  if (pac_config.empty()) {
    const std::string* host = d.FindString("host");
    const std::string* port = d.FindString("port");
    if (!host || !port || host->empty() || port->empty()) {
      return nullptr;
    }
    proxy_name = *host + ":" + *port;

    pac_config = "SOCKS5h " + proxy_name;
  }

  if (pac_config.empty()) {
    return nullptr;
  }

  return std::make_unique<net::TanyaProxy>(pac_config, username, password,
                                           proxy_name);
}

namespace {

bool ApplyProxyJsonToTab(content::WebContents* web_contents,
                         const std::string& proxy_json) {
  if (!web_contents) {
    return false;
  }
  const SessionID session_id =
      sessions::SessionTabHelper::IdForTab(web_contents);
  if (!session_id.is_valid() || session_id.id() <= 0) {
    return false;
  }

  auto* proxy_service = tanya::TanyaProxyServiceFactory::GetForProfile(
      web_contents->GetBrowserContext());
  if (!proxy_service) {
    return false;
  }

  const int64_t tab_id = static_cast<int64_t>(session_id.id());

  if (proxy_json.empty()) {
    proxy_service->DropProxyFor(tab_id);
    web_contents->NotifyPreferencesChanged();
    return true;
  }

  std::unique_ptr<net::TanyaProxy> built =
      TanyaProxyFromTanyaProfileProxyJson(proxy_json);
  if (!built) {

    proxy_service->DropProxyFor(tab_id);
    web_contents->NotifyPreferencesChanged();
    return false;
  }

  proxy_service->SetProxyFor(tab_id, std::move(built), false);
  web_contents->NotifyPreferencesChanged();
  return true;
}

std::string SerializeTanyaProxyJsonForWebContents(content::WebContents* contents) {
  if (!contents) {
    return "{}";
  }
  const SessionID session_id = sessions::SessionTabHelper::IdForTab(contents);
  if (!session_id.is_valid() || session_id.id() <= 0) {
    return "{}";
  }
  auto* proxy_service = tanya::TanyaProxyServiceFactory::GetForProfile(
      contents->GetBrowserContext());
  if (!proxy_service) {
    return "{}";
  }
  net::TanyaProxy* proxy =
      proxy_service->GetProxyFor(static_cast<int64_t>(session_id.id()));
  if (!proxy) {
    return "{}";
  }
  base::DictValue d;
  d.Set("pac_config", proxy->pac_config);
  d.Set("username", proxy->username);
  d.Set("password", proxy->password);
  d.Set("proxy_name", proxy->proxy_name);
  std::string json;
  base::JSONWriter::Write(d, &json);
  return json;
}

void FlushCookieStoreSync(network::mojom::CookieManager* cookie_manager) {
  base::RunLoop loop(base::RunLoop::Type::kNestableTasksAllowed);
  cookie_manager->FlushCookieStore(loop.QuitClosure());
  base::ScopedAllowBlockingForTesting allow_blocking;
  loop.Run();
}

void GetAllCookiesSync(network::mojom::CookieManager* cookie_manager,
                       net::CookieList* out) {
  base::RunLoop loop(base::RunLoop::Type::kNestableTasksAllowed);
  cookie_manager->GetAllCookies(base::BindOnce(
      [](base::RunLoop* run_loop, net::CookieList* dst,
         const net::CookieList& cookies) {
        *dst = cookies;
        run_loop->Quit();
      },
      base::Unretained(&loop), base::Unretained(out)));
  base::ScopedAllowBlockingForTesting allow_blocking;
  loop.Run();
}

GURL SourceUrlForImportedCookie(const net::CanonicalCookie& c) {
  std::string host = c.Domain();
  if (!host.empty() && host[0] == '.') {
    host = host.substr(1);
  }
  const char* scheme = c.IsSecure() ? "https" : "http";
  std::string path = c.Path();
  if (path.empty()) {
    path = "/";
  }
  const GURL url(
      base::StringPrintf("%s://%s%s", scheme, host.c_str(), path.c_str()));
  if (url.is_valid()) {
    return url;
  }
  return GURL(base::StringPrintf("https://%s/", host.c_str()));
}

void WipePartitionDataSync(content::StoragePartition* partition) {
  if (!partition) {
    return;
  }

  const uint32_t kClearDataMask =
      content::StoragePartition::REMOVE_DATA_MASK_ALL;

  network::mojom::NetworkContext* network_context =
      partition->GetNetworkContext();

  const size_t barrier_count = network_context ? 15u : 2u;

  base::RunLoop loop(base::RunLoop::Type::kNestableTasksAllowed);
  base::RepeatingClosure barrier =
      base::BarrierClosure(barrier_count, loop.QuitClosure());

  partition->ClearData(
      kClearDataMask,
      blink::StorageKey(),
      base::Time(), base::Time::Max(), barrier);
  if (network_context) {
    network_context->ClearHttpCache(base::Time(), base::Time::Max(),
                                    nullptr, barrier);
    network_context->ClearNetworkingHistoryBetween(base::Time(),
                                                   base::Time::Max(), barrier);
    network_context->ClearHostCache(nullptr, barrier);
    network_context->ClearHttpAuthCache(base::Time(), base::Time::Max(),
                                        nullptr, barrier);
    network_context->ClearCorsPreflightCache(nullptr, barrier);
    network_context->CloseAllConnections(barrier);
    network_context->ClearTrustTokenData(nullptr, barrier);
    network_context->ClearSharedDictionaryCache(
        base::Time(), base::Time::Max(), nullptr, barrier);
    network_context->ClearNetworkErrorLogging(nullptr, barrier);
    network_context->ClearReportingCacheReports(nullptr, barrier);
    network_context->ClearReportingCacheClients(nullptr, barrier);
    network_context->ClearDomainReliability(
        nullptr,
        network::mojom::NetworkContext_DomainReliabilityClearMode::
            CLEAR_CONTEXTS,
        barrier);
    network_context->ClearBadProxiesCache(barrier);
  }

  partition->ClearCodeCaches(base::Time(), base::Time::Max(),
                             {}, barrier);
  base::ScopedAllowBlockingForTesting allow_blocking;
  loop.Run();
}

void WriteTanyaFlushOnBackgroundThread(base::FilePath path,
                                      std::string encryption_key,
                                      std::string cookies_json,
                                      std::string proxy_json_for_disk) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  std::string error;
  if (!UpdateEncryptedTanyaProfileAtPath(path, encryption_key, cookies_json,
                                        proxy_json_for_disk, &error)) {
    ;
    return;
  }
}

void OnTanyaFlushGotCookies(base::FilePath path,
                           std::string encryption_key,
                           std::string file_proxy,
                           int32_t session_id,
                           const net::CookieList& cookies) {

  ;
  const std::string cookies_json =
      SerializeCookieListToJson(cookies, CookieJsonSerializeOptions());
  ;
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce(&WriteTanyaFlushOnBackgroundThread, std::move(path),
                     std::move(encryption_key), std::move(cookies_json),
                     std::move(file_proxy)));
}

void StartTanyaProfileAsyncFlush(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  TanyaProfilePersistState* state =
      TanyaProfilePersistState::FromWebContents(contents);
  if (!state || state->attached_encrypted_path().empty()) {
    return;
  }
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  if (!profile) {
    return;
  }
  TabContainerManager* container_manager =
      tab_container::GetForBrowserContext(contents->GetBrowserContext());
  if (!container_manager) {
    return;
  }
  const std::string encryption_key =
      tab_container::GetOrCreateProfileEncryptionKey(profile);
  if (encryption_key.empty()) {
    return;
  }
  content::StoragePartition* partition =
      container_manager->GetStoragePartitionForWebContents(contents);
  if (!partition) {
    return;
  }
  content::BrowserContext* browser_context = contents->GetBrowserContext();
  if (partition == browser_context->GetDefaultStoragePartition()) {
    return;
  }
  network::mojom::CookieManager* cookie_manager =
      partition->GetCookieManagerForBrowserProcess();
  if (!cookie_manager) {
    return;
  }

  const base::FilePath path = state->attached_encrypted_path();
  std::string file_proxy = state->profile_file_proxy_json();
  if (file_proxy.empty()) {
    file_proxy = SerializeTanyaProxyJsonForWebContents(contents);
    ;
  }
  const int32_t session_id =
      sessions::SessionTabHelper::IdForTab(contents).id();

  cookie_manager->FlushCookieStore(base::BindOnce(
      [](base::FilePath flush_path, std::string enc_key, std::string f_proxy,
         int32_t sid,
         network::mojom::CookieManager* cm) {
        if (!cm) {
          ;
          return;
        }
        cm->GetAllCookies(base::BindOnce(&OnTanyaFlushGotCookies,
                                         std::move(flush_path),
                                         std::move(enc_key),
                                         std::move(f_proxy), sid));
      },
      path, std::move(encryption_key), std::move(file_proxy), session_id,
      cookie_manager));
}

}  

void WipeTanyaTabPartition(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  TabContainerManager* container_manager =
      tab_container::GetForBrowserContext(contents->GetBrowserContext());
  if (!container_manager) {
    return;
  }
  content::StoragePartition* partition =
      container_manager->GetStoragePartitionForWebContents(contents);
  if (!partition) {
    return;
  }
  content::BrowserContext* browser_context = contents->GetBrowserContext();
  if (partition == browser_context->GetDefaultStoragePartition()) {
    return;
  }
  base::ScopedAllowBlockingForTesting allow_blocking;
  WipePartitionDataSync(partition);
}

std::string SerializeTanyaTabPartitionCookiesJson(content::WebContents* contents) {
  if (!contents) {
    return "[]";
  }
  TabContainerManager* container_manager =
      tab_container::GetForBrowserContext(contents->GetBrowserContext());
  if (!container_manager) {
    return "[]";
  }
  if (container_manager->GetContainerIdForTab(contents).empty()) {
    return "[]";
  }

  content::StoragePartition* partition =
      container_manager->GetStoragePartitionForWebContents(contents);
  if (!partition) {
    return "[]";
  }

  content::BrowserContext* browser_context = contents->GetBrowserContext();
  if (partition == browser_context->GetDefaultStoragePartition()) {
    return "[]";
  }

  network::mojom::CookieManager* cookie_manager =
      partition->GetCookieManagerForBrowserProcess();
  if (!cookie_manager) {
    return "[]";
  }

  base::ScopedAllowBlockingForTesting allow_blocking;
  FlushCookieStoreSync(cookie_manager);
  net::CookieList cookie_list;
  GetAllCookiesSync(cookie_manager, &cookie_list);
  return SerializeCookieListToJson(cookie_list, CookieJsonSerializeOptions());
}

bool ApplyPersistedProfileFromEncryptedFile(
    content::WebContents* web_contents,
    Profile* profile,
    const base::FilePath& encrypted_profile_file,
    std::string* error_out,
    bool wipe_partition) {
  if (!web_contents || !profile) {
    if (error_out) {
      *error_out = "invalid web_contents or profile";
    }
    return false;
  }

  const std::string encryption_key =
      tab_container::GetOrCreateProfileEncryptionKey(profile);
  if (encryption_key.empty()) {
    if (error_out) {
      *error_out = "encryption key unavailable";
    }
    return false;
  }

  PersistedProfile persisted;
  if (!ReadPersistedProfileFromEncryptedFile(encrypted_profile_file,
                                              encryption_key, &persisted,
                                              error_out)) {
    return false;
  }

  TabContainerManager* container_manager =
      tab_container::GetForBrowserContext(web_contents->GetBrowserContext());
  if (!container_manager) {
    if (error_out) {
      *error_out = "no tab container manager";
    }
    return false;
  }

  content::StoragePartition* partition =
      container_manager->GetStoragePartitionForWebContents(web_contents);
  if (!partition) {
    if (error_out) {
      *error_out = "no storage partition for tab";
    }
    return false;
  }
  network::mojom::CookieManager* cookie_manager =
      partition->GetCookieManagerForBrowserProcess();
  if (!cookie_manager) {
    if (error_out) {
      *error_out = "no cookie manager";
    }
    return false;
  }

  if (wipe_partition) {
    WipePartitionDataSync(partition);
  }

  TanyaProfilePersistState::RegisterAttachedEncryptedProfile(
      web_contents, encrypted_profile_file, persisted.proxy_json,
      persisted.profile_name);

  const bool proxy_applied =
      ApplyProxyJsonToTab(web_contents, persisted.proxy_json);
  if (!proxy_applied && !persisted.proxy_json.empty()) {
    ;
  }

  {
    const net::CookieList cookies =
        DeserializeCookieListFromJson(persisted.cookies_json);
    if (!cookies.empty()) {
      base::RunLoop restore_loop(base::RunLoop::Type::kNestableTasksAllowed);
      base::RepeatingClosure done = base::BarrierClosure(
          cookies.size(),
          base::BindOnce([](base::RunLoop* l) { l->Quit(); },
                         base::Unretained(&restore_loop)));

      static constexpr std::string_view kAntiBotCookieNames[] = {
          "datadome",   
          "_abck",      
          "bm_sz",      
          "bm_sv",      
          "ak_bmsc",    
          "__cf_bm",    
          "cf_clearance",  
      };
      for (const net::CanonicalCookie& cookie : cookies) {
        bool is_antibot = false;
        for (const auto& blocked_name : kAntiBotCookieNames) {
          if (cookie.Name() == blocked_name) {
            is_antibot = true;
            break;
          }
        }
        if (is_antibot) {
          done.Run();
          continue;
        }
        const GURL source_url = SourceUrlForImportedCookie(cookie);
        cookie_manager->SetCanonicalCookie(
            cookie, source_url, net::CookieOptions::MakeAllInclusive(),
            base::BindOnce(
                [](base::RepeatingClosure barrier,
                   net::CookieAccessResult ) { barrier.Run(); },
                done));
      }
      base::ScopedAllowBlockingForTesting allow_blocking;
      restore_loop.Run();
    }
  }

  if (error_out) {
    error_out->clear();
  }
  return true;
}

bool InheritTanyaProfileFromOpener(content::WebContents* child,
                                  content::WebContents* opener) {
  if (!child || !opener) {
    return false;
  }
  auto* parent_state = TanyaProfilePersistState::FromWebContents(opener);
  if (!parent_state) {
    return false;
  }
  const base::FilePath& parent_path = parent_state->attached_encrypted_path();
  if (parent_path.empty()) {
    return false;
  }

  TanyaProfilePersistState::RegisterAttachedEncryptedProfile(
      child, parent_path, parent_state->profile_file_proxy_json(),
      parent_state->attached_profile_display_name());

  ApplyProxyJsonToTab(child, parent_state->profile_file_proxy_json());

  return true;
}

bool FlushTanyaPersistedProfileToDiskIfAttached(content::WebContents* contents) {
  if (!contents) {
    return false;
  }
  TanyaProfilePersistState* state =
      TanyaProfilePersistState::FromWebContents(contents);
  if (!state || state->attached_encrypted_path().empty()) {
    return false;
  }

  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  if (!profile) {
    return false;
  }

  TabContainerManager* container_manager =
      tab_container::GetForBrowserContext(contents->GetBrowserContext());
  if (!container_manager) {
    return false;
  }

  const std::string encryption_key =
      tab_container::GetOrCreateProfileEncryptionKey(profile);
  if (encryption_key.empty()) {
    return false;
  }

  const std::string cookies_json =
      SerializeTanyaTabPartitionCookiesJson(contents);

  const std::string& file_proxy = state->profile_file_proxy_json();
  const std::string proxy_json_for_disk =
      file_proxy.empty() ? SerializeTanyaProxyJsonForWebContents(contents)
                         : file_proxy;
  std::string error;
  if (!UpdateEncryptedTanyaProfileAtPath(state->attached_encrypted_path(),
                                         encryption_key, cookies_json,
                                         proxy_json_for_disk, &error)) {
    ;
    return false;
  }

  return true;
}

void FlushTanyaPersistedProfileToDiskIfAttachedAsync(
    content::WebContents* contents) {
  StartTanyaProfileAsyncFlush(contents);
}

}  
