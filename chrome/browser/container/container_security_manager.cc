
#include "chrome/browser/container/container_security_manager.h"

#include <algorithm>
#include <sstream>

#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "content/public/browser/browser_context.h"
#include "url/gurl.h"

namespace tab_container {

namespace {

constexpr const char* kLogPrefix = "[ContainerSecurityManager]";

std::string SecurityEventTypeToString(SecurityEventType type) {
  switch (type) {
    case SecurityEventType::kThreatDetected: return "ThreatDetected";
    case SecurityEventType::kThreatBlocked: return "ThreatBlocked";
    case SecurityEventType::kPermissionDenied: return "PermissionDenied";
    case SecurityEventType::kPermissionGranted: return "PermissionGranted";
    case SecurityEventType::kCSPViolation: return "CSPViolation";
    case SecurityEventType::kMixedContentBlocked: return "MixedContentBlocked";
    case SecurityEventType::kCORSBlocked: return "CORSBlocked";
    case SecurityEventType::kCertificateError: return "CertificateError";
    case SecurityEventType::kSandboxViolation: return "SandboxViolation";
    case SecurityEventType::kNavigationBlocked: return "NavigationBlocked";
    case SecurityEventType::kDownloadBlocked: return "DownloadBlocked";
    case SecurityEventType::kScriptBlocked: return "ScriptBlocked";
    case SecurityEventType::kPopupBlocked: return "PopupBlocked";
  }
  return "Unknown";
}

std::string ThreatTypeToString(SecurityThreatType type) {
  switch (type) {
    case SecurityThreatType::kNone: return "None";
    case SecurityThreatType::kMalware: return "Malware";
    case SecurityThreatType::kPhishing: return "Phishing";
    case SecurityThreatType::kUnwantedSoftware: return "UnwantedSoftware";
    case SecurityThreatType::kSocialEngineering: return "SocialEngineering";
    case SecurityThreatType::kPotentiallyHarmful: return "PotentiallyHarmful";
    case SecurityThreatType::kBillingFraud: return "BillingFraud";
    case SecurityThreatType::kAbusiveContent: return "AbusiveContent";
    case SecurityThreatType::kCryptomining: return "Cryptomining";
    case SecurityThreatType::kDataExfiltration: return "DataExfiltration";
    case SecurityThreatType::kUnknown: return "Unknown";
  }
  return "Unknown";
}

std::string IsolationLevelToString(SecurityIsolationLevel level) {
  switch (level) {
    case SecurityIsolationLevel::kMinimal: return "Minimal";
    case SecurityIsolationLevel::kStandard: return "Standard";
    case SecurityIsolationLevel::kEnhanced: return "Enhanced";
    case SecurityIsolationLevel::kMaximum: return "Maximum";
    case SecurityIsolationLevel::kParanoid: return "Paranoid";
  }
  return "Unknown";
}

const std::set<std::string>& GetDangerousExtensions() {
  static const base::NoDestructor<std::set<std::string>> kExt(
      std::set<std::string>{
          ".exe", ".dll", ".scr", ".bat", ".cmd", ".com", ".pif", ".vbs",
          ".vbe", ".js",  ".jse", ".ws",  ".wsf", ".wsc",  ".wsh",
          ".ps1", ".ps1xml", ".ps2", ".ps2xml", ".psc1", ".psc2",
          ".msh", ".msh1", ".msh2", ".mshxml", ".msh1xml", ".msh2xml",
          ".reg", ".inf", ".msi", ".jar", ".app",
      });
  return *kExt;
}

}  

ContainerSecurityManager::ContainerSecurityManager(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  {
    ContainerSecurityConfig minimal;
    minimal.isolation_level = SecurityIsolationLevel::kMinimal;
    minimal.csp_level = CSPEnforcementLevel::kOff;
    minimal.block_mixed_content = false;
    minimal.require_https = false;
    security_presets_["minimal"] = minimal;
  }

  {
    ContainerSecurityConfig standard;
    standard.isolation_level = SecurityIsolationLevel::kStandard;
    standard.csp_level = CSPEnforcementLevel::kStandard;
    standard.block_mixed_content = true;
    security_presets_["standard"] = standard;
  }

  {
    ContainerSecurityConfig strict;
    strict.isolation_level = SecurityIsolationLevel::kEnhanced;
    strict.csp_level = CSPEnforcementLevel::kStrict;
    strict.block_mixed_content = true;
    strict.require_https = true;
    strict.block_eval = true;
    strict.block_inline_scripts = true;
    strict.enable_strict_process_isolation = true;
    security_presets_["strict"] = strict;
  }

  {
    ContainerSecurityConfig paranoid;
    paranoid.isolation_level = SecurityIsolationLevel::kParanoid;
    paranoid.csp_level = CSPEnforcementLevel::kStrict;
    paranoid.block_mixed_content = true;
    paranoid.require_https = true;
    paranoid.block_eval = true;
    paranoid.block_inline_scripts = true;
    paranoid.block_external_scripts = true;
    paranoid.block_iframes = true;
    paranoid.enable_strict_process_isolation = true;
    paranoid.enable_enhanced_safe_browsing = true;
    paranoid.enforce_certificate_transparency = true;
    paranoid.min_tls_version = 13;  

    for (auto& pair : paranoid.permission_policies) {
      pair.second = PermissionPolicy::kDeny;
    }
    security_presets_["paranoid"] = paranoid;
  }

  ;
}

ContainerSecurityManager::~ContainerSecurityManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ;
}

