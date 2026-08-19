
#ifndef CHROME_BROWSER_CONTAINER_CONTAINER_SECURITY_MANAGER_H_
#define CHROME_BROWSER_CONTAINER_CONTAINER_SECURITY_MANAGER_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"

namespace content {
class BrowserContext;
class WebContents;
}  

namespace tab_container {

enum class SecurityIsolationLevel {

  kMinimal = 0,

  kStandard = 1,

  kEnhanced = 2,

  kMaximum = 3,

  kParanoid = 4,
};

enum class CSPEnforcementLevel {
  kOff,          
  kReportOnly,   
  kStandard,     
  kStrict,       
  kCustom,       
};

enum class PermissionPolicy {
  kAllow,        
  kDeny,         
  kAsk,          
  kInherit,      
  kDefault,      
};

enum class SecurityThreatType {
  kNone,
  kMalware,
  kPhishing,
  kUnwantedSoftware,
  kSocialEngineering,
  kPotentiallyHarmful,
  kBillingFraud,
  kAbusiveContent,
  kCryptomining,
  kDataExfiltration,
  kUnknown,
};

enum class SecurityEventType {
  kThreatDetected,
  kThreatBlocked,
  kPermissionDenied,
  kPermissionGranted,
  kCSPViolation,
  kMixedContentBlocked,
  kCORSBlocked,
  kCertificateError,
  kSandboxViolation,
  kNavigationBlocked,
  kDownloadBlocked,
  kScriptBlocked,
  kPopupBlocked,
};

struct ContainerSecurityConfig {
  std::string container_id;
  SecurityIsolationLevel isolation_level = SecurityIsolationLevel::kStandard;

  CSPEnforcementLevel csp_level = CSPEnforcementLevel::kStandard;
  std::string custom_csp;
  bool upgrade_insecure_requests = true;
  bool block_mixed_content = true;

  std::map<std::string, PermissionPolicy> permission_policies;

  bool block_dangerous_downloads = true;
  bool block_potentially_dangerous_downloads = true;
  bool require_https = false;
  bool block_http_to_https_redirects = false;

  bool block_eval = false;
  bool block_inline_scripts = false;
  bool block_external_scripts = false;
  std::set<std::string> script_whitelist;
  std::set<std::string> script_blacklist;

  bool block_iframes = false;
  bool block_cross_origin_iframes = false;
  std::set<std::string> iframe_whitelist;

  bool block_form_submission_to_http = true;
  bool block_form_submission_to_external = false;
  bool warn_on_password_fields = true;

  bool block_insecure_websockets = true;
  bool enforce_certificate_transparency = false;
  bool require_valid_certificates = true;
  int min_tls_version = 12;  

  bool enable_site_isolation = true;
  bool enable_strict_process_isolation = false;

  bool enable_safe_browsing = true;
  bool enable_enhanced_safe_browsing = false;

  std::set<std::string> allowed_domains;
  std::set<std::string> blocked_domains;

  ContainerSecurityConfig() {

    permission_policies["geolocation"] = PermissionPolicy::kAsk;
    permission_policies["notifications"] = PermissionPolicy::kAsk;
    permission_policies["camera"] = PermissionPolicy::kAsk;
    permission_policies["microphone"] = PermissionPolicy::kAsk;
    permission_policies["clipboard-read"] = PermissionPolicy::kAsk;
    permission_policies["clipboard-write"] = PermissionPolicy::kAllow;
    permission_policies["payment"] = PermissionPolicy::kAsk;
    permission_policies["usb"] = PermissionPolicy::kDeny;
    permission_policies["serial"] = PermissionPolicy::kDeny;
    permission_policies["bluetooth"] = PermissionPolicy::kDeny;
    permission_policies["midi"] = PermissionPolicy::kAsk;
    permission_policies["autoplay"] = PermissionPolicy::kAllow;
    permission_policies["fullscreen"] = PermissionPolicy::kAllow;
  }
};

struct SecurityEvent {
  base::TimeTicks timestamp;
  std::string container_id;
  SecurityEventType type;
  std::string description;
  std::string url;
  std::string origin;
  SecurityThreatType threat_type;
  bool was_blocked;
  std::map<std::string, std::string> details;
};

struct SecurityStatistics {
  size_t threats_blocked = 0;
  size_t permissions_denied = 0;
  size_t permissions_granted = 0;
  size_t csp_violations = 0;
  size_t mixed_content_blocked = 0;
  size_t cors_blocked = 0;
  size_t certificate_errors = 0;
  size_t navigation_blocked = 0;
  size_t downloads_blocked = 0;
  size_t scripts_blocked = 0;
  size_t popups_blocked = 0;
  std::map<SecurityThreatType, size_t> threats_by_type;
  std::map<std::string, size_t> blocked_domains;
};

class ContainerSecurityObserver : public base::CheckedObserver {
 public:
  ~ContainerSecurityObserver() override = default;

