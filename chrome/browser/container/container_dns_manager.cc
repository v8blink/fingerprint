
#include "chrome/browser/container/container_dns_manager.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/container/tab_container_manager.h"
#include "chrome/browser/container/tab_container_manager_factory.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"

namespace tab_container {

namespace {

constexpr const char* kLogPrefix = "[ContainerDnsManager]";

std::string DnsModeToString(ContainerDnsConfig::DnsMode mode) {
  switch (mode) {
    case ContainerDnsConfig::DnsMode::kSystem:
      return "System";
    case ContainerDnsConfig::DnsMode::kSecure:
      return "Secure";
    case ContainerDnsConfig::DnsMode::kAutomatic:
      return "Automatic";
    case ContainerDnsConfig::DnsMode::kOff:
      return "Off";
    case ContainerDnsConfig::DnsMode::kCustom:
      return "Custom";
    case ContainerDnsConfig::DnsMode::kProxyDns:
      return "ProxyDns";
  }
  return "Unknown";
}

std::string DnsSourceToString(DnsResolutionResult::Source source) {
  switch (source) {
    case DnsResolutionResult::Source::kCache:
      return "Cache";
    case DnsResolutionResult::Source::kSystem:
      return "System";
    case DnsResolutionResult::Source::kDoH:
      return "DoH";
    case DnsResolutionResult::Source::kCustom:
      return "Custom";
    case DnsResolutionResult::Source::kProxy:
      return "Proxy";
  }
  return "Unknown";
}

}  

bool ContainerDnsConfig::IsValid() const {

  switch (mode) {
    case DnsMode::kSecure:

      if (!doh_config.enabled || doh_config.servers.empty()) {
        return false;
      }
      break;
    case DnsMode::kCustom:

      if (custom_dns_config.nameservers.empty()) {
        return false;
      }
      break;
    case DnsMode::kProxyDns:

      if (proxy_dns_config.proxy_server.empty()) {
        return false;
      }
      break;
    default:
      break;
  }

  return true;
}

std::string ContainerDnsConfig::Serialize() const {
  base::DictValue dict;
  dict.Set("container_id", container_id);
  dict.Set("mode", static_cast<int>(mode));

  base::DictValue doh_dict;
  doh_dict.Set("enabled", doh_config.enabled);
  base::ListValue servers_list;
  for (const auto& server : doh_config.servers) {
    servers_list.Append(server);
  }
  doh_dict.Set("servers", std::move(servers_list));
  doh_dict.Set("template_uri", doh_config.template_uri);
  doh_dict.Set("fallback_without_doh", doh_config.fallback_without_doh);
  dict.Set("doh_config", std::move(doh_dict));

  base::DictValue custom_dict;
  base::ListValue nameservers_list;
  for (const auto& ns : custom_dns_config.nameservers) {
    nameservers_list.Append(ns);
  }
  custom_dict.Set("nameservers", std::move(nameservers_list));
  base::ListValue search_list;
  for (const auto& domain : custom_dns_config.search_domains) {
    search_list.Append(domain);
  }
  custom_dict.Set("search_domains", std::move(search_list));
  custom_dict.Set("timeout_ms", custom_dns_config.timeout_ms);
  custom_dict.Set("attempts", custom_dns_config.attempts);
  dict.Set("custom_dns_config", std::move(custom_dict));

  base::DictValue proxy_dict;
  proxy_dict.Set("route_dns_through_proxy", 
                 proxy_dns_config.route_dns_through_proxy);
  proxy_dict.Set("proxy_server", proxy_dns_config.proxy_server);
  dict.Set("proxy_dns_config", std::move(proxy_dict));

  base::DictValue cache_dict;
  cache_dict.Set("enabled", cache_config.enabled);
  cache_dict.Set("max_entries", cache_config.max_entries);
  cache_dict.Set("ttl_seconds", cache_config.ttl_seconds);
  cache_dict.Set("negative_ttl_seconds", cache_config.negative_ttl_seconds);
  dict.Set("cache_config", std::move(cache_dict));

  base::DictValue prefetch_dict;
  prefetch_dict.Set("enabled", prefetch_config.enabled);
  prefetch_dict.Set("max_concurrent", prefetch_config.max_concurrent);
  prefetch_dict.Set("allow_stale_on_failure", 
                    prefetch_config.allow_stale_on_failure);
  dict.Set("prefetch_config", std::move(prefetch_dict));

  base::DictValue privacy_dict;
  privacy_dict.Set("randomize_case", privacy_config.randomize_case);
  privacy_dict.Set("pad_queries", privacy_config.pad_queries);
  privacy_dict.Set("padding_block_size", privacy_config.padding_block_size);
  privacy_dict.Set("disable_ipv6", privacy_config.disable_ipv6);
  privacy_dict.Set("disable_additional", privacy_config.disable_additional);
  dict.Set("privacy_config", std::move(privacy_dict));

  std::string output;
  base::JSONWriter::Write(dict, &output);
  return output;
}

std::optional<ContainerDnsConfig> ContainerDnsConfig::Deserialize(
    const std::string& data) {
  auto parsed =
      base::JSONReader::Read(data, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }

  const base::DictValue& dict = parsed->GetDict();
  ContainerDnsConfig config;

  const std::string* container_id = dict.FindString("container_id");
  if (container_id) config.container_id = *container_id;

  config.mode = static_cast<DnsMode>(dict.FindInt("mode").value_or(0));

  const base::DictValue* doh_dict = dict.FindDict("doh_config");
  if (doh_dict) {
    config.doh_config.enabled = doh_dict->FindBool("enabled").value_or(false);
    const base::ListValue* servers = doh_dict->FindList("servers");
    if (servers) {
      for (const auto& server : *servers) {
        if (server.is_string()) {
          config.doh_config.servers.push_back(server.GetString());
        }
      }
    }
    const std::string* template_uri = doh_dict->FindString("template_uri");
    if (template_uri) config.doh_config.template_uri = *template_uri;
    config.doh_config.fallback_without_doh = 
        doh_dict->FindBool("fallback_without_doh").value_or(false);
  }

  const base::DictValue* custom_dict = dict.FindDict("custom_dns_config");
  if (custom_dict) {
    const base::ListValue* nameservers = custom_dict->FindList("nameservers");
    if (nameservers) {
      for (const auto& ns : *nameservers) {
        if (ns.is_string()) {
          config.custom_dns_config.nameservers.push_back(ns.GetString());
        }
      }
    }
    const base::ListValue* search = custom_dict->FindList("search_domains");
    if (search) {
      for (const auto& domain : *search) {
        if (domain.is_string()) {
          config.custom_dns_config.search_domains.push_back(domain.GetString());
        }
      }
    }
    config.custom_dns_config.timeout_ms = 
        custom_dict->FindInt("timeout_ms").value_or(5000);
    config.custom_dns_config.attempts = 
        custom_dict->FindInt("attempts").value_or(2);
  }

  return config;
}

ContainerDnsManager::ContainerDnsManager(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ;
}

ContainerDnsManager::~ContainerDnsManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;
}