void ContainerSecurityManager::AddObserver(
    ContainerSecurityObserver* observer) {
  observers_.AddObserver(observer);
}

void ContainerSecurityManager::RemoveObserver(
    ContainerSecurityObserver* observer) {
  observers_.RemoveObserver(observer);
}

bool ContainerSecurityManager::SetSecurityConfig(
    const std::string& container_id,
    const ContainerSecurityConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);
  info->config = config;
  info->config.container_id = container_id;

  ;

  return true;
}

std::optional<ContainerSecurityConfig>
ContainerSecurityManager::GetSecurityConfig(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = security_info_.find(container_id);
  if (it == security_info_.end()) {
    return std::nullopt;
  }

  return it->second->config;
}

bool ContainerSecurityManager::RemoveSecurityConfig(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return security_info_.erase(container_id) > 0;
}

bool ContainerSecurityManager::SetIsolationLevel(
    const std::string& container_id,
    SecurityIsolationLevel level) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);
  info->config.isolation_level = level;

  return true;
}

SecurityIsolationLevel ContainerSecurityManager::GetIsolationLevel(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = security_info_.find(container_id);
  if (it == security_info_.end()) {
    return default_config_.isolation_level;
  }

  return it->second->config.isolation_level;
}

void ContainerSecurityManager::SetDefaultConfig(
    const ContainerSecurityConfig& config) {
  default_config_ = config;
}

bool ContainerSecurityManager::ApplySecurityPreset(
    const std::string& container_id,
    const std::string& preset_name) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = security_presets_.find(preset_name);
  if (it == security_presets_.end()) {
    ;
    return false;
  }

  return SetSecurityConfig(container_id, it->second);
}

PermissionPolicy ContainerSecurityManager::CheckPermission(
    const std::string& container_id,
    const std::string& permission,
    const std::string& origin) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = security_info_.find(container_id);
  if (it == security_info_.end()) {

    auto policy_it = default_config_.permission_policies.find(permission);
    if (policy_it != default_config_.permission_policies.end()) {
      return policy_it->second;
    }
    return PermissionPolicy::kDefault;
  }

  const ContainerSecurityInfo* info = it->second.get();

  auto origin_it = info->origin_permissions.find(origin);
  if (origin_it != info->origin_permissions.end()) {
    auto perm_it = origin_it->second.find(permission);
    if (perm_it != origin_it->second.end()) {
      return perm_it->second;
    }
  }

  auto policy_it = info->config.permission_policies.find(permission);
  if (policy_it != info->config.permission_policies.end()) {
    return policy_it->second;
  }

  return PermissionPolicy::kDefault;
}

bool ContainerSecurityManager::SetPermissionPolicy(
    const std::string& container_id,
    const std::string& permission,
    PermissionPolicy policy) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);
  info->config.permission_policies[permission] = policy;

  return true;
}

