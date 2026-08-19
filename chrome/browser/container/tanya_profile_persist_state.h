
#ifndef CHROME_BROWSER_CONTAINER_TANYA_PROFILE_PERSIST_STATE_H_
#define CHROME_BROWSER_CONTAINER_TANYA_PROFILE_PERSIST_STATE_H_

#include <string>

#include "base/files/file_path.h"
#include "base/timer/timer.h"
#include "components/sessions/core/session_id.h"
#include "content/public/browser/web_contents_user_data.h"

namespace tab_container {

class ScopedTanyaSuppressHardcodedProxy {
 public:
  ScopedTanyaSuppressHardcodedProxy();
  ScopedTanyaSuppressHardcodedProxy(const ScopedTanyaSuppressHardcodedProxy&) =
      delete;
  ScopedTanyaSuppressHardcodedProxy& operator=(
      const ScopedTanyaSuppressHardcodedProxy&) = delete;
  ~ScopedTanyaSuppressHardcodedProxy();

  static bool IsSuppressed();

 private:
  static thread_local int suppression_depth_;
};

class TanyaProfilePersistState
    : public content::WebContentsUserData<TanyaProfilePersistState> {
 public:
  ~TanyaProfilePersistState() override;

  TanyaProfilePersistState(const TanyaProfilePersistState&) = delete;
  TanyaProfilePersistState& operator=(const TanyaProfilePersistState&) = delete;

  static void RegisterAttachedEncryptedProfile(
      content::WebContents* web_contents,
      const base::FilePath& encrypted_file,
      const std::string& proxy_json_from_profile_file,
      const std::string& profile_display_name);

  const base::FilePath& attached_encrypted_path() const { return path_; }
  const std::string& attached_profile_display_name() const {
    return attached_profile_display_name_;
  }
  const std::string& profile_file_proxy_json() const {
    return profile_file_proxy_json_;
  }

  SessionID tanya_profile_instance_id() const { return tanya_profile_instance_id_; }

 private:
  friend class content::WebContentsUserData<TanyaProfilePersistState>;

  explicit TanyaProfilePersistState(content::WebContents* web_contents);

  void OnPeriodicSaveTick();

  base::FilePath path_;
  std::string profile_file_proxy_json_;
  std::string attached_profile_display_name_;
  SessionID tanya_profile_instance_id_ = SessionID::InvalidValue();
  base::RepeatingTimer save_timer_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  

#endif  
