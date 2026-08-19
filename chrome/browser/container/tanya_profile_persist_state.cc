
#include "chrome/browser/container/tanya_profile_persist_state.h"

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "chrome/browser/container/profile_load_apply.h"
#include "content/public/browser/web_contents.h"

namespace tab_container {

thread_local int ScopedTanyaSuppressHardcodedProxy::suppression_depth_ = 0;

ScopedTanyaSuppressHardcodedProxy::ScopedTanyaSuppressHardcodedProxy() {
  ++suppression_depth_;
}

ScopedTanyaSuppressHardcodedProxy::~ScopedTanyaSuppressHardcodedProxy() {
  DCHECK_GT(suppression_depth_, 0);
  --suppression_depth_;
}

bool ScopedTanyaSuppressHardcodedProxy::IsSuppressed() {
  return suppression_depth_ > 0;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(TanyaProfilePersistState);

TanyaProfilePersistState::TanyaProfilePersistState(content::WebContents* web_contents)
    : content::WebContentsUserData<TanyaProfilePersistState>(*web_contents) {}

TanyaProfilePersistState::~TanyaProfilePersistState() {
  save_timer_.Stop();
}

void TanyaProfilePersistState::RegisterAttachedEncryptedProfile(
    content::WebContents* web_contents,
    const base::FilePath& encrypted_file,
    const std::string& proxy_json_from_profile_file,
    const std::string& profile_display_name) {
  if (!web_contents || encrypted_file.empty()) {
    return;
  }
  TanyaProfilePersistState* state =
      TanyaProfilePersistState::GetOrCreateForWebContents(web_contents);
  state->path_ = encrypted_file;
  state->profile_file_proxy_json_ = proxy_json_from_profile_file;
  state->attached_profile_display_name_ = profile_display_name;
  state->tanya_profile_instance_id_ = SessionID::NewUnique();
  state->save_timer_.Stop();

}

void TanyaProfilePersistState::OnPeriodicSaveTick() {
  FlushTanyaPersistedProfileToDiskIfAttachedAsync(&GetWebContents());
}

}  