bool ContainerSecurityManager::GrantPermission(
    const std::string& container_id,
    const std::string& permission,
    const std::string& origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);
  info->origin_permissions[origin][permission] = PermissionPolicy::kAllow;
  info->stats.permissions_granted++;

  RecordEvent(container_id, SecurityEventType::kPermissionGranted,
              "Permission granted: " + permission, origin, false);
  NotifyPermissionDecision(container_id, permission, PermissionPolicy::kAllow);

  return true;
}

bool ContainerSecurityManager::DenyPermission(
    const std::string& container_id,
    const std::string& permission,
    const std::string& origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);
  info->origin_permissions[origin][permission] = PermissionPolicy::kDeny;
  info->stats.permissions_denied++;

  RecordEvent(container_id, SecurityEventType::kPermissionDenied,
              "Permission denied: " + permission, origin, true);
  NotifyPermissionDecision(container_id, permission, PermissionPolicy::kDeny);

  return true;
}

bool ContainerSecurityManager::ResetPermission(
    const std::string& container_id,
    const std::string& permission) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = security_info_.find(container_id);
  if (it == security_info_.end()) {
    return false;
  }

  for (auto& origin_pair : it->second->origin_permissions) {
    origin_pair.second.erase(permission);
  }

  it->second->config.permission_policies.erase(permission);

  return true;
}

std::map<std::string, std::vector<std::string>>
ContainerSecurityManager::GetGrantedPermissions(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::map<std::string, std::vector<std::string>> result;

  auto it = security_info_.find(container_id);
  if (it == security_info_.end()) {
    return result;
  }

  for (const auto& origin_pair : it->second->origin_permissions) {
    for (const auto& perm_pair : origin_pair.second) {
      if (perm_pair.second == PermissionPolicy::kAllow) {
        result[perm_pair.first].push_back(origin_pair.first);
      }
    }
  }

  return result;
}

ContainerSecurityManager::NavigationCheckResult
ContainerSecurityManager::CheckNavigation(
    const std::string& container_id,
    const std::string& url,
    const std::string& referrer) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  NavigationCheckResult result;
  result.allowed = true;
  result.threat_type = SecurityThreatType::kNone;
  result.should_warn = false;

  GURL parsed_url(url);
  if (!parsed_url.is_valid()) {
    result.allowed = false;
    result.reason = "Invalid URL";
    return result;
  }

  auto it = security_info_.find(container_id);
  const ContainerSecurityConfig& config = 
      it != security_info_.end() ? it->second->config : default_config_;

  const auto host_view = parsed_url.host();
  const std::string host(host_view.data(), host_view.size());

  if (IsDomainBlocked(container_id, host)) {
    result.allowed = false;
    result.reason = "Domain blocked";
    return result;
  }

  if (!config.allowed_domains.empty()) {
    if (config.allowed_domains.find(host) == config.allowed_domains.end()) {
      result.allowed = false;
      result.reason = "Domain not in allowed list";
      return result;
    }
  }

  if (config.require_https && !parsed_url.SchemeIsCryptographic()) {
    result.allowed = false;
    result.reason = "HTTPS required";
    return result;
  }

  result.threat_type = CheckThreatList(url);
  if (result.threat_type != SecurityThreatType::kNone) {
    result.allowed = false;
    result.reason = "Threat detected: " + ThreatTypeToString(result.threat_type);
    result.should_warn = true;
    result.warning_message = "This site may be dangerous";
  }

  return result;
}

bool ContainerSecurityManager::BlockDomain(
    const std::string& container_id,
    const std::string& domain) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);
  info->config.blocked_domains.insert(domain);
  info->stats.blocked_domains[domain]++;

  return true;
}

bool ContainerSecurityManager::AllowDomain(
    const std::string& container_id,
    const std::string& domain) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);
  info->config.allowed_domains.insert(domain);
  info->config.blocked_domains.erase(domain);

  return true;
}

bool ContainerSecurityManager::IsDomainBlocked(
    const std::string& container_id,
    const std::string& domain) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = security_info_.find(container_id);
  if (it == security_info_.end()) {
    return false;
  }

  return it->second->config.blocked_domains.find(domain) !=
         it->second->config.blocked_domains.end();
}