  virtual void OnThreatDetected(const std::string& container_id,
                                SecurityThreatType threat_type,
                                const std::string& url,
                                bool was_blocked) {}
  virtual void OnPermissionRequested(const std::string& container_id,
                                     const std::string& permission,
                                     const std::string& origin) {}
  virtual void OnPermissionDecision(const std::string& container_id,
                                    const std::string& permission,
                                    PermissionPolicy decision) {}
  virtual void OnCSPViolation(const std::string& container_id,
                              const std::string& directive,
                              const std::string& blocked_uri) {}
  virtual void OnSecurityEvent(const std::string& container_id,
                               const SecurityEvent& event) {}
};

class ContainerSecurityManager {
 public:
  explicit ContainerSecurityManager(content::BrowserContext* browser_context);
  ~ContainerSecurityManager();

  ContainerSecurityManager(const ContainerSecurityManager&) = delete;
  ContainerSecurityManager& operator=(const ContainerSecurityManager&) = delete;

  void AddObserver(ContainerSecurityObserver* observer);
  void RemoveObserver(ContainerSecurityObserver* observer);

  bool SetSecurityConfig(const std::string& container_id,
                         const ContainerSecurityConfig& config);

  std::optional<ContainerSecurityConfig> GetSecurityConfig(
      const std::string& container_id) const;

  bool RemoveSecurityConfig(const std::string& container_id);

  bool SetIsolationLevel(const std::string& container_id,
                         SecurityIsolationLevel level);

  SecurityIsolationLevel GetIsolationLevel(
      const std::string& container_id) const;

  void SetDefaultConfig(const ContainerSecurityConfig& config);

  bool ApplySecurityPreset(const std::string& container_id,
                           const std::string& preset_name);

  PermissionPolicy CheckPermission(const std::string& container_id,
                                   const std::string& permission,
                                   const std::string& origin) const;

  bool SetPermissionPolicy(const std::string& container_id,
                           const std::string& permission,
                           PermissionPolicy policy);

  bool GrantPermission(const std::string& container_id,
                       const std::string& permission,
                       const std::string& origin);

  bool DenyPermission(const std::string& container_id,
                      const std::string& permission,
                      const std::string& origin);

  bool ResetPermission(const std::string& container_id,
                       const std::string& permission);

  std::map<std::string, std::vector<std::string>> GetGrantedPermissions(
      const std::string& container_id) const;

  struct NavigationCheckResult {
    bool allowed;
    std::string reason;
    SecurityThreatType threat_type;
    bool should_warn;
    std::string warning_message;
  };
  NavigationCheckResult CheckNavigation(const std::string& container_id,
                                        const std::string& url,
                                        const std::string& referrer) const;

  bool BlockDomain(const std::string& container_id,
                   const std::string& domain);

  bool AllowDomain(const std::string& container_id,
                   const std::string& domain);

  bool IsDomainBlocked(const std::string& container_id,
                       const std::string& domain) const;

  using SafeBrowsingCallback = base::OnceCallback<void(
      SecurityThreatType threat_type, bool is_safe)>;
  void CheckUrlSafety(const std::string& container_id,
                      const std::string& url,
                      SafeBrowsingCallback callback);

  void ReportThreat(const std::string& container_id,
                    const std::string& url,
                    SecurityThreatType threat_type);

  std::vector<std::pair<std::string, SecurityThreatType>> GetBlockedThreats(
      const std::string& container_id) const;

  std::string GetCSPHeader(const std::string& container_id) const;

  bool SetCustomCSP(const std::string& container_id,
                    const std::string& csp);

  bool ValidateCSPDirective(const std::string& directive,
                            const std::string& value) const;

