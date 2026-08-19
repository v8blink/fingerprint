
#ifndef CHROME_BROWSER_CONTAINER_COOKIE_PORTABILITY_SERVICE_FACTORY_H_
#define CHROME_BROWSER_CONTAINER_COOKIE_PORTABILITY_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace tab_container {

class CookiePortabilityService;

class CookiePortabilityServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static CookiePortabilityService* GetForProfile(Profile* profile);
  static CookiePortabilityServiceFactory* GetInstance();

 private:
  friend class base::NoDestructor<CookiePortabilityServiceFactory>;

  CookiePortabilityServiceFactory();
  ~CookiePortabilityServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  

#endif  