void ContainerSecurityManager::CheckUrlSafety(
    const std::string& container_id,
    const std::string& url,
    SafeBrowsingCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  SecurityThreatType threat = CheckThreatList(url);
  bool is_safe = (threat == SecurityThreatType::kNone);

  if (!is_safe) {
    ReportThreat(container_id, url, threat);
  }

  std::move(callback).Run(threat, is_safe);
}

void ContainerSecurityManager::ReportThreat(
    const std::string& container_id,
    const std::string& url,
    SecurityThreatType threat_type) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);
  info->blocked_threats.emplace_back(url, threat_type);
  info->stats.threats_blocked++;
  info->stats.threats_by_type[threat_type]++;

  RecordEvent(container_id, SecurityEventType::kThreatBlocked,
              "Threat blocked: " + ThreatTypeToString(threat_type), url, true);
  NotifyThreatDetected(container_id, threat_type, url, true);
}

std::vector<std::pair<std::string, SecurityThreatType>>
ContainerSecurityManager::GetBlockedThreats(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = security_info_.find(container_id);
  if (it == security_info_.end()) {
    return {};
  }

  return it->second->blocked_threats;
}

std::string ContainerSecurityManager::GetCSPHeader(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = security_info_.find(container_id);
  const ContainerSecurityConfig& config = 
      it != security_info_.end() ? it->second->config : default_config_;

  if (config.csp_level == CSPEnforcementLevel::kCustom) {
    return config.custom_csp;
  }

  return GenerateCSP(config);
}

bool ContainerSecurityManager::SetCustomCSP(
    const std::string& container_id,
    const std::string& csp) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);
  info->config.csp_level = CSPEnforcementLevel::kCustom;
  info->config.custom_csp = csp;

  return true;
}

bool ContainerSecurityManager::ValidateCSPDirective(
    const std::string& directive,
    const std::string& value) const {

  static const base::NoDestructor<std::set<std::string>> valid_directives(
      std::set<std::string>{
          "default-src", "script-src", "style-src", "img-src", "font-src",
          "connect-src", "media-src",  "object-src", "frame-src", "child-src",
          "worker-src",  "frame-ancestors", "form-action", "base-uri",
          "plugin-types", "sandbox", "report-uri", "report-to",
          "upgrade-insecure-requests", "block-all-mixed-content",
      });

  return valid_directives->find(directive) != valid_directives->end();
}

void ContainerSecurityManager::ReportCSPViolation(
    const std::string& container_id,
    const std::string& directive,
    const std::string& blocked_uri,
    const std::string& source_file,
    int line_number) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);

  CSPViolation violation;
  violation.timestamp = base::TimeTicks::Now();
  violation.directive = directive;
  violation.blocked_uri = blocked_uri;
  violation.source_file = source_file;
  violation.line_number = line_number;
  violation.original_policy = GetCSPHeader(container_id);

  info->csp_violations.push_back(violation);
  info->stats.csp_violations++;

  if (info->csp_violations.size() > 1000) {
    info->csp_violations.erase(
        info->csp_violations.begin(),
        info->csp_violations.begin() + 500);
  }

  RecordEvent(container_id, SecurityEventType::kCSPViolation,
              "CSP violation: " + directive + " blocked " + blocked_uri,
              blocked_uri, true);
  NotifyCSPViolation(container_id, directive, blocked_uri);
}

std::vector<ContainerSecurityManager::CSPViolation>
ContainerSecurityManager::GetCSPViolations(
    const std::string& container_id,
    size_t count) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = security_info_.find(container_id);
  if (it == security_info_.end()) {
    return {};
  }

  const auto& violations = it->second->csp_violations;
  if (violations.size() <= count) {
    return violations;
  }

  return std::vector<CSPViolation>(
      violations.end() - count, violations.end());
}

