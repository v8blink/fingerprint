
#ifndef CHROME_BROWSER_CONTAINER_TAB_CONTAINER_MANAGER_FACTORY_H_
#define CHROME_BROWSER_CONTAINER_TAB_CONTAINER_MANAGER_FACTORY_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/browser_context.h"

class TabContainerManager;

namespace content {
class WebContents;
}

namespace tab_container {

TabContainerManager* GetForBrowserContext(
    content::BrowserContext* browser_context);

TabContainerManager* GetExistingForBrowserContext(
    content::BrowserContext* browser_context);

std::string GetContainerIdForTab(content::BrowserContext* browser_context,
                                 content::WebContents* web_contents);

}  

#endif  
