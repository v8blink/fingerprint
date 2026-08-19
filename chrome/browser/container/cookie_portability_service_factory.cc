
#include "chrome/browser/container/cookie_portability_service_factory.h"

#include "base/no_destructor.h"
#include "chrome/browser/container/cookie_portability_service.h"
#include "chrome/browser/container/tab_container_manager_factory.h"
#include "chrome/browser/container/tab_session_applier_factory.h"
#include "chrome/browser/profiles/profile.h"

namespace tab_container {

CookiePortabilityService* CookiePortabilityServiceFactory::GetForProfile(
    Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  const bool create = GetExistingForBrowserContext(profile) != nullptr;
  return static_cast<CookiePortabilityService*>(
      GetInstance()->GetServiceForBrowserContext(profile, create));
}

CookiePortabilityServiceFactory* CookiePortabilityServiceFactory::GetInstance() {
  static base::NoDestructor<CookiePortabilityServiceFactory> instance;
  return instance.get();
}

CookiePortabilityServiceFactory::CookiePortabilityServiceFactory()
    : ProfileKeyedServiceFactory(
          "CookiePortabilityService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              .WithGuest(ProfileSelection::kNone)
              .WithSystem(ProfileSelection::kNone)
              .WithAshInternals(ProfileSelection::kNone)
              .Build()) {
  DependsOn(TabSessionApplierFactory::GetInstance());
}

CookiePortabilityServiceFactory::~CookiePortabilityServiceFactory() = default;

std::unique_ptr<KeyedService>
CookiePortabilityServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  if (!context || context->IsOffTheRecord()) {
    return nullptr;
  }

  Profile* profile = Profile::FromBrowserContext(context);
  if (!profile || profile->IsSystemProfile()) {
    return nullptr;
  }
  if (profile->IsGuestSession()) {
    return nullptr;
  }
  if (GetExistingForBrowserContext(context) == nullptr) {
    return nullptr;
  }
  if (TabSessionApplierFactory::GetForProfile(profile) == nullptr) {
    return nullptr;
  }

  return std::make_unique<CookiePortabilityService>(profile);
}

bool CookiePortabilityServiceFactory::ServiceIsCreatedWithBrowserContext() const {

  return false;
}

}  
