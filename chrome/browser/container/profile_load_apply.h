
#ifndef CHROME_BROWSER_CONTAINER_PROFILE_LOAD_APPLY_H_
#define CHROME_BROWSER_CONTAINER_PROFILE_LOAD_APPLY_H_

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "tanya/net/tanya_proxy.h"

class Profile;
namespace content {
class WebContents;
}  

namespace tab_container {

std::unique_ptr<net::TanyaProxy> TanyaProxyFromTanyaProfileProxyJson(
    const std::string& proxy_json);

bool ApplyPersistedProfileFromEncryptedFile(
    content::WebContents* web_contents,
    Profile* profile,
    const base::FilePath& encrypted_profile_file,
    std::string* error_out,
    bool wipe_partition = false);

bool InheritTanyaProfileFromOpener(content::WebContents* child,
                                  content::WebContents* opener);

bool FlushTanyaPersistedProfileToDiskIfAttached(content::WebContents* contents);

void FlushTanyaPersistedProfileToDiskIfAttachedAsync(
    content::WebContents* contents);

void WipeTanyaTabPartition(content::WebContents* contents);

std::string SerializeTanyaTabPartitionCookiesJson(content::WebContents* contents);

}  

#endif  