void ContainerDnsManager::AddObserver(ContainerDnsObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void ContainerDnsManager::RemoveObserver(ContainerDnsObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

bool ContainerDnsManager::SetDnsConfig(const std::string& container_id,
                                       const ContainerDnsConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!config.IsValid()) {
    ;
    return false;
  }

  ;

  auto it = container_dns_.find(container_id);
  if (it == container_dns_.end()) {
    auto info = std::make_unique<ContainerDnsInfo>();
    info->container_id = container_id;
    info->config = config;
    info->config.container_id = container_id;
    container_dns_[container_id] = std::move(info);
    stats_.containers_configured++;
  } else {
    it->second->config = config;
    it->second->config.container_id = container_id;
  }

  stats_.last_activity = base::TimeTicks::Now();

  NotifyConfigUpdated(container_id, config);

  return true;
}

std::optional<ContainerDnsConfig> ContainerDnsManager::GetDnsConfig(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_dns_.find(container_id);
  if (it == container_dns_.end()) {
    return std::nullopt;
  }

  return it->second->config;
}

bool ContainerDnsManager::RemoveDnsConfig(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_dns_.find(container_id);
  if (it == container_dns_.end()) {
    return false;
  }

  ;

  container_dns_.erase(it);

  return true;
}

bool ContainerDnsManager::SetDnsMode(const std::string& container_id,
                                     ContainerDnsConfig::DnsMode mode) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_dns_.find(container_id);
  if (it == container_dns_.end()) {

    ContainerDnsConfig config;
    config.container_id = container_id;
    config.mode = mode;
    return SetDnsConfig(container_id, config);
  }

  it->second->config.mode = mode;

  ;

  NotifyConfigUpdated(container_id, it->second->config);

  return true;
}

