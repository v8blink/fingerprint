
#ifndef CHROME_BROWSER_CONTAINER_TAB_SESSION_APPLIER_FACTORY_H_
#define CHROME_BROWSER_CONTAINER_TAB_SESSION_APPLIER_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace tab_container {

class TabSessionApplier;

class TabSessionApplierFactory : public ProfileKeyedServiceFactory {
 public:
  static TabSessionApplier* GetForProfile(Profile* profile);
  static TabSessionApplierFactory* GetInstance();

 private:
  friend class base::NoDestructor<TabSessionApplierFactory>;

  TabSessionApplierFactory();
  ~TabSessionApplierFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  

#endif  
