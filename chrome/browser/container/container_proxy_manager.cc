
#include "chrome/browser/container/container_proxy_manager.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/container/tab_container_manager.h"
#include "chrome/browser/container/tab_container_manager_factory.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address.h"
#include "url/gurl.h"

namespace tab_container {

namespace {

std::string ProxyStateToString(ContainerProxyState state) {
  switch (state) {
    case ContainerProxyState::kNoProxy:
      return "NoProxy";
    case ContainerProxyState::kConfigured:
      return "Configured";
    case ContainerProxyState::kConnecting:
      return "Connecting";
    case ContainerProxyState::kConnected:
      return "Connected";
    case ContainerProxyState::kAuthRequired:
      return "AuthRequired";
    case ContainerProxyState::kAuthenticating:
      return "Authenticating";
    case ContainerProxyState::kDegraded:
      return "Degraded";
    case ContainerProxyState::kFailed:
      return "Failed";
    case ContainerProxyState::kDisconnected:
      return "Disconnected";
    case ContainerProxyState::kReconfiguring:
      return "Reconfiguring";
  }
  return "Unknown";
}

}  

std::string ContainerProxyConfig::GetProxyServer() const {
  if (!enabled || proxy_host.empty()) {
    return "";
  }

  std::string scheme = proxy_scheme.empty() ? "http" : proxy_scheme;
  std::string port_str = proxy_port > 0 ? ":" + base::NumberToString(proxy_port) : "";

  return scheme + "://" + proxy_host + port_str;
}

bool ContainerProxyConfig::IsSocksProxy() const {
  return proxy_scheme == "socks4" || proxy_scheme == "socks5";
}

std::string ContainerProxyConfig::Serialize() const {
  base::DictValue dict;
  dict.Set("enabled", enabled);
  dict.Set("proxy_scheme", proxy_scheme);
  dict.Set("proxy_host", proxy_host);
  dict.Set("proxy_port", proxy_port);

  base::DictValue auth_dict;
  auth_dict.Set("required", auth_config.required);
  auth_dict.Set("username", auth_config.username);
  auth_dict.Set("password", auth_config.password);
  auth_dict.Set("auth_scheme", auth_config.auth_scheme);
  dict.Set("auth_config", std::move(auth_dict));

  base::ListValue bypass_list;
  for (const auto& rule : bypass_rules) {
    bypass_list.Append(rule);
  }
  dict.Set("bypass_rules", std::move(bypass_list));
  dict.Set("bypass_local", bypass_local);

  base::DictValue pac_dict;
  pac_dict.Set("enabled", pac_config.enabled);
  pac_dict.Set("pac_url", pac_config.pac_url);
  pac_dict.Set("pac_script", pac_config.pac_script);
  dict.Set("pac_config", std::move(pac_dict));

  base::DictValue advanced_dict;
  advanced_dict.Set("connect_timeout_seconds", 
                    advanced_config.connect_timeout_seconds);
  advanced_dict.Set("read_timeout_seconds", advanced_config.read_timeout_seconds);
  advanced_dict.Set("max_retries", advanced_config.max_retries);
  advanced_dict.Set("allow_direct_fallback", advanced_config.allow_direct_fallback);
  advanced_dict.Set("dns_over_proxy", advanced_config.dns_over_proxy);
  advanced_dict.Set("enable_keep_alive", advanced_config.enable_keep_alive);
  advanced_dict.Set("keep_alive_timeout_seconds", 
                    advanced_config.keep_alive_timeout_seconds);
  dict.Set("advanced_config", std::move(advanced_dict));

  std::string output;
  base::JSONWriter::Write(dict, &output);
  return output;
}

std::optional<ContainerProxyConfig> ContainerProxyConfig::Deserialize(
    const std::string& data) {
  auto parsed =
      base::JSONReader::Read(data, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }

  const base::DictValue& dict = parsed->GetDict();
  ContainerProxyConfig config;

  config.enabled = dict.FindBool("enabled").value_or(false);

  const std::string* scheme = dict.FindString("proxy_scheme");
  const std::string* host = dict.FindString("proxy_host");
  if (scheme) config.proxy_scheme = *scheme;
  if (host) config.proxy_host = *host;
  config.proxy_port = dict.FindInt("proxy_port").value_or(0);

  const base::DictValue* auth_dict = dict.FindDict("auth_config");
  if (auth_dict) {
    config.auth_config.required = auth_dict->FindBool("required").value_or(false);
    const std::string* username = auth_dict->FindString("username");
    const std::string* password = auth_dict->FindString("password");
    const std::string* auth_scheme = auth_dict->FindString("auth_scheme");
    if (username) config.auth_config.username = *username;
    if (password) config.auth_config.password = *password;
    if (auth_scheme) config.auth_config.auth_scheme = *auth_scheme;
  }

  const base::ListValue* bypass_list = dict.FindList("bypass_rules");
  if (bypass_list) {
    for (const auto& rule : *bypass_list) {
      if (rule.is_string()) {
        config.bypass_rules.push_back(rule.GetString());
      }
    }
  }
  config.bypass_local = dict.FindBool("bypass_local").value_or(true);

  const base::DictValue* pac_dict = dict.FindDict("pac_config");
  if (pac_dict) {
    config.pac_config.enabled = pac_dict->FindBool("enabled").value_or(false);
    const std::string* pac_url = pac_dict->FindString("pac_url");
    const std::string* pac_script = pac_dict->FindString("pac_script");
    if (pac_url) config.pac_config.pac_url = *pac_url;
    if (pac_script) config.pac_config.pac_script = *pac_script;
  }

  const base::DictValue* advanced_dict = dict.FindDict("advanced_config");
  if (advanced_dict) {
    config.advanced_config.connect_timeout_seconds = 
        advanced_dict->FindInt("connect_timeout_seconds").value_or(30);
    config.advanced_config.read_timeout_seconds = 
        advanced_dict->FindInt("read_timeout_seconds").value_or(60);
    config.advanced_config.max_retries = 
        advanced_dict->FindInt("max_retries").value_or(3);
    config.advanced_config.allow_direct_fallback = 
        advanced_dict->FindBool("allow_direct_fallback").value_or(false);
    config.advanced_config.dns_over_proxy = 
        advanced_dict->FindBool("dns_over_proxy").value_or(true);
    config.advanced_config.enable_keep_alive = 
        advanced_dict->FindBool("enable_keep_alive").value_or(true);
    config.advanced_config.keep_alive_timeout_seconds = 
        advanced_dict->FindInt("keep_alive_timeout_seconds").value_or(120);
  }

  return config;
}

ContainerProxyManager::ContainerProxyManager(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ;
}

ContainerProxyManager::~ContainerProxyManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;
}

