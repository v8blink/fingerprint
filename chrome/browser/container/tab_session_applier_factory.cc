
#include "chrome/browser/container/tab_session_applier_factory.h"

#include "base/no_destructor.h"
#include "chrome/browser/container/tab_container_manager_factory.h"
#include "chrome/browser/container/tab_session_applier.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "tanya/browser/tanya_proxy_service_factory.h"

namespace tab_container {

TabSessionApplier* TabSessionApplierFactory::GetForProfile(Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  const bool create =
      tab_container::GetExistingForBrowserContext(profile) != nullptr;
  return static_cast<TabSessionApplier*>(
      GetInstance()->GetServiceForBrowserContext(profile, create));
}

TabSessionApplierFactory* TabSessionApplierFactory::GetInstance() {
  static base::NoDestructor<TabSessionApplierFactory> instance;
  return instance.get();
}

TabSessionApplierFactory::TabSessionApplierFactory()
    : ProfileKeyedServiceFactory(
          "TabSessionApplier",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              .WithGuest(ProfileSelection::kNone)
              .WithSystem(ProfileSelection::kNone)
              .WithAshInternals(ProfileSelection::kNone)
              .Build()) {
  DependsOn(tanya::TanyaProxyServiceFactory::GetInstance());
}

TabSessionApplierFactory::~TabSessionApplierFactory() = default;

std::unique_ptr<KeyedService>
TabSessionApplierFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  if (context->IsOffTheRecord()) {
    return nullptr;
  }

  Profile* profile = Profile::FromBrowserContext(context);
  if (!profile || profile->IsSystemProfile()) {
    return nullptr;
  }

  if (profile->IsGuestSession()) {
    return nullptr;
  }

  if (tab_container::GetExistingForBrowserContext(context) == nullptr) {
    return nullptr;
  }

  return std::make_unique<TabSessionApplier>(profile);
}

}  
