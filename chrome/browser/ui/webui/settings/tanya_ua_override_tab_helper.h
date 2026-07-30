#ifndef CHROME_BROWSER_UI_WEBUI_SETTINGS_TANYA_UA_OVERRIDE_TAB_HELPER_H_
#define CHROME_BROWSER_UI_WEBUI_SETTINGS_TANYA_UA_OVERRIDE_TAB_HELPER_H_

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace settings {

class TanyaUaOverrideTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<TanyaUaOverrideTabHelper> {
 public:
  ~TanyaUaOverrideTabHelper() override;
  TanyaUaOverrideTabHelper(const TanyaUaOverrideTabHelper&) = delete;
  TanyaUaOverrideTabHelper& operator=(const TanyaUaOverrideTabHelper&) = delete;

  void set_force_ua_override(bool force) { force_ua_override_ = force; }

  void DidStartNavigation(content::NavigationHandle* navigation_handle) override;

 private:
  friend class content::WebContentsUserData<TanyaUaOverrideTabHelper>;
  explicit TanyaUaOverrideTabHelper(content::WebContents* contents);

  bool force_ua_override_ = false;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace settings

#endif  // CHROME_BROWSER_UI_WEBUI_SETTINGS_TANYA_UA_OVERRIDE_TAB_HELPER_H_