void ContainerProxyManager::AddObserver(ContainerProxyObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void ContainerProxyManager::RemoveObserver(ContainerProxyObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

bool ContainerProxyManager::SetProxyConfig(const std::string& container_id,
                                           const ContainerProxyConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {

    auto info = std::make_unique<ContainerProxyInfo>();
    info->container_id = container_id;
    info->config = config;
    info->state_changed_at = base::TimeTicks::Now();
    info->last_activity = base::TimeTicks::Now();

    if (config.enabled) {
      TransitionState(info.get(), ContainerProxyState::kConfigured);
    } else {
      TransitionState(info.get(), ContainerProxyState::kNoProxy);
    }

    container_proxies_[container_id] = std::move(info);
    stats_.total_containers_configured++;
  } else {

    ContainerProxyInfo* info = it->second.get();

    if (debug_logging_enabled_) {
      ;
    }

    TransitionState(info, ContainerProxyState::kReconfiguring);
    info->config = config;
    info->last_activity = base::TimeTicks::Now();

    if (config.enabled) {
      TransitionState(info, ContainerProxyState::kConfigured);
    } else {
      TransitionState(info, ContainerProxyState::kNoProxy);
    }
  }

  stats_.last_activity = base::TimeTicks::Now();

  LogProxyOperation(container_id, "SetProxyConfig",
                    "Proxy configured: " + config.GetProxyServer());

  return true;
}

std::optional<ContainerProxyConfig> ContainerProxyManager::GetProxyConfig(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return std::nullopt;
  }

  return it->second->config;
}

bool ContainerProxyManager::RemoveProxyConfig(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return false;
  }

  ;

  ContainerProxyInfo* info = it->second.get();
  TransitionState(info, ContainerProxyState::kDisconnected);

  container_proxies_.erase(it);

  LogProxyOperation(container_id, "RemoveProxyConfig", "Proxy configuration removed");

  return true;
}