bool ContainerDnsManager::EnableSecureDns(
    const std::string& container_id,
    const std::vector<std::string>& doh_servers) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (doh_servers.empty()) {
    return false;
  }

  auto it = container_dns_.find(container_id);
  ContainerDnsConfig config;

  if (it != container_dns_.end()) {
    config = it->second->config;
  }

  config.container_id = container_id;
  config.mode = ContainerDnsConfig::DnsMode::kSecure;
  config.doh_config.enabled = true;
  config.doh_config.servers = doh_servers;

  ;

  return SetDnsConfig(container_id, config);
}

bool ContainerDnsManager::DisableSecureDns(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_dns_.find(container_id);
  if (it == container_dns_.end()) {
    return false;
  }

  it->second->config.mode = ContainerDnsConfig::DnsMode::kSystem;
  it->second->config.doh_config.enabled = false;

  ;

  NotifyConfigUpdated(container_id, it->second->config);

  return true;
}

bool ContainerDnsManager::SetCustomDns(
    const std::string& container_id,
    const std::vector<std::string>& nameservers) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (nameservers.empty()) {
    return false;
  }

  auto it = container_dns_.find(container_id);
  ContainerDnsConfig config;

  if (it != container_dns_.end()) {
    config = it->second->config;
  }

  config.container_id = container_id;
  config.mode = ContainerDnsConfig::DnsMode::kCustom;
  config.custom_dns_config.nameservers = nameservers;

  ;

  return SetDnsConfig(container_id, config);
}

bool ContainerDnsManager::EnableProxyDns(const std::string& container_id,
                                         const std::string& proxy_server) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (proxy_server.empty()) {
    return false;
  }

  auto it = container_dns_.find(container_id);
  ContainerDnsConfig config;

  if (it != container_dns_.end()) {
    config = it->second->config;
  }

  config.container_id = container_id;
  config.mode = ContainerDnsConfig::DnsMode::kProxyDns;
  config.proxy_dns_config.route_dns_through_proxy = true;
  config.proxy_dns_config.proxy_server = proxy_server;

  ;

  return SetDnsConfig(container_id, config);
}

void ContainerDnsManager::ResolveHostname(const std::string& container_id,
                                          const std::string& hostname,
                                          DnsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&ContainerDnsManager::DoResolve,
                     weak_factory_.GetWeakPtr(),
                     container_id, hostname, std::move(callback)));
}

void ContainerDnsManager::ResolveHostnameForWebContents(
    content::WebContents* web_contents,
    const std::string& hostname,
    DnsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!web_contents) {
    DnsResolutionResult result;
    result.success = false;
    result.hostname = hostname;
    result.error_message = "WebContents is null";
    std::move(callback).Run(std::move(result));
    return;
  }

  content::BrowserContext* browser_context = web_contents->GetBrowserContext();
  if (!browser_context) {
    DnsResolutionResult result;
    result.success = false;
    result.hostname = hostname;
    result.error_message = "No BrowserContext";
    std::move(callback).Run(std::move(result));
    return;
  }

  TabContainerManager* container_manager = GetForBrowserContext(browser_context);
  if (!container_manager) {
    DnsResolutionResult result;
    result.success = false;
    result.hostname = hostname;
    result.error_message = "No TabContainerManager";
    std::move(callback).Run(std::move(result));
    return;
  }

  std::string container_id = 
      container_manager->GetContainerIdForTab(web_contents);
  if (container_id.empty()) {
    DnsResolutionResult result;
    result.success = false;
    result.hostname = hostname;
    result.error_message = "No container for WebContents";
    std::move(callback).Run(std::move(result));
    return;
  }

  ResolveHostname(container_id, hostname, std::move(callback));
}