ContainerSecurityManager::ScriptCheckResult
ContainerSecurityManager::CheckScript(
    const std::string& container_id,
    const std::string& script_url,
    const std::string& page_origin,
    bool is_inline) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ScriptCheckResult result;
  result.allowed = true;
  result.should_sandbox = false;

  auto it = security_info_.find(container_id);
  const ContainerSecurityConfig& config = 
      it != security_info_.end() ? it->second->config : default_config_;

  if (is_inline && config.block_inline_scripts) {
    result.allowed = false;
    result.reason = "Inline scripts blocked";
    return result;
  }

  if (!is_inline && config.block_external_scripts) {
    result.allowed = false;
    result.reason = "External scripts blocked";
    return result;
  }

  if (!script_url.empty() &&
      config.script_blacklist.find(script_url) != config.script_blacklist.end()) {
    result.allowed = false;
    result.reason = "Script blacklisted";
    return result;
  }

  if (!config.script_whitelist.empty() && !is_inline) {
    bool found = false;
    for (const auto& allowed : config.script_whitelist) {
      if (script_url.find(allowed) != std::string::npos) {
        found = true;
        break;
      }
    }
    if (!found) {
      result.allowed = false;
      result.reason = "Script not in whitelist";
      return result;
    }
  }

  return result;
}

bool ContainerSecurityManager::WhitelistScript(
    const std::string& container_id,
    const std::string& script_url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);
  info->config.script_whitelist.insert(script_url);
  info->config.script_blacklist.erase(script_url);

  return true;
}

bool ContainerSecurityManager::BlacklistScript(
    const std::string& container_id,
    const std::string& script_url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);
  info->config.script_blacklist.insert(script_url);
  info->config.script_whitelist.erase(script_url);

  return true;
}

ContainerSecurityManager::DownloadCheckResult
ContainerSecurityManager::CheckDownload(
    const std::string& container_id,
    const std::string& url,
    const std::string& mime_type,
    const std::string& suggested_filename) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DownloadCheckResult result;
  result.allowed = true;
  result.threat_type = SecurityThreatType::kNone;
  result.should_scan = true;

  auto it = security_info_.find(container_id);
  const ContainerSecurityConfig& config = 
      it != security_info_.end() ? it->second->config : default_config_;

  std::string lower_filename = base::ToLowerASCII(suggested_filename);
  for (const auto& ext : GetDangerousExtensions()) {
    if (base::EndsWith(lower_filename, ext, base::CompareCase::INSENSITIVE_ASCII)) {
      if (config.block_dangerous_downloads) {
        result.allowed = false;
        result.reason = "Dangerous file type blocked";
        result.threat_type = SecurityThreatType::kPotentiallyHarmful;
        return result;
      } else if (config.block_potentially_dangerous_downloads) {
        result.should_scan = true;
      }
    }
  }

  result.threat_type = CheckThreatList(url);
  if (result.threat_type != SecurityThreatType::kNone) {
    result.allowed = false;
    result.reason = "Download from unsafe source";
  }

  return result;
}

void ContainerSecurityManager::ReportDangerousDownload(
    const std::string& container_id,
    const std::string& url,
    const std::string& filename,
    SecurityThreatType threat_type) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);
  info->stats.downloads_blocked++;

  RecordEvent(container_id, SecurityEventType::kDownloadBlocked,
              "Dangerous download blocked: " + filename, url, true);
}

ContainerSecurityManager::CertificateCheckResult
ContainerSecurityManager::CheckCertificate(
    const std::string& container_id,
    const std::string& host,
    bool is_valid,
    const std::vector<std::string>& cert_errors) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  CertificateCheckResult result;
  result.valid = is_valid;
  result.errors = cert_errors;
  result.should_proceed = is_valid;

  if (!is_valid) {

    auto it = security_info_.find(container_id);
    if (it != security_info_.end()) {
      if (it->second->certificate_exceptions.find(host) !=
          it->second->certificate_exceptions.end()) {
        result.should_proceed = true;
        result.warnings.push_back("Certificate exception granted by user");
      }
    }
  }

  return result;
}

bool ContainerSecurityManager::AllowCertificateException(
    const std::string& container_id,
    const std::string& host) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);
  info->certificate_exceptions.insert(host);

  RecordEvent(container_id, SecurityEventType::kCertificateError,
              "Certificate exception allowed for: " + host, host, false);

  return true;
}