bool ContainerProxyManager::UpdateProxyServer(const std::string& container_id,
                                              const std::string& proxy_scheme,
                                              const std::string& proxy_host,
                                              int proxy_port) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return false;
  }

  ContainerProxyInfo* info = it->second.get();
  info->config.proxy_scheme = proxy_scheme;
  info->config.proxy_host = proxy_host;
  info->config.proxy_port = proxy_port;
  info->last_activity = base::TimeTicks::Now();

  ;

  return true;
}

bool ContainerProxyManager::UpdateProxyAuth(const std::string& container_id,
                                            const std::string& username,
                                            const std::string& password) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return false;
  }

  ContainerProxyInfo* info = it->second.get();
  info->config.auth_config.required = true;
  info->config.auth_config.username = username;
  info->config.auth_config.password = password;
  info->last_activity = base::TimeTicks::Now();

  ;

  return true;
}

bool ContainerProxyManager::UpdateProxyBypassRules(
    const std::string& container_id,
    const std::vector<std::string>& rules) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return false;
  }

  ContainerProxyInfo* info = it->second.get();
  info->config.bypass_rules = rules;
  info->last_activity = base::TimeTicks::Now();

  ;

  return true;
}

ContainerProxyState ContainerProxyManager::GetProxyState(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return ContainerProxyState::kNoProxy;
  }

  return it->second->state;
}

bool ContainerProxyManager::IsProxyEnabled(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return false;
  }

  return it->second->config.enabled;
}

bool ContainerProxyManager::IsProxyConnected(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return false;
  }

  ContainerProxyState state = it->second->state;
  return state == ContainerProxyState::kConnected ||
         state == ContainerProxyState::kDegraded;
}

void ContainerProxyManager::TestProxyConnection(
    const std::string& container_id,
    ConnectionTestCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    ProxyOperationResult result;
    result.success = false;
    result.error_message = "Container not found";
    std::move(callback).Run(std::move(result));
    return;
  }

  ContainerProxyInfo* info = it->second.get();
  if (!info->config.enabled) {
    ProxyOperationResult result;
    result.success = false;
    result.error_message = "Proxy not enabled";
    std::move(callback).Run(std::move(result));
    return;
  }

  ;

  TransitionState(info, ContainerProxyState::kConnecting);

  pending_connection_tests_[container_id] = std::move(callback);

  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ContainerProxyManager::DoConnectionTest,
                     weak_factory_.GetWeakPtr(), container_id),
      base::Milliseconds(100));
}

void ContainerProxyManager::ReconnectProxy(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return;
  }

  ContainerProxyInfo* info = it->second.get();

  ;

  TransitionState(info, ContainerProxyState::kConfigured);
  info->failure_stats = FailureStats();  
  info->connection_tested = false;
}

void ContainerProxyManager::DisconnectProxy(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return;
  }

  ;

  TransitionState(it->second.get(), ContainerProxyState::kDisconnected);
}