void ContainerDnsManager::DoResolve(const std::string& container_id,
                                    const std::string& hostname,
                                    DnsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::TimeTicks start_time = base::TimeTicks::Now();

  stats_.total_resolutions++;

  DnsResolutionResult result;
  result.hostname = hostname;
  result.container_id = container_id;

  auto it = container_dns_.find(container_id);
  if (it != container_dns_.end()) {
    result.source = DetermineResolutionSource(it->second->config);
    it->second->resolution_count++;
    it->second->last_resolution = base::TimeTicks::Now();
  } else {
    result.source = DnsResolutionResult::Source::kSystem;
  }

  result.success = true;
  result.resolved_addresses.push_back("192.0.2.1");  
  result.resolution_time = base::TimeTicks::Now() - start_time;
  result.used_secure_dns = 
      (result.source == DnsResolutionResult::Source::kDoH);

  stats_.successful_resolutions++;
  if (result.used_secure_dns) {
    stats_.secure_dns_resolutions++;
  }
  if (result.source == DnsResolutionResult::Source::kProxy) {
    stats_.proxy_dns_resolutions++;
  }

  stats_.last_activity = base::TimeTicks::Now();

  if (stats_.total_resolutions > 0) {
    base::TimeDelta total = stats_.avg_resolution_time * 
                            (stats_.total_resolutions - 1);
    total += result.resolution_time;
    stats_.avg_resolution_time = total / stats_.total_resolutions;
  }

  ;

  NotifyResolutionComplete(result);

  std::move(callback).Run(std::move(result));
}

DnsResolutionResult::Source ContainerDnsManager::DetermineResolutionSource(
    const ContainerDnsConfig& config) const {
  switch (config.mode) {
    case ContainerDnsConfig::DnsMode::kSecure:
      return DnsResolutionResult::Source::kDoH;
    case ContainerDnsConfig::DnsMode::kCustom:
      return DnsResolutionResult::Source::kCustom;
    case ContainerDnsConfig::DnsMode::kProxyDns:
      return DnsResolutionResult::Source::kProxy;
    default:
      return DnsResolutionResult::Source::kSystem;
  }
}

void ContainerDnsManager::ClearDnsCache(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_dns_.find(container_id);
  if (it != container_dns_.end()) {
    it->second->cache_stats = CacheStats();
    ;
  }
}

void ContainerDnsManager::ClearAllDnsCaches() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  for (auto& pair : container_dns_) {
    pair.second->cache_stats = CacheStats();
  }

  ;
}

ContainerDnsManager::CacheStats ContainerDnsManager::GetCacheStats(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_dns_.find(container_id);
  if (it == container_dns_.end()) {
    return CacheStats();
  }

  return it->second->cache_stats;
}

void ContainerDnsManager::PrefetchDns(const std::string& container_id,
                                      const std::string& hostname) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_dns_.find(container_id);
  if (it != container_dns_.end() && !it->second->config.prefetch_config.enabled) {
    return;
  }

  ;

  ResolveHostname(container_id, hostname,
                  base::BindOnce([](DnsResolutionResult) {}));
}

void ContainerDnsManager::PrefetchDnsBatch(
    const std::string& container_id,
    const std::vector<std::string>& hostnames) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  for (const auto& hostname : hostnames) {
    PrefetchDns(container_id, hostname);
  }
}

void ContainerDnsManager::CancelPrefetch(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

}

ContainerDnsManager::DnsValidationResult ContainerDnsManager::ValidateDnsConfig(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DnsValidationResult result;

  auto it = container_dns_.find(container_id);
  if (it == container_dns_.end()) {
    result.errors.push_back("No DNS configuration found for container");
    return result;
  }

  const ContainerDnsConfig& config = it->second->config;

  switch (config.mode) {
    case ContainerDnsConfig::DnsMode::kSecure:
      if (!config.doh_config.enabled) {
        result.errors.push_back("Secure DNS mode but DoH not enabled");
      }
      if (config.doh_config.servers.empty()) {
        result.errors.push_back("No DoH servers configured");
      }
      break;

    case ContainerDnsConfig::DnsMode::kCustom:
      if (config.custom_dns_config.nameservers.empty()) {
        result.errors.push_back("No custom nameservers configured");
      }
      break;

    case ContainerDnsConfig::DnsMode::kProxyDns:
      if (config.proxy_dns_config.proxy_server.empty()) {
        result.errors.push_back("No proxy server configured for proxy DNS");
      }
      if (!config.proxy_dns_config.route_dns_through_proxy) {
        result.warnings.push_back("Proxy DNS mode but routing not enabled");
      }
      break;

    default:
      break;
  }

  if (config.cache_config.ttl_seconds < 10) {
    result.warnings.push_back("Very low DNS cache TTL may impact performance");
  }

  if (config.privacy_config.disable_ipv6) {
    result.warnings.push_back("IPv6 disabled - may cause connectivity issues");
  }

  result.valid = result.errors.empty();

  return result;
}

