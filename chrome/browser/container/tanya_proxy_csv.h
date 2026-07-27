
#ifndef CHROME_BROWSER_CONTAINER_TANYA_PROXY_CSV_H_
#define CHROME_BROWSER_CONTAINER_TANYA_PROXY_CSV_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"

namespace net {
struct TanyaProxy;
}

namespace tab_container {

base::FilePath GetTanyaProxyCsvPath(const base::FilePath& profile_path);

void InvalidateTanyaProxyCsvCache();

bool AppendBulkProxyLinesFromWebUi(const base::FilePath& profile_path,
                                   const std::vector<std::string>& lines,
                                   std::string* error_out);

std::unique_ptr<net::TanyaProxy> PickRotatingTanyaProxyFromCsvPool(
    const base::FilePath& profile_path,
    int tab_id);

size_t CountQuickLaunchProxyRowsInCsv(const base::FilePath& profile_path);

}  

#endif  