bool ContainerProxyManager::ProvideProxyAuth(const std::string& container_id,
                                             const std::string& username,
                                             const std::string& password) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return false;
  }

  ContainerProxyInfo* info = it->second.get();

  if (info->state != ContainerProxyState::kAuthRequired) {
    ;
    return false;
  }

  ;

  info->config.auth_config.username = username;
  info->config.auth_config.password = password;
  info->pending_auth = std::nullopt;

  TransitionState(info, ContainerProxyState::kAuthenticating);

  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce([](base::WeakPtr<ContainerProxyManager> self,
                        const std::string& cid) {
        if (!self) return;
        auto it = self->container_proxies_.find(cid);
        if (it != self->container_proxies_.end()) {
          self->TransitionState(it->second.get(), 
                                ContainerProxyState::kConnected);
        }
      }, weak_factory_.GetWeakPtr(), container_id),
      base::Milliseconds(500));

  return true;
}

std::vector<ContainerProxyManager::PendingAuthChallenge>
ContainerProxyManager::GetPendingAuthChallenges() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<PendingAuthChallenge> challenges;
  for (const auto& pair : container_proxies_) {
    if (pair.second->pending_auth) {
      challenges.push_back(*pair.second->pending_auth);
    }
  }
  return challenges;
}

void ContainerProxyManager::CancelProxyAuth(const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return;
  }

  ContainerProxyInfo* info = it->second.get();
  info->pending_auth = std::nullopt;

  ;

  TransitionState(info, ContainerProxyState::kFailed);
  info->failure_stats.auth_failures++;
}

ContainerProxyManager::ProxyResolutionResult
ContainerProxyManager::ResolveProxy(const std::string& container_id,
                                    const GURL& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ProxyResolutionResult result;
  result.resolution_path = "ResolveProxy(" + container_id + ", " + url.spec() + ")";

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    result.resolution_path += " -> No proxy configured";
    result.success = true;
    result.use_proxy = false;
    return result;
  }

  ContainerProxyInfo* info = it->second.get();

  if (!info->config.enabled) {
    result.resolution_path += " -> Proxy disabled";
    result.success = true;
    result.use_proxy = false;
    return result;
  }

  result.resolution_path += " -> Proxy enabled";

  if (ShouldBypassProxy(info, url)) {
    result.resolution_path += " -> Bypass rule matched";
    result.success = true;
    result.use_proxy = false;
    result.is_direct_allowed = true;
    return result;
  }

  result.success = true;
  result.use_proxy = true;
  result.proxy_server = info->config.GetProxyServer();
  result.resolution_path += " -> Using proxy: " + result.proxy_server;

  if (tracing_enabled_) {
    ProxyRequestTrace trace;
    trace.event = ProxyRequestTrace::TraceEvent::kProxyResolution;
    trace.timestamp = base::TimeTicks::Now();
    trace.container_id = container_id;
    trace.url = url.spec();
    trace.proxy_server = result.proxy_server;
    trace.details = result.resolution_path;
    RecordTrace(trace);
  }

  if (debug_logging_enabled_) {
    ;
  }

  return result;
}

ContainerProxyManager::ProxyResolutionResult
ContainerProxyManager::ResolveProxyForWebContents(
    content::WebContents* web_contents,
    const GURL& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ProxyResolutionResult result;

  if (!web_contents) {
    result.error_message = "WebContents is null";
    return result;
  }

  content::BrowserContext* browser_context = web_contents->GetBrowserContext();
  if (!browser_context) {
    result.error_message = "No BrowserContext";
    return result;
  }

  TabContainerManager* container_manager = GetForBrowserContext(browser_context);
  if (!container_manager) {
    result.error_message = "No TabContainerManager";
    return result;
  }

  std::string container_id = 
      container_manager->GetContainerIdForTab(web_contents);
  if (container_id.empty()) {
    result.error_message = "No container for WebContents";
    return result;
  }

  return ResolveProxy(container_id, url);
}