std::set<std::string> ContainerSecurityManager::GetCertificateExceptions(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = security_info_.find(container_id);
  if (it == security_info_.end()) {
    return {};
  }

  return it->second->certificate_exceptions;
}

SecurityStatistics ContainerSecurityManager::GetStatistics(
    const std::string& container_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = security_info_.find(container_id);
  if (it == security_info_.end()) {
    return SecurityStatistics();
  }

  return it->second->stats;
}

SecurityStatistics ContainerSecurityManager::GetGlobalStatistics() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  SecurityStatistics global;

  for (const auto& pair : security_info_) {
    const auto& stats = pair.second->stats;
    global.threats_blocked += stats.threats_blocked;
    global.permissions_denied += stats.permissions_denied;
    global.permissions_granted += stats.permissions_granted;
    global.csp_violations += stats.csp_violations;
    global.mixed_content_blocked += stats.mixed_content_blocked;
    global.cors_blocked += stats.cors_blocked;
    global.certificate_errors += stats.certificate_errors;
    global.navigation_blocked += stats.navigation_blocked;
    global.downloads_blocked += stats.downloads_blocked;
    global.scripts_blocked += stats.scripts_blocked;
    global.popups_blocked += stats.popups_blocked;
  }

  return global;
}

std::vector<SecurityEvent> ContainerSecurityManager::GetSecurityEvents(
    const std::string& container_id,
    size_t count) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = security_info_.find(container_id);
  if (it == security_info_.end()) {
    return {};
  }

  const auto& events = it->second->events;
  if (events.size() <= count) {
    return events;
  }

  return std::vector<SecurityEvent>(events.end() - count, events.end());
}

void ContainerSecurityManager::SetDebugLoggingEnabled(bool enabled) {
  debug_logging_enabled_ = enabled;
}

std::string ContainerSecurityManager::GetDiagnosticReport() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::stringstream report;
  report << "=== ContainerSecurityManager Diagnostic Report ===\n\n";

  SecurityStatistics global = GetGlobalStatistics();
  report << "Global Statistics:\n";
  report << "  Threats Blocked: " << global.threats_blocked << "\n";
  report << "  Permissions Denied: " << global.permissions_denied << "\n";
  report << "  Permissions Granted: " << global.permissions_granted << "\n";
  report << "  CSP Violations: " << global.csp_violations << "\n";
  report << "  Downloads Blocked: " << global.downloads_blocked << "\n";
  report << "  Scripts Blocked: " << global.scripts_blocked << "\n\n";

  report << "Security Presets: " << security_presets_.size() << "\n";
  for (const auto& pair : security_presets_) {
    report << "  - " << pair.first << "\n";
  }

  report << "\nContainer Configurations: " << security_info_.size() << "\n";
  for (const auto& pair : security_info_) {
    const auto* info = pair.second.get();
    report << "  - " << pair.first << ":\n";
    report << "      Isolation: " 
           << IsolationLevelToString(info->config.isolation_level) << "\n";
    report << "      Threats Blocked: " << info->stats.threats_blocked << "\n";
    report << "      CSP Violations: " << info->stats.csp_violations << "\n";
    report << "      Blocked Domains: " << info->config.blocked_domains.size() << "\n";
    report << "      Cert Exceptions: " << info->certificate_exceptions.size() << "\n";
  }

  return report.str();
}

void ContainerSecurityManager::DumpStateToLog() const {
  ;
}

ContainerSecurityManager::ContainerSecurityInfo*
ContainerSecurityManager::GetOrCreateSecurityInfo(
    const std::string& container_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = security_info_.find(container_id);
  if (it != security_info_.end()) {
    return it->second.get();
  }

  auto info = std::make_unique<ContainerSecurityInfo>();
  info->container_id = container_id;
  info->config = default_config_;
  info->config.container_id = container_id;

  auto* ptr = info.get();
  security_info_[container_id] = std::move(info);

  return ptr;
}