  void ReportCSPViolation(const std::string& container_id,
                          const std::string& directive,
                          const std::string& blocked_uri,
                          const std::string& source_file,
                          int line_number);

  struct CSPViolation {
    base::TimeTicks timestamp;
    std::string directive;
    std::string blocked_uri;
    std::string source_file;
    int line_number;
    std::string original_policy;
  };
  std::vector<CSPViolation> GetCSPViolations(
      const std::string& container_id,
      size_t count = 100) const;

  struct ScriptCheckResult {
    bool allowed;
    std::string reason;
    bool should_sandbox;
  };
  ScriptCheckResult CheckScript(const std::string& container_id,
                                const std::string& script_url,
                                const std::string& page_origin,
                                bool is_inline) const;

  bool WhitelistScript(const std::string& container_id,
                       const std::string& script_url);

  bool BlacklistScript(const std::string& container_id,
                       const std::string& script_url);

  struct DownloadCheckResult {
    bool allowed;
    std::string reason;
    SecurityThreatType threat_type;
    bool should_scan;
  };
  DownloadCheckResult CheckDownload(const std::string& container_id,
                                    const std::string& url,
                                    const std::string& mime_type,
                                    const std::string& suggested_filename) const;

  void ReportDangerousDownload(const std::string& container_id,
                               const std::string& url,
                               const std::string& filename,
                               SecurityThreatType threat_type);

  struct CertificateCheckResult {
    bool valid;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool should_proceed;
  };
  CertificateCheckResult CheckCertificate(
      const std::string& container_id,
      const std::string& host,
      bool is_valid,
      const std::vector<std::string>& cert_errors) const;

  bool AllowCertificateException(const std::string& container_id,
                                 const std::string& host);

  std::set<std::string> GetCertificateExceptions(
      const std::string& container_id) const;

  SecurityStatistics GetStatistics(const std::string& container_id) const;

  SecurityStatistics GetGlobalStatistics() const;

  std::vector<SecurityEvent> GetSecurityEvents(
      const std::string& container_id,
      size_t count = 100) const;

  void SetDebugLoggingEnabled(bool enabled);
  std::string GetDiagnosticReport() const;
  void DumpStateToLog() const;

 private:

  struct ContainerSecurityInfo {
    std::string container_id;
    ContainerSecurityConfig config;
    SecurityStatistics stats;
    std::vector<SecurityEvent> events;
    std::vector<CSPViolation> csp_violations;

    std::map<std::string, std::map<std::string, PermissionPolicy>> 
        origin_permissions;

    std::set<std::string> certificate_exceptions;

    std::vector<std::pair<std::string, SecurityThreatType>> blocked_threats;

    ContainerSecurityInfo() = default;
  };

  ContainerSecurityInfo* GetOrCreateSecurityInfo(
      const std::string& container_id);

  void RecordEvent(const std::string& container_id,
                   SecurityEventType type,
                   const std::string& description,
                   const std::string& url,
                   bool was_blocked,
                   const std::map<std::string, std::string>& details = {});

  void NotifyThreatDetected(const std::string& container_id,
                            SecurityThreatType threat_type,
                            const std::string& url,
                            bool was_blocked);
  void NotifyPermissionRequested(const std::string& container_id,
                                 const std::string& permission,
                                 const std::string& origin);
  void NotifyPermissionDecision(const std::string& container_id,
                                const std::string& permission,
                                PermissionPolicy decision);
  void NotifyCSPViolation(const std::string& container_id,
                          const std::string& directive,
                          const std::string& blocked_uri);
  void NotifySecurityEvent(const std::string& container_id,
                           const SecurityEvent& event);

  std::string GenerateCSP(const ContainerSecurityConfig& config) const;

  SecurityThreatType CheckThreatList(const std::string& url) const;

  raw_ptr<content::BrowserContext> browser_context_;

  std::map<std::string, std::unique_ptr<ContainerSecurityInfo>> security_info_;

  ContainerSecurityConfig default_config_;

  std::map<std::string, ContainerSecurityConfig> security_presets_;

  bool debug_logging_enabled_ = false;

  base::ObserverList<ContainerSecurityObserver> observers_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ContainerSecurityManager> weak_factory_{this};
};

}  

#endif  