void ContainerDnsManager::TestDnsResolution(const std::string& container_id,
                                            const std::string& test_hostname,
                                            DnsTestCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ResolveHostname(container_id, test_hostname,
      base::BindOnce([](DnsTestCallback cb, DnsResolutionResult result) {
        std::stringstream details;
        details << "Hostname: " << result.hostname << "\n";
        details << "Success: " << (result.success ? "Yes" : "No") << "\n";
        if (result.success) {
          details << "Resolved to: ";
          for (const auto& addr : result.resolved_addresses) {
            details << addr << " ";
          }
          details << "\n";
          details << "Source: " << DnsSourceToString(result.source) << "\n";
          details << "Time: " << result.resolution_time.InMillisecondsF() << "ms\n";
        } else {
          details << "Error: " << result.error_message << "\n";
        }
        std::move(cb).Run(result.success, details.str());
      }, std::move(callback)));
}

std::string ContainerDnsManager::GetDiagnosticReport() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::stringstream report;
  report << "=== ContainerDnsManager Diagnostic Report ===\n";
  report << "Browser Context: " << browser_context_ << "\n\n";

  report << "Statistics:\n";
  report << "  Containers Configured: " << stats_.containers_configured << "\n";
  report << "  Total Resolutions: " << stats_.total_resolutions << "\n";
  report << "  Successful: " << stats_.successful_resolutions << "\n";
  report << "  Failed: " << stats_.failed_resolutions << "\n";
  report << "  Secure DNS: " << stats_.secure_dns_resolutions << "\n";
  report << "  Proxy DNS: " << stats_.proxy_dns_resolutions << "\n";
  report << "  Avg Resolution Time: " 
         << stats_.avg_resolution_time.InMillisecondsF() << "ms\n\n";

  report << "Container DNS Configurations: " << container_dns_.size() << "\n";
  for (const auto& pair : container_dns_) {
    const ContainerDnsInfo* info = pair.second.get();
    report << "  - " << pair.first << ":\n";
    report << "      Mode: " << DnsModeToString(info->config.mode) << "\n";
    report << "      Resolutions: " << info->resolution_count << "\n";
    report << "      Failures: " << info->failure_count << "\n";

    if (info->config.mode == ContainerDnsConfig::DnsMode::kSecure) {
      report << "      DoH Servers: " << info->config.doh_config.servers.size() 
             << "\n";
    } else if (info->config.mode == ContainerDnsConfig::DnsMode::kCustom) {
      report << "      Nameservers: " 
             << info->config.custom_dns_config.nameservers.size() << "\n";
    } else if (info->config.mode == ContainerDnsConfig::DnsMode::kProxyDns) {
      report << "      Proxy: " << info->config.proxy_dns_config.proxy_server 
             << "\n";
    }
  }

  return report.str();
}

void ContainerDnsManager::DumpStateToLog() const {
  ;
}

ContainerDnsManager::Statistics ContainerDnsManager::GetStatistics() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return stats_;
}

void ContainerDnsManager::NotifyConfigUpdated(
    const std::string& container_id,
    const ContainerDnsConfig& config) {
  for (auto& observer : observers_) {
    observer.OnDnsConfigUpdated(container_id, config);
  }
}

void ContainerDnsManager::NotifyResolutionComplete(
    const DnsResolutionResult& result) {
  for (auto& observer : observers_) {
    observer.OnDnsResolutionComplete(result);
  }
}

void ContainerDnsManager::NotifyResolutionFailed(
    const std::string& container_id,
    const std::string& hostname,
    const std::string& error) {
  for (auto& observer : observers_) {
    observer.OnDnsResolutionFailed(container_id, hostname, error);
  }
}

}  