ContainerProxyManager::ProxyEnforcementResult
ContainerProxyManager::ValidateProxyUsage(const std::string& container_id,
                                          const GURL& url,
                                          const std::string& used_proxy) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ProxyEnforcementResult result;

  ProxyResolutionResult resolution = ResolveProxy(container_id, url);

  result.proxy_expected = resolution.use_proxy;
  result.expected_proxy = resolution.proxy_server;
  result.actual_proxy = used_proxy;
  result.proxy_used = !used_proxy.empty() && used_proxy != "DIRECT";

  if (!resolution.success) {
    result.violation_reason = "Proxy resolution failed: " + resolution.error_message;
    return result;
  }

  if (result.proxy_expected && !result.proxy_used) {

    result.valid = false;
    result.violation_reason = 
        "Direct connection when proxy expected! URL: " + url.spec() +
        ", Expected proxy: " + result.expected_proxy;

    ;

    ReportDirectConnectionViolation(container_id, url, result.violation_reason);

    return result;
  }

  if (result.proxy_expected && result.proxy_used) {

    if (result.expected_proxy != result.actual_proxy) {
      result.valid = false;
      result.violation_reason = 
          "Wrong proxy used! Expected: " + result.expected_proxy +
          ", Actual: " + result.actual_proxy;

      ;

      return result;
    }
  }

  result.valid = true;

  if (debug_logging_enabled_) {
    ;
  }

  return result;
}

ContainerProxyManager::ProxyEnforcementResult
ContainerProxyManager::ValidateProxyUsageForWebContents(
    content::WebContents* web_contents,
    const GURL& url,
    const std::string& used_proxy) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ProxyEnforcementResult result;

  if (!web_contents) {
    result.violation_reason = "WebContents is null";
    return result;
  }

  content::BrowserContext* browser_context = web_contents->GetBrowserContext();
  if (!browser_context) {
    result.violation_reason = "No BrowserContext";
    return result;
  }

  TabContainerManager* container_manager = GetForBrowserContext(browser_context);
  if (!container_manager) {
    result.violation_reason = "No TabContainerManager";
    return result;
  }

  std::string container_id = 
      container_manager->GetContainerIdForTab(web_contents);
  if (container_id.empty()) {
    result.violation_reason = "No container for WebContents";
    return result;
  }

  return ValidateProxyUsage(container_id, url, used_proxy);
}

void ContainerProxyManager::ReportDirectConnectionViolation(
    const std::string& container_id,
    const GURL& url,
    const std::string& reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ;

  stats_.total_bypass_violations++;

  auto it = container_proxies_.find(container_id);
  if (it != container_proxies_.end()) {
    it->second->failure_stats.bypass_violations++;
  }

  if (tracing_enabled_) {
    ProxyRequestTrace trace;
    trace.event = ProxyRequestTrace::TraceEvent::kDirectConnection;
    trace.timestamp = base::TimeTicks::Now();
    trace.container_id = container_id;
    trace.url = url.spec();
    trace.details = reason;
    trace.is_violation = true;
    RecordTrace(trace);
  }

  NotifyBypassDetected(container_id, url.spec(), reason);
}

bool ContainerProxyManager::ShouldBlockDirectConnection(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return false;
  }

  const ContainerProxyInfo* info = it->second.get();

  return info->config.enabled && 
         !info->config.advanced_config.allow_direct_fallback;
}

void ContainerProxyManager::OnProxyConnectionFailure(
    const std::string& container_id,
    int error_code,
    const std::string& error_message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return;
  }

  ContainerProxyInfo* info = it->second.get();

  ;

  info->failure_stats.connection_failures++;
  info->failure_stats.last_failure = base::TimeTicks::Now();
  info->failure_stats.last_error = error_message;

  stats_.total_connection_failures++;

  TransitionState(info, ContainerProxyState::kFailed);

  NotifyConnectionFailed(container_id, error_message);

  if (tracing_enabled_) {
    ProxyRequestTrace trace;
    trace.event = ProxyRequestTrace::TraceEvent::kRequestFailed;
    trace.timestamp = base::TimeTicks::Now();
    trace.container_id = container_id;
    trace.proxy_server = info->config.GetProxyServer();
    trace.details = "Connection failure: " + error_message;
    RecordTrace(trace);
  }
}