void ContainerSecurityManager::RecordEvent(
    const std::string& container_id,
    SecurityEventType type,
    const std::string& description,
    const std::string& url,
    bool was_blocked,
    const std::map<std::string, std::string>& details) {
  ContainerSecurityInfo* info = GetOrCreateSecurityInfo(container_id);

  SecurityEvent event;
  event.timestamp = base::TimeTicks::Now();
  event.container_id = container_id;
  event.type = type;
  event.description = description;
  event.url = url;
  event.was_blocked = was_blocked;
  event.details = details;

  GURL parsed(url);
  if (parsed.is_valid()) {
    event.origin = parsed.DeprecatedGetOriginAsURL().spec();
  }

  info->events.push_back(event);

  if (info->events.size() > 1000) {
    info->events.erase(info->events.begin(), info->events.begin() + 500);
  }

  if (debug_logging_enabled_) {
    ;
  }

  NotifySecurityEvent(container_id, event);
}

void ContainerSecurityManager::NotifyThreatDetected(
    const std::string& container_id,
    SecurityThreatType threat_type,
    const std::string& url,
    bool was_blocked) {
  for (auto& observer : observers_) {
    observer.OnThreatDetected(container_id, threat_type, url, was_blocked);
  }
}

void ContainerSecurityManager::NotifyPermissionRequested(
    const std::string& container_id,
    const std::string& permission,
    const std::string& origin) {
  for (auto& observer : observers_) {
    observer.OnPermissionRequested(container_id, permission, origin);
  }
}

void ContainerSecurityManager::NotifyPermissionDecision(
    const std::string& container_id,
    const std::string& permission,
    PermissionPolicy decision) {
  for (auto& observer : observers_) {
    observer.OnPermissionDecision(container_id, permission, decision);
  }
}

void ContainerSecurityManager::NotifyCSPViolation(
    const std::string& container_id,
    const std::string& directive,
    const std::string& blocked_uri) {
  for (auto& observer : observers_) {
    observer.OnCSPViolation(container_id, directive, blocked_uri);
  }
}

void ContainerSecurityManager::NotifySecurityEvent(
    const std::string& container_id,
    const SecurityEvent& event) {
  for (auto& observer : observers_) {
    observer.OnSecurityEvent(container_id, event);
  }
}

std::string ContainerSecurityManager::GenerateCSP(
    const ContainerSecurityConfig& config) const {
  std::stringstream csp;

  switch (config.csp_level) {
    case CSPEnforcementLevel::kOff:
      return "";

    case CSPEnforcementLevel::kReportOnly:
    case CSPEnforcementLevel::kStandard:
      csp << "default-src 'self';";
      csp << " script-src 'self'";
      if (!config.block_inline_scripts) {
        csp << " 'unsafe-inline'";
      }
      if (!config.block_eval) {
        csp << " 'unsafe-eval'";
      }
      csp << ";";
      csp << " style-src 'self' 'unsafe-inline';";
      csp << " img-src 'self' data: https:;";
      csp << " font-src 'self' data:;";
      csp << " connect-src 'self' https:;";
      if (config.block_iframes) {
        csp << " frame-src 'none';";
      } else if (config.block_cross_origin_iframes) {
        csp << " frame-src 'self';";
      }
      break;

    case CSPEnforcementLevel::kStrict:
      csp << "default-src 'none';";
      csp << " script-src 'self' 'strict-dynamic';";
      csp << " style-src 'self';";
      csp << " img-src 'self' data:;";
      csp << " font-src 'self';";
      csp << " connect-src 'self';";
      csp << " frame-src 'none';";
      csp << " base-uri 'self';";
      csp << " form-action 'self';";
      break;

    default:
      break;
  }

  if (config.upgrade_insecure_requests) {
    csp << " upgrade-insecure-requests;";
  }

  if (config.block_mixed_content) {
    csp << " block-all-mixed-content;";
  }

  return csp.str();
}

SecurityThreatType ContainerSecurityManager::CheckThreatList(
    const std::string& url) const {

  std::string lower_url = base::ToLowerASCII(url);

  if (lower_url.find("malware") != std::string::npos) {
    return SecurityThreatType::kMalware;
  }
  if (lower_url.find("phishing") != std::string::npos) {
    return SecurityThreatType::kPhishing;
  }
  if (lower_url.find("cryptominer") != std::string::npos) {
    return SecurityThreatType::kCryptomining;
  }

  return SecurityThreatType::kNone;
}

}  
