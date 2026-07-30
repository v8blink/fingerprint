#ifndef CHROME_BROWSER_UI_WEBUI_SETTINGS_FINGERPRINT_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SETTINGS_FINGERPRINT_HANDLER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"

class Profile;

namespace settings {

class FingerprintHandler : public SettingsPageUIHandler {
 public:
  explicit FingerprintHandler(Profile* profile);
  FingerprintHandler(const FingerprintHandler&) = delete;
  FingerprintHandler& operator=(const FingerprintHandler&) = delete;
  ~FingerprintHandler() override;

  void RegisterMessages() override;
  void OnJavascriptAllowed() override {}
  void OnJavascriptDisallowed() override {}

 private:
  void HandleInitialize(const base::ListValue& args);
  void HandleOpenFingerprintTab(const base::ListValue& args);
  void OnJsonLoaded(std::string contents);

  raw_ptr<Profile> profile_;
  base::ListValue profiles_;
  base::WeakPtrFactory<FingerprintHandler> weak_factory_{this};
};

}  // namespace settings

#endif  // CHROME_BROWSER_UI_WEBUI_SETTINGS_FINGERPRINT_HANDLER_H_