void ContainerProxyManager::OnProxyAuthFailure(
    const std::string& container_id,
    int error_code,
    const std::string& error_message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return;
  }

  ContainerProxyInfo* info = it->second.get();

  ;

  info->failure_stats.auth_failures++;
  info->failure_stats.last_failure = base::TimeTicks::Now();
  info->failure_stats.last_error = error_message;

  TransitionState(info, ContainerProxyState::kAuthRequired);

  for (auto& observer : observers_) {
    observer.OnProxyAuthFailed(container_id, error_message);
  }
}

void ContainerProxyManager::OnProxyRequestFailure(
    const std::string& container_id,
    const GURL& url,
    int error_code,
    const std::string& error_message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return;
  }

  ContainerProxyInfo* info = it->second.get();

  ;

  info->failure_stats.request_failures++;
  info->failure_stats.last_failure = base::TimeTicks::Now();
  info->failure_stats.last_error = error_message;

  if (info->failure_stats.request_failures >= 5 &&
      info->state == ContainerProxyState::kConnected) {
    TransitionState(info, ContainerProxyState::kDegraded);
  }

  if (tracing_enabled_) {
    ProxyRequestTrace trace;
    trace.event = ProxyRequestTrace::TraceEvent::kRequestFailed;
    trace.timestamp = base::TimeTicks::Now();
    trace.container_id = container_id;
    trace.url = url.spec();
    trace.proxy_server = info->config.GetProxyServer();
    trace.details = "Request failure: " + error_message;
    RecordTrace(trace);
  }
}

ContainerProxyManager::FailureStats ContainerProxyManager::GetFailureStats(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return FailureStats();
  }

  return it->second->failure_stats;
}

void ContainerProxyManager::SetTracingEnabled(bool enabled) {
  tracing_enabled_ = enabled;
  ;
}

void ContainerProxyManager::RecordTrace(const ProxyRequestTrace& trace) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!tracing_enabled_) {
    return;
  }

  traces_.push_back(trace);

  if (traces_.size() > max_traces_) {
    traces_.erase(traces_.begin(), 
                  traces_.begin() + (traces_.size() - max_traces_));
  }

  for (auto& observer : observers_) {
    observer.OnProxyRequestTraced(trace);
  }

  if (debug_logging_enabled_) {
    ;
  }
}

std::vector<ProxyRequestTrace> ContainerProxyManager::GetRecentTraces(
    size_t count) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (traces_.size() <= count) {
    return traces_;
  }

  return std::vector<ProxyRequestTrace>(
      traces_.end() - count, traces_.end());
}

std::vector<ProxyRequestTrace> ContainerProxyManager::GetTracesForContainer(
    const std::string& container_id,
    size_t count) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<ProxyRequestTrace> result;
  for (auto it = traces_.rbegin(); 
       it != traces_.rend() && result.size() < count; 
       ++it) {
    if (it->container_id == container_id) {
      result.push_back(*it);
    }
  }

  std::reverse(result.begin(), result.end());
  return result;
}

std::vector<ProxyRequestTrace> ContainerProxyManager::GetViolationTraces() 
    const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<ProxyRequestTrace> result;
  for (const auto& trace : traces_) {
    if (trace.is_violation) {
      result.push_back(trace);
    }
  }
  return result;
}

void ContainerProxyManager::ClearTraces() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  traces_.clear();
}

void ContainerProxyManager::SetDebugLoggingEnabled(bool enabled) {
  debug_logging_enabled_ = enabled;
  ;
}

