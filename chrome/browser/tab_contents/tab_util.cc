// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/tab_contents/tab_util.h"

#include "base/supports_user_data.h"
#include "base/unguessable_token.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/chrome_web_ui_controller_factory.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_url_handler.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "extensions/buildflags/buildflags.h"
#include "url/gurl.h"

#if BUILDFLAG(ENABLE_HOSTED_APPS)
#include "extensions/browser/extension_registry.h"
#endif

using content::SiteInstance;

namespace {

const int kTanyaInitialStoragePartitionUserDataKey = 0;

class TanyaInitialStoragePartitionUserData
    : public base::SupportsUserData::Data {
 public:
  explicit TanyaInitialStoragePartitionUserData(std::string partition_id)
      : partition_id_(std::move(partition_id)) {}
  const std::string& partition_id() const { return partition_id_; }

 private:
  std::string partition_id_;
};

}  // namespace

namespace tab_util {

scoped_refptr<SiteInstance> GetSiteInstanceForNewTab(
    Profile* profile,
    GURL url,
    content::SiteInstance* source_site_instance) {
  content::BrowserURLHandler::GetInstance()->RewriteURLIfNecessary(&url,
                                                                   profile);

  if (ChromeWebUIControllerFactory::GetInstance()->UseWebUIForURL(profile, url))
    return SiteInstance::CreateForURL(profile, url);

#if BUILDFLAG(ENABLE_HOSTED_APPS)
  if (extensions::ExtensionRegistry::Get(profile)
          ->enabled_extensions()
          .GetHostedAppByURL(url))
    return SiteInstance::CreateForURL(profile, url);
#endif

  if (source_site_instance) {
    content::StoragePartition* source_partition =
        profile->GetStoragePartition(source_site_instance);
    if (source_partition &&
        source_partition->GetConfig().partition_name().starts_with(
            "tab_partition_")) {
      return SiteInstance::CreateForFixedStoragePartition(
          profile, url, source_partition->GetConfig());
    }
  }

  const std::string partition_name =
      "tab_partition_" + base::UnguessableToken::Create().ToString();
  const content::StoragePartitionConfig partition_config =
      content::StoragePartitionConfig::Create(profile, "tab_container",
                                              partition_name,
                                              profile->IsOffTheRecord());
  return SiteInstance::CreateForFixedStoragePartition(profile, url,
                                                      partition_config);
}

void RecordTanyaInitialStoragePartitionOnWebContents(
    content::WebContents* contents,
    content::SiteInstance* site_instance_from_create) {
  if (!contents || !site_instance_from_create) {
    return;
  }
  content::StoragePartition* partition =
      contents->GetBrowserContext()->GetStoragePartition(
          site_instance_from_create);
  if (!partition) {
    return;
  }
  const std::string& partition_name = partition->GetConfig().partition_name();
  if (!partition_name.starts_with("tab_partition_")) {
    return;
  }
  contents->SetUserData(
      &kTanyaInitialStoragePartitionUserDataKey,
      std::make_unique<TanyaInitialStoragePartitionUserData>(partition_name));
}

std::string GetTanyaInitialStoragePartitionIdIfRecorded(
    const content::WebContents* contents) {
  if (!contents) {
    return std::string();
  }
  auto* data = static_cast<TanyaInitialStoragePartitionUserData*>(
      contents->GetUserData(&kTanyaInitialStoragePartitionUserDataKey));
  return data ? data->partition_id() : std::string();
}

}  // namespace tab_util
