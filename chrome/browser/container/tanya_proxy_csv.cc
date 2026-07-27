
#include "chrome/browser/container/tanya_proxy_csv.h"

#include <algorithm>
#include <string_view>

#include "base/files/file_util.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/container/createProfiles.h"
#include "tanya/net/tanya_proxy.h"

namespace tab_container {

namespace {

struct CsvProxyRow {
  std::string name;
  std::string host;
  std::string port;
  std::string username;
  std::string password;
  std::string session_type_raw;
  std::string group_name;
  bool use_for_quick_launch = false;
};

base::FilePath& CachedCsvPath() {
  static base::FilePath* p = new base::FilePath();
  return *p;
}

int64_t& CachedFileStamp() {
  static int64_t* v = new int64_t(-1);
  return *v;
}

std::vector<CsvProxyRow>& CachedQuickLaunchRows() {
  static std::vector<CsvProxyRow>* rows = new std::vector<CsvProxyRow>();
  return *rows;
}

base::Lock& PoolLock() {
  static base::Lock* lock = new base::Lock();
  return *lock;
}

bool ParseProxyConnectionString(const std::string& s,
                                std::string* host,
                                std::string* port,
                                std::string* user,
                                std::string* pass) {
  const std::vector<std::string> parts = base::SplitString(
      s, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.size() < 2) {
    return false;
  }
  *host = parts[0];
  *port = parts[1];
  if (parts.size() == 2) {
    user->clear();
    pass->clear();
    return !host->empty() && !port->empty();
  }
  if (parts.size() == 3) {
    *user = parts[2];
    pass->clear();
    return true;
  }
  *user = parts[2];
  pass->clear();
  for (size_t i = 3; i < parts.size(); ++i) {
    if (i > 3) {
      *pass += ':';
    }
    *pass += parts[i];
  }
  return true;
}

bool SessionTypeMatchesQuickLaunch(const std::string& raw) {
  const std::string t =
      base::ToLowerASCII(base::TrimWhitespaceASCII(raw, base::TRIM_ALL));
  return t == "quick launch" || t == "quicklaunch" || t == "both";
}

bool IsLikelyHeaderLine(const std::string& l) {
  const std::string t =
      base::ToLowerASCII(base::TrimWhitespaceASCII(l, base::TRIM_ALL));
  return t.starts_with("name|") && t.find("proxy") != std::string::npos &&
         t.find("session") != std::string::npos;
}

bool ParseDataLine(const std::string& line, CsvProxyRow* out, std::string* error_out) {
  if (error_out) {
    error_out->clear();
  }
  std::string l = line;
  base::TrimWhitespaceASCII(l, base::TRIM_ALL, &l);
  if (l.empty() || l[0] == '#') {
    return false;
  }
  if (IsLikelyHeaderLine(l)) {
    return false;
  }

  std::vector<std::string> cols;
  if (std::count(l.begin(), l.end(), '|') >= 3) {
    cols = base::SplitString(l, "|", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  } else {
    cols = base::SplitString(l, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  }
  if (cols.size() < 3) {
    *error_out =
        "Each line needs: Name | ip:port[:user:pass] | Session Type [| Group]";
    return true;
  }

  out->name = cols[0];
  const std::string& proxy_str = cols[1];
  out->session_type_raw = cols[2];
  out->group_name = cols.size() >= 4 ? cols[3] : std::string();

  if (!ParseProxyConnectionString(proxy_str, &out->host, &out->port,
                                   &out->username, &out->password)) {
    *error_out = "Invalid proxy string (use host:port or host:port:user:pass)";
    return true;
  }
  out->use_for_quick_launch = SessionTypeMatchesQuickLaunch(out->session_type_raw);
  const std::string st = base::ToLowerASCII(
      base::TrimWhitespaceASCII(out->session_type_raw, base::TRIM_ALL));
  if (!out->use_for_quick_launch && st != "saved profile" &&
      st != "savedprofile") {
    if (error_out) {
      *error_out =
          "Session Type must be Saved Profile, Quick Launch, or Both";
    }
    return true;
  }
  if (out->name.empty()) {
    if (error_out) {
      *error_out = "Name is required.";
    }
    return true;
  }
  return true;
}

std::string FormatRowLine(const CsvProxyRow& r) {
  std::string proxy_str = r.host + ":" + r.port;
  if (!r.username.empty() || !r.password.empty()) {
    proxy_str += ":" + r.username + ":" + r.password;
  }
  return base::StrCat({r.name, "|", proxy_str, "|", r.session_type_raw, "|",
                       r.group_name, "\n"});
}

int64_t FileIdentityStamp(const base::FilePath& path) {
  base::File::Info info;
  if (!base::GetFileInfo(path, &info)) {
    return -1;
  }
  return info.last_modified.ToInternalValue();
}

void ReloadQuickLaunchCacheUnlocked(const base::FilePath& path) {
  CachedCsvPath() = path;
  CachedQuickLaunchRows().clear();
  if (!base::PathExists(path)) {
    CachedFileStamp() = -1;
    return;
  }
  CachedFileStamp() = FileIdentityStamp(path);
  if (CachedFileStamp() < 0) {
    return;
  }

  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    CachedFileStamp() = -1;
    return;
  }

  for (std::string_view piece : base::SplitStringPiece(
           contents, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    std::string line(piece);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    CsvProxyRow row;
    std::string err;
    if (!ParseDataLine(line, &row, &err)) {
      continue;
    }
    if (!err.empty()) {
      continue;
    }
    if (row.use_for_quick_launch) {
      CachedQuickLaunchRows().push_back(std::move(row));
    }
  }
}

void EnsureCacheCurrent(const base::FilePath& path) {

  base::ScopedAllowBlockingForTesting allow_blocking;
  const int64_t stamp =
      base::PathExists(path) ? FileIdentityStamp(path) : int64_t{-1};
  if (path == CachedCsvPath() && stamp == CachedFileStamp()) {
    return;
  }
  ReloadQuickLaunchCacheUnlocked(path);
}

}  

base::FilePath GetTanyaProxyCsvPath(const base::FilePath& profile_path) {
  return GetTanyaProfilesBasePath(profile_path).AppendASCII("tanya_proxies.csv");
}

void InvalidateTanyaProxyCsvCache() {
  base::AutoLock lock(PoolLock());
  CachedFileStamp() = -1;
}

bool AppendBulkProxyLinesFromWebUi(const base::FilePath& profile_path,
                                   const std::vector<std::string>& lines,
                                   std::string* error_out) {
  std::vector<CsvProxyRow> new_rows;
  for (const std::string& raw_line : lines) {
    std::string line = raw_line;
    base::TrimWhitespaceASCII(line, base::TRIM_ALL, &line);
    if (line.empty()) {
      continue;
    }
    CsvProxyRow row;
    std::string err;
    if (!ParseDataLine(line, &row, &err)) {
      continue;
    }
    if (!err.empty()) {
      if (error_out) {
        *error_out = err;
      }
      return false;
    }
    new_rows.push_back(std::move(row));
  }

  if (new_rows.empty()) {
    if (error_out) {
      *error_out = "No valid proxy lines to add.";
    }
    return false;
  }

  const base::FilePath path = GetTanyaProxyCsvPath(profile_path);
  base::FilePath parent = path.DirName();
  if (!base::DirectoryExists(parent) && !base::CreateDirectory(parent)) {
    if (error_out) {
      *error_out = "Could not create Tanya profiles directory.";
    }
    return false;
  }

  std::string blob;
  if (base::PathExists(path)) {
    if (!base::ReadFileToString(path, &blob)) {
      if (error_out) {
        *error_out = "Could not read existing tanya_proxies.csv.";
      }
      return false;
    }
  }

  if (blob.empty()) {
    blob = "Name|Proxy string|Session Type|Group Name\n";
  } else if (blob.find("Session Type") == std::string::npos) {
    blob.insert(0, "Name|Proxy string|Session Type|Group Name\n");
  }

  for (const CsvProxyRow& r : new_rows) {
    blob += FormatRowLine(r);
  }

  if (!base::WriteFile(path, blob)) {
    if (error_out) {
      *error_out = "Failed to write tanya_proxies.csv.";
    }
    return false;
  }

  InvalidateTanyaProxyCsvCache();
  if (error_out) {
    error_out->clear();
  }
  return true;
}

std::unique_ptr<net::TanyaProxy> PickRotatingTanyaProxyFromCsvPool(
    const base::FilePath& profile_path,
    int tab_id) {
  const base::FilePath path = GetTanyaProxyCsvPath(profile_path);
  std::vector<CsvProxyRow> pick_from;
  {
    base::AutoLock lock(PoolLock());
    EnsureCacheCurrent(path);
    pick_from = CachedQuickLaunchRows();
  }

  if (pick_from.empty() || tab_id <= 0) {
    return nullptr;
  }

  static const int kRandomOffset =
      base::RandIntInclusive(0, static_cast<int>(pick_from.size() - 1));
  const size_t idx =
      (static_cast<size_t>(tab_id) + static_cast<size_t>(kRandomOffset)) %
      pick_from.size();
  const CsvProxyRow& r = pick_from[idx];
  const std::string proxy_host_port = r.host + ":" + r.port;
  return std::make_unique<net::TanyaProxy>(
      "PROXY " + proxy_host_port, r.username, r.password, proxy_host_port);
}

size_t CountQuickLaunchProxyRowsInCsv(const base::FilePath& profile_path) {
  const base::FilePath path = GetTanyaProxyCsvPath(profile_path);
  base::AutoLock lock(PoolLock());
  EnsureCacheCurrent(path);
  return CachedQuickLaunchRows().size();
}

}  