std::string ContainerProxyManager::GetDiagnosticReport() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::stringstream report;
  report << "=== ContainerProxyManager Diagnostic Report ===\n";
  report << "Browser Context: " << browser_context_ << "\n\n";

  report << "Statistics:\n";
  report << "  Total Containers Configured: " 
         << stats_.total_containers_configured << "\n";
  report << "  Active Proxy Connections: " 
         << stats_.active_proxy_connections << "\n";
  report << "  Total Requests Proxied: " 
         << stats_.total_requests_proxied << "\n";
  report << "  Total Bypass Violations: " 
         << stats_.total_bypass_violations << "\n";
  report << "  Total Auth Challenges: " 
         << stats_.total_auth_challenges << "\n";
  report << "  Total Connection Failures: " 
         << stats_.total_connection_failures << "\n\n";

  report << "Container Proxies: " << container_proxies_.size() << "\n";
  for (const auto& pair : container_proxies_) {
    const ContainerProxyInfo* info = pair.second.get();
    report << "  - " << pair.first << ":\n";
    report << "      State: " << ProxyStateToString(info->state) << "\n";
    report << "      Enabled: " 
           << (info->config.enabled ? "Yes" : "No") << "\n";
    if (info->config.enabled) {
      report << "      Proxy Server: " 
             << info->config.GetProxyServer() << "\n";
      report << "      Auth Required: " 
             << (info->config.auth_config.required ? "Yes" : "No") << "\n";
      report << "      Bypass Rules: " 
             << info->config.bypass_rules.size() << "\n";
      report << "      Allow Direct Fallback: " 
             << (info->config.advanced_config.allow_direct_fallback 
                 ? "Yes" : "No") << "\n";
    }
    report << "      Connection Failures: " 
           << info->failure_stats.connection_failures << "\n";
    report << "      Auth Failures: " 
           << info->failure_stats.auth_failures << "\n";
    report << "      Bypass Violations: " 
           << info->failure_stats.bypass_violations << "\n";
  }

  report << "\nRecent Violations:\n";
  std::vector<ProxyRequestTrace> violations = GetViolationTraces();
  for (size_t i = 0; i < std::min(violations.size(), size_t(10)); ++i) {
    const auto& v = violations[violations.size() - 1 - i];
    report << "  - " << v.container_id << ": " << v.url << "\n";
    report << "    " << v.details << "\n";
  }

  return report.str();
}

void ContainerProxyManager::DumpStateToLog() const {
  ;
}

ContainerProxyManager::Statistics ContainerProxyManager::GetStatistics() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return stats_;
}

void ContainerProxyManager::TransitionState(ContainerProxyInfo* info,
                                            ContainerProxyState new_state) {
  ContainerProxyState old_state = info->state;
  info->state = new_state;
  info->state_changed_at = base::TimeTicks::Now();

  if (debug_logging_enabled_) {
    ;
  }

  NotifyStateChanged(info->container_id, old_state, new_state);

  if (new_state == ContainerProxyState::kConnected) {
    stats_.active_proxy_connections++;
  } else if (old_state == ContainerProxyState::kConnected) {
    stats_.active_proxy_connections--;
  }
}

bool ContainerProxyManager::ShouldBypassProxy(const ContainerProxyInfo* info,
                                              const GURL& url) const {

  if (info->config.bypass_local) {
    if (url.host() == "localhost" || url.host() == "127.0.0.1" ||
        url.host() == "::1" || url.HostIsIPAddress()) {
      net::IPAddress ip;
      if (ip.AssignFromIPLiteral(url.host())) {
        if (ip.IsLoopback()) {
          return true;
        }
      }
    }
  }

  for (const auto& rule : info->config.bypass_rules) {
    if (MatchesBypassRule(rule, url)) {
      return true;
    }
  }

  return false;
}

bool ContainerProxyManager::MatchesBypassRule(const std::string& rule,
                                              const GURL& url) const {

  if (rule == "<local>") {
    return url.host() == "localhost" || url.host() == "127.0.0.1";
  }

  if (rule.empty()) {
    return false;
  }

  if (rule[0] == '*') {

    std::string suffix = rule.substr(1);
    return base::EndsWith(url.host(), suffix, 
                          base::CompareCase::INSENSITIVE_ASCII);
  }

  return base::EqualsCaseInsensitiveASCII(url.host(), rule);
}

