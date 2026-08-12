#include "chrome/browser/ui/webui/settings/tanya_ua_override_tab_helper.h"

#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"

namespace settings {

TanyaUaOverrideTabHelper::TanyaUaOverrideTabHelper(
    content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<TanyaUaOverrideTabHelper>(*contents) {}

TanyaUaOverrideTabHelper::~TanyaUaOverrideTabHelper() = default;

void TanyaUaOverrideTabHelper::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle || !force_ua_override_) {
    return;
  }
  navigation_handle->SetIsOverridingUserAgent(true);
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(TanyaUaOverrideTabHelper);

}
