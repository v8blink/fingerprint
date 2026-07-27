
#include "chrome/browser/container/tab_container_manager_factory.h"

#include "base/memory/ptr_util.h"
#include "base/supports_user_data.h"
#include "chrome/browser/container/tab_container_manager.h"
#include "content/public/browser/browser_context.h"

namespace tab_container {

namespace {

const char kTabContainerManagerKey[] = "TabContainerManager";

class TabContainerManagerUserData : public base::SupportsUserData::Data {
 public:
  explicit TabContainerManagerUserData(
      content::BrowserContext* browser_context)
      : manager_(std::make_unique<TabContainerManager>(browser_context)) {}

 TabContainerManager* manager() { return manager_.get(); }

 private:
  std::unique_ptr<TabContainerManager> manager_;
};

}  

TabContainerManager* GetForBrowserContext(
    content::BrowserContext* browser_context) {
  if (!browser_context) {
    return nullptr;
  }

  TabContainerManagerUserData* user_data =
      static_cast<TabContainerManagerUserData*>(
          browser_context->GetUserData(kTabContainerManagerKey));

  if (!user_data) {
    auto new_user_data =
        std::make_unique<TabContainerManagerUserData>(browser_context);
    TabContainerManager* manager = new_user_data->manager();
    browser_context->SetUserData(kTabContainerManagerKey,
                                 std::move(new_user_data));
    return manager;
  }

  return user_data->manager();
}

TabContainerManager* GetExistingForBrowserContext(
    content::BrowserContext* browser_context) {
  if (!browser_context) {
    return nullptr;
  }

  TabContainerManagerUserData* user_data =
      static_cast<TabContainerManagerUserData*>(
          browser_context->GetUserData(kTabContainerManagerKey));
  return user_data ? user_data->manager() : nullptr;
}

std::string GetContainerIdForTab(content::BrowserContext* browser_context,
                                 content::WebContents* web_contents) {
  TabContainerManager* manager = GetExistingForBrowserContext(browser_context);
  if (!manager || !web_contents) {
    return std::string();
  }
  return manager->GetContainerIdForTab(web_contents);
}

}  