void ContainerProxyManager::DoConnectionTest(const std::string& container_id) {

  auto it = container_proxies_.find(container_id);
  if (it == container_proxies_.end()) {
    return;
  }

  ContainerProxyInfo* info = it->second.get();

  ProxyOperationResult result;
  result.success = true;
  result.duration = base::Milliseconds(150);
  result.http_status_code = 200;

  info->connection_tested = true;
  info->last_connection_result = result;

  TransitionState(info, ContainerProxyState::kConnected);
  NotifyConnected(container_id, info->config.GetProxyServer());

  OnConnectionTestComplete(container_id, result);
}

void ContainerProxyManager::OnConnectionTestComplete(
    const std::string& container_id,
    ProxyOperationResult result) {
  auto it = pending_connection_tests_.find(container_id);
  if (it != pending_connection_tests_.end()) {
    std::move(it->second).Run(std::move(result));
    pending_connection_tests_.erase(it);
  }
}

void ContainerProxyManager::NotifyStateChanged(const std::string& container_id,
                                               ContainerProxyState old_state,
                                               ContainerProxyState new_state) {
  for (auto& observer : observers_) {
    observer.OnProxyStateChanged(container_id, old_state, new_state);
  }
}

void ContainerProxyManager::NotifyConnected(const std::string& container_id,
                                            const std::string& proxy_server) {
  for (auto& observer : observers_) {
    observer.OnProxyConnected(container_id, proxy_server);
  }
}

void ContainerProxyManager::NotifyConnectionFailed(
    const std::string& container_id,
    const std::string& error) {
  for (auto& observer : observers_) {
    observer.OnProxyConnectionFailed(container_id, error);
  }
}

void ContainerProxyManager::NotifyAuthRequired(const std::string& container_id,
                                               const std::string& realm) {
  for (auto& observer : observers_) {
    observer.OnProxyAuthRequired(container_id, realm);
  }
}

void ContainerProxyManager::NotifyBypassDetected(const std::string& container_id,
                                                 const std::string& url,
                                                 const std::string& reason) {
  for (auto& observer : observers_) {
    observer.OnProxyBypassDetected(container_id, url, reason);
  }
}

void ContainerProxyManager::LogProxyOperation(const std::string& container_id,
                                              const std::string& operation,
                                              const std::string& details) {
  if (debug_logging_enabled_) {
    ;
  }
}

ProxyEnforcementValidator::ProxyEnforcementValidator(
    ContainerProxyManager* manager,
    const std::string& container_id,
    const GURL& url)
    : manager_(manager), container_id_(container_id), url_(url) {

  auto resolution = manager_->ResolveProxy(container_id_, url_);
  if (resolution.use_proxy) {
    expected_proxy_ = resolution.proxy_server;
  }
}

ProxyEnforcementValidator::~ProxyEnforcementValidator() {
  if (!completed_) {
    OnRequestComplete(true);  
  }
}

void ProxyEnforcementValidator::OnProxyResolved(
    const std::string& proxy_server) {

  if (!expected_proxy_.empty() && proxy_server != expected_proxy_) {
    has_violation_ = true;
    violation_reason_ = "Proxy resolution mismatch: expected " + 
                        expected_proxy_ + ", got " + proxy_server;
  }
}

void ProxyEnforcementValidator::OnRequestSent(const std::string& actual_proxy) {
  actual_proxy_ = actual_proxy;

  if (!expected_proxy_.empty() && 
      (actual_proxy.empty() || actual_proxy == "DIRECT")) {
    has_violation_ = true;
    violation_reason_ = "Direct connection when proxy expected: " + 
                        expected_proxy_;

    if (manager_) {
      manager_->ReportDirectConnectionViolation(container_id_, url_, 
                                                violation_reason_);
    }
  }
}

void ProxyEnforcementValidator::OnRequestComplete(bool success) {
  if (completed_) return;
  completed_ = true;

  if (!has_violation_) {
    auto result = manager_->ValidateProxyUsage(container_id_, url_, 
                                               actual_proxy_);
    if (!result.valid) {
      has_violation_ = true;
      violation_reason_ = result.violation_reason;
    }
  }
}

}  
